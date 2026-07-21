// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryTransportRobot.h"
#include "Atlas_CyberDepot.h"
#include "Agent/FactoryAIController.h"
#include "Agent/FactoryAtlasRobot.h"
#include "Infrastructure/LogisticsItem.h"
#include "Infrastructure/StorageShelf.h"
#include "Infrastructure/HorizontalTray.h"
#include "Infrastructure/IdleWaitingZone.h"
#include "Navigation/CostZoneVolume.h"
#include "Navigation/FactoryNavWaypoint.h"
#include "Assignment/OutboundDispatchSubsystem.h"
#include "Assignment/SmartFactoryManager.h"
#include "Repair/RepairProgressComponent.h"
#include "EventBus/FactoryEventBusSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

AFactoryTransportRobot::AFactoryTransportRobot()
{
	AgentType = EActorType::TransportRobot;
	RepairComponent = CreateDefaultSubobject<URepairProgressComponent>(TEXT("RepairComponent"));
}

void AFactoryTransportRobot::BeginPlay()
{
	Super::BeginPlay();

	// 버그 수정 — 이 로봇은 스켈레탈 메시가 없어 GetMesh()는 항상 빈 컴포넌트다. 실제 바디 겸 물품
	// 소켓("ItemSocket")은 BP에 추가된 스태틱 메시 컴포넌트에 있으므로, 그 소켓을 실제로 가진
	// 컴포넌트를 찾아 캐싱한다(못 찾으면 첫 스태틱 메시 컴포넌트로 대체).
	TArray<UStaticMeshComponent*> StaticMeshComponents;
	GetComponents<UStaticMeshComponent>(StaticMeshComponents);

	for (UStaticMeshComponent* Component : StaticMeshComponents)
	{
		if (Component && Component->DoesSocketExist(PayloadItemSocketName))
		{
			BodyMeshComponent = Component;
			break;
		}
	}

	if (!BodyMeshComponent && StaticMeshComponents.Num() > 0)
	{
		BodyMeshComponent = StaticMeshComponents[0];
	}
}

bool AFactoryTransportRobot::IsMaintenanceDue() const
{
	return OperationCount >= MaintenanceThreshold;
}

float AFactoryTransportRobot::GetOperationRatio() const
{
	return MaintenanceThreshold > 0 ? static_cast<float>(OperationCount) / MaintenanceThreshold : 0.f;
}

void AFactoryTransportRobot::ApplyRestDecay(int32 Amount)
{
	OperationCount = FMath::Max(0, OperationCount - Amount);
}

void AFactoryTransportRobot::ResumeAfterRepair()
{
	// 고장 직전 진행 중이던 트립이 남아있으면(자연 발생 고장은 항상 Working 도중 롤링되므로 CurrentTask가
	// 유효하다) 새 작업을 끼워넣지 않는다 — 그 경우의 "이어서 재개"는 이번 스코프 밖. 트립이 없던 상태
	// (디버그 강제 고장 등)에서 복구됐을 때만 유휴 스윕으로 대기 중인 트립을 받는다.
	if (CurrentTask.IsValid())
	{
		return;
	}

	Super::ResumeAfterRepair();

	if (UWorld* World = GetWorld())
	{
		if (UOutboundDispatchSubsystem* Dispatch = World->GetSubsystem<UOutboundDispatchSubsystem>())
		{
			Dispatch->TryDispatchIdleAgents();
		}
	}
}

bool AFactoryTransportRobot::IsEligibleForQuickCheck() const
{
	return CurrentState == EAgentState::Idle && bIsParkedInIdleZone && IsMaintenanceDue();
}

float AFactoryTransportRobot::ComputeCurrentBreakdownChance() const
{
	if (OperationCount < MaintenanceThreshold)
	{
		return 0.f;
	}

	const int32 OverageUnits = (OperationCount - MaintenanceThreshold) / OverageOperationsPerStep;
	const float Chance = BreakdownChanceBase + static_cast<float>(OverageUnits) * BreakdownChanceOverageMultiplier;
	return FMath::Min(Chance, MaxBreakdownChanceCap);
}

