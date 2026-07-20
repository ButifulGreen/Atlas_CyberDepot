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

	if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
	{
		SetState(EAgentState::Moving);
		AIController->RequestMoveWithFilter(GetTaskPointLocation(Task.PickupPoint.Get(), true));
	}

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

	if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
	{
		SetState(EAgentState::Moving);
		AIController->RequestMoveWithFilter(GetTaskPointLocation(CurrentTask.DropoffPoint.Get(), false));
	}
}

void AFactoryTransportRobot::OnItemCollectedByAtlas()
{
	if (PayloadItem)
	{
		PayloadItem->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		PayloadItem = nullptr;
	}

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
	if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
	{
		SetState(EAgentState::Moving);
		AIController->RequestMoveWithFilter(GetTaskPointLocation(Shelf, bIsPickupSide));
	}
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