void AFactoryTransportRobot::AcceptTransportTask(const FTransportTask& Task)
{
	LeaveIdleZoneIfParked();
	// 버그 수정 — 웨이포인트 그래프로 이동하던 도중(대기실行 등) 새 트립이 끼어들면, 그때 쥐고 있던 노드
	// 예약을 반납할 기회 없이 바로 넘어가 그 노드가 영구 점유 상태로 남았다
	// (FactoryAgentBase::AbandonAnyActiveWaypointRoute 주석 참고).
	AbandonAnyActiveWaypointRoute();

	CurrentTask = Task;
	TryStartMoveToPoint(Task.PickupPoint.Get(), true);

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
		{
			FTaskLifecycleEvent Event;
			Event.Timestamp = FDateTime::UtcNow();
			Event.EventID = FGuid::NewGuid();
			Event.TaskOrAssignmentID = Task.TaskID;
			Event.EventType = ETaskLifecycleEventType::Assigned;
			Event.ActorID = AgentID;
			Event.ActorType = AgentType;
			Event.ItemType = Task.ItemType;
			EventBus->PublishTaskLifecycle(Event);
		}
	}
}

void AFactoryTransportRobot::OnItemGivenByAtlas(ALogisticsItem* Item)
{
	PayloadItem = Item;
	if (Item && BodyMeshComponent)
	{
		// 버그 수정 — 물품 콜리전이 켜진 채로 부착되면 배송로봇 콜리전과 겹쳐 물리 디페네트레이션이
		// 발생할 수 있다(FactoryAtlasRobot::AttachHeldItem 참고). 들고 있는 동안은 콜리전이 필요 없다.
		Item->SetActorEnableCollision(false);

		// 버그 수정 — 이 로봇은 스켈레탈 메시가 없어 GetMesh()에 붙이면 소켓을 절대 못 찾아 매 틱
		// "No SkeletalMesh for Component" 경고가 반복 출력됐다. 실제 소켓이 있는 BodyMeshComponent
		// (BeginPlay에서 캐싱한 스태틱 메시 컴포넌트)에 부착한다.
		const FName SocketName = BodyMeshComponent->DoesSocketExist(PayloadItemSocketName) ? PayloadItemSocketName : NAME_None;
		Item->AttachToComponent(BodyMeshComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
		{
			FTaskLifecycleEvent Event;
			Event.Timestamp = FDateTime::UtcNow();
			Event.EventID = FGuid::NewGuid();
			Event.TaskOrAssignmentID = CurrentTask.TaskID;
			Event.EventType = ETaskLifecycleEventType::PickedUp;
			Event.ActorID = AgentID;
			Event.ActorType = AgentType;
			Event.ItemType = CurrentTask.ItemType;
			EventBus->PublishTaskLifecycle(Event);
		}
	}

	TryStartMoveToPoint(CurrentTask.DropoffPoint.Get(), false);
}

void AFactoryTransportRobot::OnItemCollectedByAtlas()
{
	if (PayloadItem)
	{
		PayloadItem->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		PayloadItem = nullptr;
	}

	// 버그 수정 — 지금 서있던 지점이 트레이 작업 지점이었다면(출고 트레이 등) 여기서 반납해야 다음
	// 배송로봇이 같은 지점으로 이동을 시도할 수 있다. OnTaskCompleted 이후 경로(다음 트립 수락/대기실행)는
	// 전부 TryStartMoveToPoint를 거치지 않는 경우가 있어(TryHeadToIdleZone 등) 여기서 명시적으로 반납한다.
	ReleaseReservedTrayZone();

	OnTaskCompleted();
}

void AFactoryTransportRobot::OnArrivedAtDestination()
{
	if (TryHandleWaypointRouteArrival())
	{
		return;
	}

	if (TryHandleIdleZoneArrival())
	{
		return;
	}

	// 도착 후 스스로 트레이/선반을 건드리지 않고 파킹 — 아틀라스가 TransferItem으로 다가와야 다음 단계로 넘어간다.
	SetState(EAgentState::Working);
	EvaluateRotationOrContinue();
}

void AFactoryTransportRobot::OnMoveFailedPermanently()
{
	Super::OnMoveFailedPermanently();

	if (!CurrentTask.IsValid())
	{
		// 대기실 이동 등 CurrentTask와 무관한 이동 실패 — 정리할 트립이 없다.
		SetState(EAgentState::Idle);
		return;
	}

	// 버그 수정 — 목적지가 근본적으로 도달 불가능하면(레벨 지오메트리 등) 로봇이 CurrentTask를 영원히
	// 붙든 채 Moving에 멈춰 함대에서 영구 이탈했다. 트레이 존을 반납하고 Idle로 복귀시켜 최소한 이 로봇
	// 자체는 다른 작업을 계속 받게 한다.
	ReleaseReservedTrayZone();

	// 버그 수정(사용자 지시, 근본 원인인 회피 국소최소 문제와 별개로 우선 반영) — 아직 짐을 안 실은
	// 상태(픽업 이동 중 실패)라면 트립을 통째로 재큐잉해도 안전하다(TaskID 유지 — 아틀라스 쪽
	// ReservedSlots의 TripTaskID와 이 값으로 짝지어지므로 바뀌면 그 아틀라스는 영영 못 찾는다).
	// 짐을 이미 실은 상태(배달 이동 중 실패)는 픽업 지점부터 다시 도는 재큐잉이 물품을 중복 생성한다
	// (원본은 허공에 분리된 채 남고, 새 로봇이 같은 슬롯에서 또 하나를 집어옴) — 이 경우는 기존처럼
	// 자동 재큐잉하지 않고 레벨 점검이 필요함을 크게 남긴다.
	if (PayloadItem)
	{
		UE_LOG(LogFactoryDispatch, Error, TEXT("[%s] 목적지 도달 불가로 트립(%s)을 포기 — 짐을 실은 채 실패해 재큐잉하지 않음(중복 생성 방지). 물품/레벨 NavMesh 수동 점검 필요"),
			*GetName(), *CurrentTask.TaskID.ToString());

		// 짐을 든 채로 포기하면 물품이 허공에 남는다 — 최소한 부착만 풀어 시야에서 정리한다.
		PayloadItem->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		PayloadItem = nullptr;
		CurrentTask = FTransportTask();
	}
	else
	{
		UE_LOG(LogFactoryDispatch, Error, TEXT("[%s] 목적지 도달 불가로 트립(%s)을 포기 — 아직 미적재라 재큐잉함"),
			*GetName(), *CurrentTask.TaskID.ToString());

		const FTransportTask FailedTask = CurrentTask;
		CurrentTask = FTransportTask();

		if (UWorld* World = GetWorld())
		{
			if (UOutboundDispatchSubsystem* Dispatch = World->GetSubsystem<UOutboundDispatchSubsystem>())
			{
				Dispatch->RequeueTransportTask(FailedTask);
			}
		}
	}

	SetState(EAgentState::Idle);

	// 버그 수정 — 방금 재큐잉한 트립을 이 스윕이 같은(방금 실패한) 로봇에게 그 자리에서 다시 쥐어줄 수
	// 있다. 동기 호출 그대로 두면 같은 콜스택 안에서 이동 실패→OnMoveFailedPermanently 재귀로 이어질
	// 위험이 있다(아틀라스 쪽에서 실제 재현된 스택 오버플로우와 동일한 구조). EnqueueInboundWork와
	// 동일하게 다음 틱으로 미룬다.
	if (UWorld* World = GetWorld())
	{
		if (UOutboundDispatchSubsystem* Dispatch = World->GetSubsystem<UOutboundDispatchSubsystem>())
		{
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(Dispatch, &UOutboundDispatchSubsystem::TryDispatchIdleAgents));
		}
	}
}

FVector AFactoryTransportRobot::GetTaskPointLocation(AActor* PointActor, bool bIsPickupSide) const
{
	if (const AHorizontalTray* Tray = Cast<AHorizontalTray>(PointActor))
	{
		return Tray->GetTransportRobotWorkLocation();
	}

	if (const AStorageShelf* Shelf = Cast<AStorageShelf>(PointActor))
	{
		// 버그 수정 — 별도 스테이징 지점이 아니라, 이번 트립이 실려 온 정확한 슬롯의 (X,Y) 위치로
		// 아틀라스와 직접 만난다(층 높이는 ComputeWorkLocation이 지상으로 고정해서 무시됨).
		const EWorkZoneType ZoneType = bIsPickupSide ? EWorkZoneType::ShelfOutboundZone : EWorkZoneType::ShelfInboundZone;
		return Shelf->GetTransportRobotWorkLocation(CurrentTask.FloorIndex, CurrentTask.SlotIndex, ZoneType);
	}

	return PointActor ? PointActor->GetActorLocation() : FVector::ZeroVector;
}

AFactoryAtlasRobot* AFactoryTransportRobot::FindAtlasForTrip(const FGuid& TripTaskID) const
{
	if (!TripTaskID.IsValid())
	{
		return nullptr;
	}

	TArray<AActor*> FoundAtlases;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFactoryAtlasRobot::StaticClass(), FoundAtlases);
	for (AActor* Actor : FoundAtlases)
	{
		AFactoryAtlasRobot* Atlas = Cast<AFactoryAtlasRobot>(Actor);
		if (Atlas && Atlas->PendingSlotReservation.bIsValid && Atlas->PendingSlotReservation.TripTaskID == TripTaskID)
		{
			return Atlas;
		}
	}

	return nullptr;
}

AFactoryAgentBase* AFactoryTransportRobot::GetCurrentTripPartner() const
{
	if (!CurrentTask.IsValid())
	{
		return nullptr;
	}
	return FindAtlasForTrip(CurrentTask.TaskID);
}

bool AFactoryTransportRobot::CanProceedFromWaitbound() const
{
	AFactoryAgentBase* Partner = GetCurrentTripPartner();
	return Partner && Partner->CurrentState == EAgentState::Working;
}

void AFactoryTransportRobot::RetargetCurrentTaskSlot(int32 NewFloorIndex, int32 NewSlotIndex)
{
	if (!CurrentTask.IsValid())
	{
		return;
	}

	CurrentTask.FloorIndex = NewFloorIndex;
	CurrentTask.SlotIndex = NewSlotIndex;

	// 이 트립의 선반 다리(Inbound=Dropoff, Outbound=Pickup)를 이미 향하고 있었는지 확인 — 짐을 실은
	// 뒤(Inbound 드롭오프 단계)이거나 애초에 선반이 픽업 지점(Outbound)인 경우만 지금 이 좌표를 실제로
	// 쓰는 중이다. 아직 트레이 쪽(픽업 대기 등)이면 저장된 값만 갱신해두면 나중에 자동으로 새 값을 쓴다.
	if (CurrentState != EAgentState::Moving && CurrentState != EAgentState::Pause && CurrentState != EAgentState::Working)
	{
		return;
	}

	AStorageShelf* Shelf = Cast<AStorageShelf>(CurrentTask.DropoffPoint.Get());
	bool bHeadingToShelf = Shelf && (PayloadItem != nullptr);
	bool bIsPickupSide = false;
	if (!Shelf)
	{
		Shelf = Cast<AStorageShelf>(CurrentTask.PickupPoint.Get());
		bHeadingToShelf = (Shelf != nullptr) && (PayloadItem == nullptr);
		bIsPickupSide = true;
	}

	if (!bHeadingToShelf)
	{
		return;
	}

	UE_LOG(LogFactoryDispatch, Log, TEXT("[%s] 짝 아틀라스의 선반칸 재할당에 맞춰 (Floor=%d,Slot=%d)로 재이동"),
		*GetName(), NewFloorIndex, NewSlotIndex);
	TryStartMoveToPoint(Shelf, bIsPickupSide);
}

void AFactoryTransportRobot::TryStartMoveToPoint(AActor* PointActor, bool bIsPickupSide)
{
	// 다음 목적지로 넘어가는 시점이므로, 이전에 붙잡고 있던 트레이 예약이 있으면 먼저 반납한다.
	ReleaseReservedTrayZone();

	// 버그 수정 — 예전엔 이 아래 트레이 점유 재시도 분기가 SetState(Moving)보다 먼저 return해서, 방금
	// 트립을 배정받은 로봇이 첫 이동 시도에서 하필 작업 지점이 점유 중이면 CurrentTask는 이미 찼는데
	// CurrentState는 Idle에 그대로 머물렀다. 그 틈에 배차 스윕이 다시 돌면 이 로봇을 "아직 유휴"로
	// 오판해 또 배정을 시도하고(TryAssignIdleTransportRobot의 이중배정 방어에 걸려 거부됨), 거부 시
	// 호출부가 "줄 일이 없다"고 보고 TryHeadToIdleZone()으로 보내버려 진행 중이던 트립을 CurrentTask에
	// 남긴 채 로봇만 대기실로 떠나는 문제가 있었다(대기열로 같은 트레이에 로봇이 몰리면서 자주 재현됨).
	// 이동 시도가 시작되는 즉시(점유 재시도 여부와 무관하게) Moving으로 반영해 이 틈을 없앤다.
	SetState(EAgentState::Moving);

	// 버그 수정 — 같은 트립의 아틀라스가 이미 핸드오프 지점 근처에 있다면 서로 Crowd 회피 대상이 되지
	// 않도록 미리 무시를 걸어둔다(이동 실패 Code=1 Blocked로 제자리에서 밀고 당기는 문제 대응).
	AFactoryAtlasRobot* TripAtlas = FindAtlasForTrip(CurrentTask.TaskID);
	if (TripAtlas)
	{
		if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
		{
			AIController->SetAvoidanceIgnoreActor(TripAtlas, true);
		}
	}

	if (AHorizontalTray* Tray = Cast<AHorizontalTray>(PointActor))
	{
		// 버그 수정(사용자 지시) — 트레이는 작업 지점이 하나뿐인데, 같은 아틀라스의 여러 트립이 이
		// 트레이를 순차 방문할 수 있다. 트립 순서는 절대 불변(고장은 그 자리에서 정비되므로 다른
		// 로봇으로 대체 불가)인데, 배송로봇 쪽은 물리적 도착 순서로 존을 선점해버려서 나중 트립의
		// 로봇이 먼저 도착하면 아틀라스가 실제로 기다리는 이전 트립의 로봇이 영원히 못 들어오는 순환
		// 교착이 발생했다(실제 재현). 아틀라스가 아직 내 트립까지 처리하지 않았으면(=이전 트립 처리
		// 중이면 FindAtlasForTrip이 null) 존 예약은커녕 **이동 자체를 시작하지 않는다**(아래
		// TryRequestWaypointRoute 호출 전에 여기서 반환) — 마커에 물리적으로 진입하는 것 자체를 막아,
		// 도착 순서와 무관하게 항상 트립 순서대로만 진입이 성립하게 한다.
		if (!TripAtlas)
		{
			UE_LOG(LogFactoryDispatch, Log, TEXT("[%s] %s 진입 대기 — 아틀라스가 아직 이 트립까지 처리하지 않음(이전 트립 처리 중), %.1f초 후 재확인"),
				*GetName(), *Tray->GetName(), TrayZoneRetryIntervalSeconds);
			PendingMovePoint = PointActor;
			bPendingMoveIsPickupSide = bIsPickupSide;
			GetWorldTimerManager().SetTimer(TrayZoneWaitTimerHandle, this, &AFactoryTransportRobot::RetryMoveToPendingPoint, TrayZoneRetryIntervalSeconds, false);
			return;
		}

		if (!Tray->TryReserveTransportRobotWorkZone(this))
		{
			UE_LOG(LogFactoryDispatch, Log, TEXT("[%s] %s 작업 지점이 이미 점유 중 — %.1f초 후 재시도"),
				*GetName(), *Tray->GetName(), TrayZoneRetryIntervalSeconds);
			PendingMovePoint = PointActor;
			bPendingMoveIsPickupSide = bIsPickupSide;
			GetWorldTimerManager().SetTimer(TrayZoneWaitTimerHandle, this, &AFactoryTransportRobot::RetryMoveToPendingPoint, TrayZoneRetryIntervalSeconds, false);
			return;
		}

		ReservedTrayZone = Tray;
	}
	else if (!Cast<AStorageShelf>(PointActor))
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] TryStartMoveToPoint 실패 — PointActor(%s)가 Tray도 Shelf도 아님"),
			*GetName(), PointActor ? *PointActor->GetName() : TEXT("Invalid"));
		return;
	}

	// 버그 수정 — 슬롯/트레이마다 사람이 미리 지정해둔 고정 도킹 참조(TransportRobotInboundDock(s)) 대신,
	// 목표 마커 좌표에 가장 가깝고 실제로 도달 가능한 웨이포인트를 매번 동적으로 찾는다
	// (Docs/08_Navigation.md §8-B, TryRequestWaypointRoute(nullptr, ...)). 점유/경로 없음도 전부
	// TryRequestWaypointRoute 내부 재시도로만 처리하고 직행 폴백은 두지 않는다 — 대기실 복귀 경로와 동일 원칙.
	TryRequestWaypointRoute(nullptr, GetTaskPointLocation(PointActor, bIsPickupSide));
}

void AFactoryTransportRobot::RetryMoveToPendingPoint()
{
	AActor* Point = PendingMovePoint.Get();
	PendingMovePoint = nullptr;
	if (Point)
	{
		TryStartMoveToPoint(Point, bPendingMoveIsPickupSide);
	}
}

void AFactoryTransportRobot::ReleaseReservedTrayZone()
{
	if (AHorizontalTray* Tray = ReservedTrayZone.Get())
	{
		Tray->ReleaseTransportRobotWorkZone();
	}
	ReservedTrayZone = nullptr;
}

void AFactoryTransportRobot::TriggerBreakdown()
{
	SetState(EAgentState::Broken);
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Repair] %s 고장 발생(Broken) — FullRepair 정비 요청"), *GetName());

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
		{
			FAnomalyEvent Event;
			Event.Timestamp = FDateTime::UtcNow();
			Event.LogID = FGuid::NewGuid();
			Event.Severity = EEventSeverity::Critical;
			Event.ActorID = AgentID;
			Event.ActorType = AgentType;
			Event.AnomalyCode = TEXT("Code:005");
			Event.Location = GetActorLocation();
			Event.RiskValue = ComputeCurrentBreakdownChance();
			EventBus->PublishAnomaly(Event);
		}
	}

	if (UWorld* World = GetWorld())
	{
		if (AMSmartFactoryManager* Manager = World->GetGameState<AMSmartFactoryManager>())
		{
			Manager->RequestMaintenance(this, ERepairType::FullRepair);
		}
	}
}

void AFactoryTransportRobot::EvaluateRotationOrContinue()
{
	if (FMath::FRand() < ComputeCurrentBreakdownChance())
	{
		TriggerBreakdown();
		return;
	}

	// 버그 수정 — 원래 PayloadItem(짐을 들고 있는지)만 봤는데, 이 함수는 도착 직후(OnArrivedAtDestination)
	// 마다 호출된다. 트레이/선반 "픽업" 지점에 막 도착해 아틀라스의 핸드오프를 기다리는 순간은 아직
	// PayloadItem이 없어(안 받았으니까) 이 조건을 그대로 통과해버렸다 — 정비 로테이션이 이 순간을
	// "낀 트립이 없는 안전한 순간"으로 오판해 대기실로 보내버려, CurrentTask는 그대로 남은 채 로봇만
	// 떠나 아틀라스가 영원히 배송로봇을 못 찾는 상태가 됐다(대기열 기능으로 로봇의 트립 회전이 빨라지면서
	// 이 타이밍에 맞아떨어질 확률이 크게 올라가 자주 재현됨). 아틀라스(HandoffStationAssignment로 배정
	// 자체를 다른 로봇에게 넘김)와 달리 배송로봇 쪽엔 대응하는 인계 로직이 없으므로, 진행 중인
	// CurrentTask가 있으면(짐 보유 여부와 무관하게) 아예 로테이션을 고려하지 않는다.
	if (!IsMaintenanceDue() || CurrentTask.IsValid())
	{
		return;
	}

	// 교대를 대신할 로봇이 대기실에 있을 때만 자리를 비운다 — 없으면 계속 작업을 이어간다.
	if (!HasRestedTransportRobotAvailable())
	{
		return;
	}

	// 7단계 후속 — 대기실 검색 대신 자신의 고정 홈 슬롯(HomeIdleZone/HomeSlotIndex)으로 바로 이동한다
	// (선입선출 없음). 이동/도착 처리는 TryHeadToIdleZone/TryHandleIdleZoneArrival과 완전히 동일한 경로.
	TryHeadToIdleZone();
}

bool AFactoryTransportRobot::HasRestedTransportRobotAvailable() const
{
	TArray<AActor*> FoundZones;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AIdleWaitingZone::StaticClass(), FoundZones);

	for (AActor* ZoneActor : FoundZones)
	{
		AIdleWaitingZone* Zone = Cast<AIdleWaitingZone>(ZoneActor);
		if (Zone && Zone->AllowedAgentType == EActorType::TransportRobot && Zone->FindRestedOccupant())
		{
			return true;
		}
	}

	return false;
}

void AFactoryTransportRobot::OnEnterBlockedState()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> FoundZones;
	UGameplayStatics::GetAllActorsOfClass(World, ACostZoneVolume::StaticClass(), FoundZones);

	const FVector MyLocation = GetActorLocation();
	for (AActor* ZoneActor : FoundZones)
	{
		if (ACostZoneVolume* Zone = Cast<ACostZoneVolume>(ZoneActor))
		{
			if (FVector::DistSquared(Zone->GetActorLocation(), MyLocation) <= FMath::Square(BlockedZoneRegisterRadius))
			{
				Zone->RegisterBlocker(this);
				RegisteredBlockedZones.Add(Zone);
			}
		}
	}
}

void AFactoryTransportRobot::OnBlockedTick(float DeltaTime)
{
	Super::OnBlockedTick(DeltaTime);

	if (!bHasRegisteredBlocker)
	{
		bHasRegisteredBlocker = true;
		OnEnterBlockedState();
	}
}

void AFactoryTransportRobot::OnUnblocked()
{
	Super::OnUnblocked();

	if (!bHasRegisteredBlocker)
	{
		return;
	}

	for (const TWeakObjectPtr<ACostZoneVolume>& ZoneRef : RegisteredBlockedZones)
	{
		if (ACostZoneVolume* Zone = ZoneRef.Get())
		{
			Zone->UnregisterBlocker(this);
		}
	}

	RegisteredBlockedZones.Reset();
	bHasRegisteredBlocker = false;
}

void AFactoryTransportRobot::OnTaskCompleted()
{
	SetState(EAgentState::Idle);
	++OperationCount;

	const FGuid CompletedTaskID = CurrentTask.TaskID;
	UOutboundDispatchSubsystem* Dispatch = GetWorld()->GetSubsystem<UOutboundDispatchSubsystem>();
	if (Dispatch)
	{
		Dispatch->OnTransportTaskCompleted(CompletedTaskID, this);
	}

	CurrentTask = FTransportTask();

	// 유휴 전환 즉시 다음 트립을 스스로 이어받는다(Pull 방식 재배정). 다음 트립이 없으면 대기실로 향한다
	// (7단계 신규 — "유휴 로봇은 항상 대기실로" 규칙).
	if (Dispatch)
	{
		FTransportTask NewTask;
		if (!Dispatch->TryAssignIdleTransportRobot(this, NewTask))
		{
			TryHeadToIdleZone();
		}
	}

	if (OperationCount < MaintenanceThreshold)
	{
		return;
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
		{
			FAnomalyEvent Event;
			Event.Timestamp = FDateTime::UtcNow();
			Event.LogID = FGuid::NewGuid();
			Event.Severity = EEventSeverity::Warning;
			Event.ActorID = AgentID;
			Event.ActorType = AgentType;
			Event.AnomalyCode = TEXT("Code:003");
			Event.Location = GetActorLocation();
			Event.RiskValue = ComputeCurrentBreakdownChance();
			EventBus->PublishAnomaly(Event);
		}
	}
}

void AFactoryTransportRobot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFactoryTransportRobot, PayloadItem);
}
