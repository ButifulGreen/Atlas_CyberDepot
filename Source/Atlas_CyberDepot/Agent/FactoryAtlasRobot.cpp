// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryAtlasRobot.h"
#include "Atlas_CyberDepot.h"
#include "Agent/FactoryTransportRobot.h"
#include "Agent/FactoryAIController.h"
#include "Infrastructure/LogisticsItem.h"
#include "Infrastructure/StorageShelf.h"
#include "Infrastructure/HorizontalTray.h"
#include "Infrastructure/IdleWaitingZone.h"
#include "Assignment/OutboundDispatchSubsystem.h"
#include "Assignment/InventoryOrderSubsystem.h"
#include "Assignment/SmartFactoryManager.h"
#include "Repair/RepairProgressComponent.h"
#include "EventBus/FactoryEventBusSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

AFactoryAtlasRobot::AFactoryAtlasRobot()
{
	AgentType = EActorType::AtlasRobot;
	RepairComponent = CreateDefaultSubobject<URepairProgressComponent>(TEXT("RepairComponent"));
}

bool AFactoryAtlasRobot::IsMaintenanceDue() const
{
	return OperationCount >= MaintenanceThreshold;
}

float AFactoryAtlasRobot::GetOperationRatio() const
{
	return MaintenanceThreshold > 0 ? static_cast<float>(OperationCount) / MaintenanceThreshold : 0.f;
}

void AFactoryAtlasRobot::ApplyRestDecay(int32 Amount)
{
	OperationCount = FMath::Max(0, OperationCount - Amount);
}

void AFactoryAtlasRobot::ResumeAfterRepair()
{
	// 고장 직전 진행 중이던 배정이 남아있으면(자연 발생 고장은 항상 Working 도중 롤링되므로 CurrentAssignment가
	// 유효하다) 새 배정을 끼워넣지 않는다 — 그 경우의 "이어서 재개"는 Working 재진입이 필요한 별개 문제라
	// 이번 스코프 밖. 배정이 없던 상태(디버그 강제 고장 등)에서 복구됐을 때만 유휴 스윕으로 새 일감을 받는다.
	if (CurrentAssignment.IsValid())
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

bool AFactoryAtlasRobot::IsEligibleForQuickCheck() const
{
	return CurrentState == EAgentState::Idle && bIsParkedInIdleZone && IsMaintenanceDue();
}

float AFactoryAtlasRobot::ComputeCurrentBreakdownChance() const
{
	if (OperationCount < MaintenanceThreshold)
	{
		return 0.f;
	}

	const int32 OverageUnits = (OperationCount - MaintenanceThreshold) / OverageOperationsPerStep;
	const float Chance = BreakdownChanceBase + static_cast<float>(OverageUnits) * BreakdownChanceOverageMultiplier;
	return FMath::Min(Chance, MaxBreakdownChanceCap);
}

void AFactoryAtlasRobot::AcceptStationAssignment(const FStationAssignment& Assignment, bool bIsHandoff)
{
	LeaveIdleZoneIfParked();
	// 버그 수정 — 웨이포인트 그래프로 이동하던 도중(대기실行 등) 새 배정이 끼어들면, 그때 쥐고 있던 노드
	// 예약을 반납할 기회 없이 CurrentAssignment/StartCurrentAssignment로 바로 넘어가 그 노드가 영구
	// 점유 상태로 남았다(FactoryAgentBase::AbandonAnyActiveWaypointRoute 주석 참고).
	AbandonAnyActiveWaypointRoute();

	CurrentAssignment = Assignment;

	if (bIsHandoff)
	{
		// 핸드오프 인수는 대기실/교대 자체가 이번 스코프 밖이라 실사용되지 않는다.
		// (Outbound 핸드오프는 슬롯이 아닌 스테이징에서 시작해 정합성이 완전하지 않음 — Docs/14_OpenIssues.md 참고)
		return;
	}

	StartCurrentAssignment();

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
		{
			FTaskLifecycleEvent Event;
			Event.Timestamp = FDateTime::UtcNow();
			Event.EventID = FGuid::NewGuid();
			Event.TaskOrAssignmentID = Assignment.AssignmentID;
			Event.EventType = ETaskLifecycleEventType::Assigned;
			Event.ActorID = AgentID;
			Event.ActorType = AgentType;
			EventBus->PublishTaskLifecycle(Event);
		}
	}
}

void AFactoryAtlasRobot::TriggerBreakdown()
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

void AFactoryAtlasRobot::EvaluateRotationOrContinue()
{
	if (FMath::FRand() < ComputeCurrentBreakdownChance())
	{
		TriggerBreakdown();
		return;
	}

	if (!IsMaintenanceDue())
	{
		return;
	}

	TArray<AActor*> FoundZones;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AIdleWaitingZone::StaticClass(), FoundZones);

	for (AActor* ZoneActor : FoundZones)
	{
		AIdleWaitingZone* Zone = Cast<AIdleWaitingZone>(ZoneActor);
		if (!Zone || Zone->AllowedAgentType != EActorType::AtlasRobot)
		{
			continue;
		}

		AFactoryAtlasRobot* RestedAtlas = Cast<AFactoryAtlasRobot>(Zone->FindRestedOccupant());
		if (!RestedAtlas)
		{
			continue;
		}

		if (UOutboundDispatchSubsystem* Dispatch = GetWorld()->GetSubsystem<UOutboundDispatchSubsystem>())
		{
			Dispatch->HandoffStationAssignment(CurrentAssignment.AssignmentID, this, RestedAtlas);
		}
		return;
	}

	// 교대 가능한 로봇을 못 찾으면 기존 배정을 유지한 채 계속 진행한다.
}

bool AFactoryAtlasRobot::PopNextReservedSlot()
{
	if (CurrentAssignment.ReservedSlots.Num() == 0)
	{
		return false;
	}

	const FReservedSlotEntry Entry = CurrentAssignment.ReservedSlots[0];
	CurrentAssignment.ReservedSlots.RemoveAt(0);

	PendingSlotReservation.bIsValid = true;
	PendingSlotReservation.FloorIndex = Entry.SlotCoord.X;
	PendingSlotReservation.SlotIndex = Entry.SlotCoord.Y;
	PendingSlotReservation.TripTaskID = Entry.TripTaskID;
	return true;
}

bool AFactoryAtlasRobot::TransferItem(AActor* Source, AActor* Destination)
{
	// 버그 수정 — RemainingCount 감소는 "이 유닛이 이 아틀라스의 책임을 떠나는" 순간(Destination 분기의
	// 성공 시점)에만 여기서 직접 처리한다. 호출부가 결과를 확인 안 하고 별도로 감소시키던 방식은
	// 실패해도 카운트가 줄어 물건이 손에 남은 채 배정이 거짓으로 완료 처리되는 문제가 있었다.

	if (AStorageShelf* SourceShelf = Cast<AStorageShelf>(Source))
	{
		if (!PendingSlotReservation.bIsValid)
		{
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] TransferItem(Shelf->) 실패 — PendingSlotReservation이 유효하지 않음"), *GetName());
			return false;
		}

		const int32 FloorIndex = PendingSlotReservation.FloorIndex;
		const int32 SlotIndex = PendingSlotReservation.SlotIndex;

		// 방어 코드 — 예약(ReserveNextSlot) 시점과 실제 인출 시점 사이에 아이템이 무효화됐다면
		// 슬롯만 정리하고 실패 처리한다(현재 코드베이스에선 도달 경로가 없지만, 향후 아이템 파괴/despawn
		// 기능이 생기면 실제로 일어날 수 있어 미리 막아둔다).
		ALogisticsItem* Item = SourceShelf->GetItemAt(FloorIndex, SlotIndex);
		if (!Item)
		{
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] TransferItem(Shelf->) 실패 — %s의 (Floor=%d, Slot=%d)에 아이템이 없음"),
				*GetName(), *SourceShelf->GetName(), FloorIndex, SlotIndex);
			SourceShelf->ConfirmOutboundRemoved(FloorIndex, SlotIndex);
			PendingSlotReservation = FPendingSlotReservation();
			return false;
		}

		CurrentIKHandTarget = SourceShelf->GetSlotMarkerTransform(FloorIndex, SlotIndex).GetLocation();
		bIsReachingForItem = true;

		AttachHeldItem(Item);
		SourceShelf->ConfirmOutboundRemoved(FloorIndex, SlotIndex);

		// 버그 수정 — PendingSlotReservation을 여기서 즉시 비우지 않는다. 배송로봇에게 건네줄 때까지
		// 같은 슬롯 좌표가 필요한데(ContinueShelfAssignment의 RobotRendezvous 계산), 한 다리(leg)가
		// 완전히 끝날 때까지는 살려두고 ContinueShelfAssignment가 PopNextReservedSlot으로 갱신한다.
		GetWorldTimerManager().SetTimer(IKReachTimerHandle, this, &AFactoryAtlasRobot::ClearIKReachFlag, IKReachHoldSeconds, false);
		return true;
	}

	if (AStorageShelf* DestShelf = Cast<AStorageShelf>(Destination))
	{
		if (!HeldItem || !PendingSlotReservation.bIsValid)
		{
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] TransferItem(->Shelf) 실패 — HeldItem=%s, PendingSlotReservation.bIsValid=%s"),
				*GetName(), HeldItem ? TEXT("있음") : TEXT("없음"), PendingSlotReservation.bIsValid ? TEXT("true") : TEXT("false"));
			return false;
		}

		const int32 FloorIndex = PendingSlotReservation.FloorIndex;
		const int32 SlotIndex = PendingSlotReservation.SlotIndex;

		CurrentIKHandTarget = DestShelf->GetSlotMarkerTransform(FloorIndex, SlotIndex).GetLocation();
		bIsReachingForItem = true;

		ALogisticsItem* Item = HeldItem;
		DestShelf->ConfirmInbound(FloorIndex, SlotIndex, Item);
		DetachHeldItem();

		// 버그 수정 — ConfirmInbound는 데이터 모델(Slots)만 갱신할 뿐 액터 위치는 옮기지 않고,
		// DetachHeldItem(KeepWorldTransform)도 아틀라스 손 위치에 그대로 두기만 한다. AHorizontalTray::
		// OnItemPlacedByAtlas와 동일하게 실제 슬롯 마커 위치로 명시적으로 스냅해야 물품이 선반에 놓인다.
		Item->SetActorLocation(CurrentIKHandTarget);

		// 버그 수정 — 이 호출은 항상 한 다리의 마지막 단계(배송로봇에게서 받아 선반에 놓기)라
		// PendingSlotReservation을 여기서 비워도 안전하다(ContinueShelfAssignment가 다음 슬롯을 팝함).
		GetWorldTimerManager().SetTimer(IKReachTimerHandle, this, &AFactoryAtlasRobot::ClearIKReachFlag, IKReachHoldSeconds, false);
		PendingSlotReservation = FPendingSlotReservation();
		--CurrentAssignment.RemainingCount;
		return true;
	}

	if (AHorizontalTray* SourceTray = Cast<AHorizontalTray>(Source))
	{
		if (ALogisticsItem* Item = SourceTray->CurrentItem.Get())
		{
			AttachHeldItem(Item);
			SourceTray->OnItemCleared();
			return true;
		}
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] TransferItem(Tray->) 실패 — %s에 CurrentItem이 없음"), *GetName(), *SourceTray->GetName());
		return false;
	}

	if (AHorizontalTray* DestTray = Cast<AHorizontalTray>(Destination))
	{
		if (!HeldItem)
		{
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] TransferItem(->Tray) 실패 — HeldItem이 없음"), *GetName());
			return false;
		}

		ALogisticsItem* Item = HeldItem;
		DetachHeldItem();
		DestTray->OnItemPlacedByAtlas(Item);
		--CurrentAssignment.RemainingCount;
		return true;
	}

	if (AFactoryTransportRobot* SourceRobot = Cast<AFactoryTransportRobot>(Source))
	{
		if (ALogisticsItem* Item = SourceRobot->PayloadItem)
		{
			AttachHeldItem(Item);
			SourceRobot->OnItemCollectedByAtlas();
			return true;
		}
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] TransferItem(Robot->) 실패 — %s의 PayloadItem이 없음"), *GetName(), *SourceRobot->GetName());
		return false;
	}

	if (AFactoryTransportRobot* DestRobot = Cast<AFactoryTransportRobot>(Destination))
	{
		if (!HeldItem)
		{
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] TransferItem(->Robot) 실패 — HeldItem이 없음"), *GetName());
			return false;
		}

		ALogisticsItem* Item = HeldItem;
		DetachHeldItem();
		DestRobot->OnItemGivenByAtlas(Item);
		--CurrentAssignment.RemainingCount;
		return true;
	}

	UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] TransferItem 실패 — Source/Destination이 어떤 타입과도 매치되지 않음"), *GetName());
	return false;
}

void AFactoryAtlasRobot::OnArrivedAtDestination()
{
	if (TryHandleWaypointRouteArrival())
	{
		return;
	}

	if (TryHandleIdleZoneArrival())
	{
		return;
	}

	if (PendingHandoffAssignmentID.IsValid())
	{
		const FGuid AssignmentID = PendingHandoffAssignmentID;
		PendingHandoffAssignmentID.Invalidate();

		if (UOutboundDispatchSubsystem* Dispatch = GetWorld()->GetSubsystem<UOutboundDispatchSubsystem>())
		{
			Dispatch->OnHandoffAtlasArrivedAtStagingPoint(AssignmentID);
		}
		return;
	}

	if (!CurrentAssignment.TargetZoneOwner.IsValid())
	{
		return;
	}

	SetState(EAgentState::Working);

	EvaluateRotationOrContinue();
	if (CurrentState == EAgentState::Broken)
	{
		return;
	}

	if (Cast<AHorizontalTray>(CurrentAssignment.TargetZoneOwner.Get()))
	{
		ContinueTrayAssignment();
	}
	else if (Cast<AStorageShelf>(CurrentAssignment.TargetZoneOwner.Get()))
	{
		ContinueShelfAssignment();
	}
}

void AFactoryAtlasRobot::OnMoveFailedPermanently()
{
	Super::OnMoveFailedPermanently();

	if (!CurrentAssignment.TargetZoneOwner.IsValid())
	{
		// 대기실 이동 등 CurrentAssignment와 무관한 이동 실패 — 정리할 존/배정이 없다.
		SetState(EAgentState::Idle);
		return;
	}

	// 버그 수정 — 목적지가 근본적으로 도달 불가능하면(레벨 지오메트리 등) 로봇이 CurrentAssignment를
	// 영원히 붙든 채 Moving에 멈춰 함대에서 영구 이탈했다. 존을 반납하고 Idle로 복귀시켜 최소한 이 로봇
	// 자체는 다른 작업을 계속 받게 한다. 실패한 배정 자체는 자동 재큐잉하지 않는다 — 같은 지점이 원인이면
	// 다음 로봇도 똑같이 실패해 함대 전체가 연쇄적으로 멈출 수 있다. 대신 로그로 레벨 점검이 필요함을 알린다.
	// 버그 수정(사용자 지시) — 자동 재큐잉은 없다고 위에 적어놨었지만, 회피가 막히는 지점에서 배정이
	// 조용히 죽어 사이클 전체가 멈추는 빈도가 테스트를 막을 정도로 잦아 재큐잉을 추가한다. 근본 원인
	// (정지 에이전트+선반 사이 회피 국소최소)은 별도로 다룬다 — 같은 지점이 계속 막히면 다음 아틀라스도
	// 반복 실패할 수 있다는 트레이드오프는 감수.
	UE_LOG(LogFactoryDispatch, Error, TEXT("[%s] 목적지 도달 불가로 배정(%s, 대상=%s)을 포기 — 레벨 NavMesh/지오메트리 또는 혼잡 점검 필요. 재큐잉함"),
		*GetName(), *CurrentAssignment.AssignmentID.ToString(),
		CurrentAssignment.TargetZoneOwner.IsValid() ? *CurrentAssignment.TargetZoneOwner->GetName() : TEXT("Invalid"));

	if (AStorageShelf* Shelf = Cast<AStorageShelf>(CurrentAssignment.TargetZoneOwner.Get()))
	{
		if (CurrentAssignment.ZoneType == EWorkZoneType::ShelfInboundZone)
		{
			Shelf->ReleaseInboundZone(this);
		}
		else if (CurrentAssignment.ZoneType == EWorkZoneType::ShelfOutboundZone)
		{
			Shelf->ReleaseOutboundZone(this);
		}
	}
	else if (AHorizontalTray* Tray = Cast<AHorizontalTray>(CurrentAssignment.TargetZoneOwner.Get()))
	{
		Tray->ReleaseWorkZone();
	}

	UOutboundDispatchSubsystem* Dispatch = GetWorld() ? GetWorld()->GetSubsystem<UOutboundDispatchSubsystem>() : nullptr;
	if (Dispatch)
	{
		// ActiveStationAssignments에 남아있는 원본 항목을 제거(AssignedAtlas가 이 로봇을 가리킨 채
		// 방치되면 영원히 아무도 못 건드리는 유령 배정이 된다).
		Dispatch->OnStationAssignmentCompleted(CurrentAssignment.AssignmentID);

		// PendingSlotReservation은 PopNextReservedSlot이 ReservedSlots에서 이미 꺼내온 "진행 중이던"
		// 슬롯이라 CurrentAssignment.ReservedSlots엔 더 이상 없다 — 재큐잉 전에 앞쪽에 되돌려 놔야
		// 이 슬롯(이미 물리적으로 예약된 재고 슬롯)이 유실되지 않는다.
		FStationAssignment RequeueTarget = CurrentAssignment;
		if (PendingSlotReservation.bIsValid)
		{
			FReservedSlotEntry RestoredEntry;
			RestoredEntry.SlotCoord = FIntPoint(PendingSlotReservation.FloorIndex, PendingSlotReservation.SlotIndex);
			RestoredEntry.TripTaskID = PendingSlotReservation.TripTaskID;
			RequeueTarget.ReservedSlots.Insert(RestoredEntry, 0);
		}
		Dispatch->RequeueStationAssignment(RequeueTarget);
	}

	CurrentAssignment = FStationAssignment();
	PendingSlotReservation = FPendingSlotReservation();
	SetState(EAgentState::Idle);

	// 버그 수정 — 방금 재큐잉한 배정을 이 스윕이 같은(방금 실패한) 아틀라스에게 그 자리에서 다시 쥐어줄
	// 수 있다. 동기 호출 그대로 두면 StartCurrentAssignment→이동 실패가 같은 콜스택 안에서 곧장
	// OnMoveFailedPermanently를 다시 불러 재귀로 이어질 위험이 있다(재현된 스택 오버플로우의 직접 원인 —
	// MoveFailureRetryCount 리셋 누락과 결합해 실제로 터짐). EnqueueInboundWork와 동일하게 다음 틱으로 미룬다.
	if (Dispatch)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(Dispatch, &UOutboundDispatchSubsystem::TryDispatchIdleAgents));
		}
	}
}

void AFactoryAtlasRobot::OnWorkingTick(float DeltaTime)
{
	Super::OnWorkingTick(DeltaTime);

	if (!CurrentAssignment.TargetZoneOwner.IsValid())
	{
		return;
	}

	ZoneRetryTimer += DeltaTime;
	if (ZoneRetryTimer < ZoneRetryIntervalSeconds)
	{
		return;
	}
	ZoneRetryTimer = 0.f;

	if (Cast<AHorizontalTray>(CurrentAssignment.TargetZoneOwner.Get()))
	{
		ContinueTrayAssignment();
	}
	else if (Cast<AStorageShelf>(CurrentAssignment.TargetZoneOwner.Get()))
	{
		ContinueShelfAssignment();
	}
}

void AFactoryAtlasRobot::StartCurrentAssignment()
{
	// 새로 시작하는 시도이므로, 이전에 걸려있던 재시도 타이머(경합 대기)가 있으면 대체한다.
	GetWorldTimerManager().ClearTimer(StartAssignmentRetryTimerHandle);

	AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController());
	if (!AIController)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] StartCurrentAssignment 실패 — AFactoryAIController가 없음. CurrentAssignment가 시작되지 못하고 방치됨"), *GetName());
		return;
	}

	// 버그 수정 — 아래 존 점유 재시도 분기들이 SetState(Moving)보다 먼저 return해서, 방금 배정받은
	// 아틀라스가 첫 시도에서 하필 존이 점유 중이면 CurrentAssignment는 이미 찼는데 CurrentState는
	// Idle에 그대로 머물렀다. 자체 재시도 타이머(StartAssignmentRetryTimerHandle)는 이 아틀라스 본인의
	// 재시도는 보장하지만, 그 사이 배차 스윕이 "아직 유휴"로 오판해 이 아틀라스에게 다른 배정을
	// 또 시도하는 것까지는 막지 못한다(TryAssignIdleAtlas의 이중배정 방어에 걸려 거부되긴 하지만, 배송
	// 로봇 쪽과 동일한 근본 원인). 배정 시작 시도 자체가 시작되는 즉시 Moving으로 반영해 이 틈을 없앤다.
	SetState(EAgentState::Moving);

	if (AHorizontalTray* Tray = Cast<AHorizontalTray>(CurrentAssignment.TargetZoneOwner.Get()))
	{
		if (!Tray->TryReserveWorkZone(this))
		{
			// 버그 수정 — 예전엔 여기서 그냥 포기해 CurrentAssignment가 시작도 못 하고 영구 방치됐다
			// (CurrentState가 Idle에 머물러 배차 스윕에서도 이미 배정된 것으로 보여 재시도가 없었음).
			// 점유 중인 아틀라스가 끝날 때까지 같은 간격으로 재시도한다.
			UE_LOG(LogFactoryDispatch, Log, TEXT("[%s] StartCurrentAssignment 대기 — %s의 WorkZone이 다른 아틀라스 점유 중, %.1f초 후 재시도"),
				*GetName(), *Tray->GetName(), ZoneRetryIntervalSeconds);
			GetWorldTimerManager().SetTimer(StartAssignmentRetryTimerHandle, this, &AFactoryAtlasRobot::StartCurrentAssignment, ZoneRetryIntervalSeconds, false);
			return;
		}

		// 버그 수정 — Tray도 Shelf와 동일하게 트립별 TripTaskID를 미리 꺼내둬야 FindWaitingTransportRobot이
		// 거리 대신 정확한 짝을 찾을 수 있다(Tray는 슬롯 개념이 없어 SlotCoord는 (-1,-1)로 무시됨).
		if (!PopNextReservedSlot())
		{
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] StartCurrentAssignment 실패 — %s에 ReservedSlots(TripTaskID)가 비어있음"), *GetName(), *Tray->GetName());
			Tray->ReleaseWorkZone();
			return;
		}

		// 버그 수정(사용자 지시) — 여기가 예전부터 그래프를 아예 안 타는 순수 직행이었다(트레이는 마커까지
		// 거리가 짧다는 전제로 자유 이동만 써옴). 아틀라스가 Inbound/Outbound 웨이포인트를 쓸 수 있게 된
		// 이상 선반과 동일하게 그래프를 태워야 한다 — 안 그러면 정작 트래픽이 가장 몰리는 트레이 접근이
		// 이번 회피 재설계의 보호를 하나도 못 받는다.
		IgnoreTransportRobotForCurrentTrip(AIController);
		TryRequestWaypointRoute(nullptr, Tray->GetAtlasWorkLocation());
		return;
	}

	AStorageShelf* Shelf = Cast<AStorageShelf>(CurrentAssignment.TargetZoneOwner.Get());
	if (!Shelf)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] StartCurrentAssignment 실패 — TargetZoneOwner가 Tray도 Shelf도 아님"), *GetName());
		return;
	}

	const bool bEmit = (CurrentAssignment.ZoneType == EWorkZoneType::ShelfOutboundZone);
	const bool bZoneReserved = bEmit ? Shelf->TryReserveOutboundZone(this) : Shelf->TryReserveInboundZone(this);
	if (!bZoneReserved)
	{
		// 버그 수정 — Tray 분기와 동일한 이유로 재시도 추가(방치되면 CurrentAssignment가 영구 미아가 됨).
		UE_LOG(LogFactoryDispatch, Log, TEXT("[%s] StartCurrentAssignment 대기 — %s의 %s이 만석, %.1f초 후 재시도"),
			*GetName(), *Shelf->GetName(), bEmit ? TEXT("OutboundZone") : TEXT("InboundZone"), ZoneRetryIntervalSeconds);
		GetWorldTimerManager().SetTimer(StartAssignmentRetryTimerHandle, this, &AFactoryAtlasRobot::StartCurrentAssignment, ZoneRetryIntervalSeconds, false);
		return;
	}

	// 버그 수정 — 층(Z)과 무관하게 슬롯의 (X,Y) 위치로 아틀라스와 배송로봇이 직접 만난다.
	// 별도의 스테이징 지점을 거치지 않으므로 Inbound/Outbound 모두 동일하게 슬롯 위치로 바로 이동한다.
	if (!PopNextReservedSlot())
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] StartCurrentAssignment 실패 — %s에 ReservedSlots가 비어있음(생성 시점 예약 로직 확인 필요)"), *GetName(), *Shelf->GetName());
		if (bEmit)
		{
			Shelf->ReleaseOutboundZone(this);
		}
		else
		{
			Shelf->ReleaseInboundZone(this);
		}
		return;
	}

	const FVector Destination = Shelf->GetAtlasWorkLocation(PendingSlotReservation.FloorIndex, PendingSlotReservation.SlotIndex, CurrentAssignment.ZoneType);

	IgnoreTransportRobotForCurrentTrip(AIController);

	// 버그 수정 — Docs/08_Navigation.md §8-B: 공용 백본으로 구역 경계까지 접근한 뒤 자유 이동(§8-A)으로
	// 전환해야 하는데, 이 웨이포인트 요청 자체가 빠져 있어 대기실/공용 웨이포인트를 무시하고 항상 마커로
	// 직행했다. 구역마다 사람이 미리 지정해둔 고정 진입 웨이포인트 대신, 목표 마커(Destination)에 가장
	// 가깝고 실제로 도달 가능한 웨이포인트를 매번 동적으로 찾는다.
	TryRequestWaypointRoute(nullptr, Destination);
}

AFactoryTransportRobot* AFactoryAtlasRobot::FindWaitingTransportRobot(const FGuid& TripTaskID, bool bNeedsPayload) const
{
	// 버그 수정 — 거리 반경으로 "근처의 아무 로봇"을 추정 매칭하던 방식은 반경 튜닝값에 따라 계속
	// 어긋났다(가까이 있어도 반경 밖, 혹은 다른 트립의 로봇과 오매칭될 위험). 이 트립을 실제로 맡은
	// 로봇(CurrentTask.TaskID 일치)을 정확히 짚고, 그 로봇이 도착(Working) + 짐 보유 상태까지
	// 기대와 일치할 때만 반환한다.
	if (!TripTaskID.IsValid())
	{
		return nullptr;
	}

	TArray<AActor*> FoundRobots;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFactoryTransportRobot::StaticClass(), FoundRobots);

	for (AActor* Actor : FoundRobots)
	{
		AFactoryTransportRobot* Robot = Cast<AFactoryTransportRobot>(Actor);
		if (!Robot || Robot->CurrentTask.TaskID != TripTaskID)
		{
			continue;
		}

		if (Robot->CurrentState != EAgentState::Working)
		{
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] FindWaitingTransportRobot 대기 — 트립(%s) 담당 %s가 아직 도착하지 않음(CurrentState=%d)"),
				*GetName(), *TripTaskID.ToString(), *Robot->GetName(), static_cast<int32>(Robot->CurrentState));
			return nullptr;
		}

		if ((Robot->PayloadItem != nullptr) != bNeedsPayload)
		{
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] FindWaitingTransportRobot 대기 — 트립(%s) 담당 %s의 짐 보유 상태가 기대(%s)와 다름"),
				*GetName(), *TripTaskID.ToString(), *Robot->GetName(), bNeedsPayload ? TEXT("true") : TEXT("false"));
			return nullptr;
		}

		return Robot;
	}

	// 이 트립을 맡을 배송로봇이 아직 배정되지 않음(PendingTransportTasks 대기 중) — 유휴 로봇이 생기면 자연히 해소된다.
	UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] FindWaitingTransportRobot 대기 — 트립(%s)을 맡은 배송로봇이 아직 배정되지 않음"),
		*GetName(), *TripTaskID.ToString());
	return nullptr;
}

AFactoryTransportRobot* AFactoryAtlasRobot::FindTransportRobotForTrip(const FGuid& TripTaskID) const
{
	if (!TripTaskID.IsValid())
	{
		return nullptr;
	}

	TArray<AActor*> FoundRobots;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFactoryTransportRobot::StaticClass(), FoundRobots);
	for (AActor* Actor : FoundRobots)
	{
		AFactoryTransportRobot* Robot = Cast<AFactoryTransportRobot>(Actor);
		if (Robot && Robot->CurrentTask.TaskID == TripTaskID)
		{
			return Robot;
		}
	}

	return nullptr;
}

AFactoryAgentBase* AFactoryAtlasRobot::GetCurrentTripPartner() const
{
	if (!PendingSlotReservation.bIsValid)
	{
		return nullptr;
	}
	return FindTransportRobotForTrip(PendingSlotReservation.TripTaskID);
}

bool AFactoryAtlasRobot::TryHandleFinalHopBrokenBlock(AFactoryAgentBase* BrokenAgent)
{
	AStorageShelf* Shelf = Cast<AStorageShelf>(CurrentAssignment.TargetZoneOwner.Get());
	if (!Shelf || !PendingSlotReservation.bIsValid)
	{
		// 선반 접근이 아니거나(트레이 등, 대안 칸 개념이 없음) 진행 중인 슬롯 정보가 없음 — 기본 Pause로 처리.
		return false;
	}

	const bool bWasInbound = (CurrentAssignment.ZoneType == EWorkZoneType::ShelfInboundZone);
	int32 NewFloorIndex = 0;
	int32 NewSlotIndex = 0;
	ALogisticsItem* UnusedItem = nullptr;

	// 버그 수정(사용자 지시) — 반드시 대체 칸을 먼저 확보한 뒤에만 원래 칸을 반납한다. 순서를 바꾸면
	// (반납부터) 그 사이 다른 입고 작업이 이 칸을 채가서 결국 대체 칸도, 원래 칸도 잃을 수 있다.
	const bool bFoundAlternative = bWasInbound
		? Shelf->TryReserveEmptySlot(NewFloorIndex, NewSlotIndex)
		: Shelf->TryReserveOldestOccupiedSlot(NewFloorIndex, NewSlotIndex, UnusedItem);

	if (!bFoundAlternative)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] 선반(%s) 정비 중인 NPC(%s)가 접근을 막았지만 대체 칸이 없음 — 수리 종료까지 대기"),
			*GetName(), *Shelf->GetName(), *BrokenAgent->GetName());
		return false;
	}

	UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] 선반(%s) 정비 중인 NPC(%s)가 (Floor=%d,Slot=%d) 접근을 막아 (Floor=%d,Slot=%d)로 재할당"),
		*GetName(), *Shelf->GetName(), *BrokenAgent->GetName(), PendingSlotReservation.FloorIndex, PendingSlotReservation.SlotIndex, NewFloorIndex, NewSlotIndex);

	Shelf->ReleaseSlotReservation(PendingSlotReservation.FloorIndex, PendingSlotReservation.SlotIndex, bWasInbound);

	// 짝 배송로봇도 같은 트립이니 물리적으로 같은 칸에서 만나야 핸드오프가 성립한다 — 같이 갱신.
	if (AFactoryTransportRobot* Partner = FindTransportRobotForTrip(PendingSlotReservation.TripTaskID))
	{
		Partner->RetargetCurrentTaskSlot(NewFloorIndex, NewSlotIndex);
	}

	// 이번에 막힌 슬롯만 새 좌표로 바꿔 맨 앞에 다시 끼워넣는다 — CurrentAssignment.ReservedSlots엔
	// PopNextReservedSlot이 이미 이번 트립을 꺼내간 뒤라 "아직 손 안 댄 미래 트립들"만 남아있으므로,
	// 그건 그대로 보존해야 한다(다중 트립 배정 도중이면 나머지 트립을 잃으면 안 됨).
	FStationAssignment RequeueTarget = CurrentAssignment;
	FReservedSlotEntry NewEntry;
	NewEntry.SlotCoord = FIntPoint(NewFloorIndex, NewSlotIndex);
	NewEntry.TripTaskID = PendingSlotReservation.TripTaskID;
	RequeueTarget.ReservedSlots.Insert(NewEntry, 0);

	UOutboundDispatchSubsystem* Dispatch = GetWorld() ? GetWorld()->GetSubsystem<UOutboundDispatchSubsystem>() : nullptr;
	if (Dispatch)
	{
		// ActiveStationAssignments에 남아있는 원본 항목을 제거(방치되면 유령 배정이 된다) — 재큐잉이
		// 그 자리를 새 AssignmentID로 대신한다.
		Dispatch->OnStationAssignmentCompleted(CurrentAssignment.AssignmentID);
		Dispatch->RequeueStationAssignment(RequeueTarget);
	}

	AbandonAnyActiveWaypointRoute();
	CurrentAssignment = FStationAssignment();
	PendingSlotReservation = FPendingSlotReservation();
	SetState(EAgentState::Idle);

	// 버그 수정 — 방금 재큐잉한 배정을 같은 콜스택 안에서 동기적으로 다시 스윕하면(과거 실제 재현된
	// 스택 오버플로우와 동일 구조) 위험하다. 다음 틱으로 미룬다.
	if (Dispatch)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(Dispatch, &UOutboundDispatchSubsystem::TryDispatchIdleAgents));
		}
	}

	return true;
}

void AFactoryAtlasRobot::IgnoreTransportRobotForCurrentTrip(AFactoryAIController* AIController)
{
	if (!AIController || !PendingSlotReservation.bIsValid)
	{
		return;
	}

	if (AFactoryTransportRobot* Robot = FindTransportRobotForTrip(PendingSlotReservation.TripTaskID))
	{
		AIController->SetAvoidanceIgnoreActor(Robot, true);
	}
}

void AFactoryAtlasRobot::ClearTransportRobotIgnore(AFactoryTransportRobot* Robot)
{
	if (!Robot)
	{
		return;
	}

	if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
	{
		AIController->SetAvoidanceIgnoreActor(Robot, false);
	}
}

void AFactoryAtlasRobot::ContinueShelfAssignment()
{
	// 버그 수정 — 아틀라스와 배송로봇은 별도 스테이징 지점이 아니라 슬롯의 (X,Y) 위치에서 직접 만난다.
	// PendingSlotReservation은 이 leg가 완전히 끝날 때까지(양쪽 절반 모두) TransferItem이 지우지 않고
	// 여기서만 PopNextReservedSlot으로 갱신하므로, 재시도 중에도 안전하게 같은 슬롯 좌표를 계속 쓸 수 있다.
	AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController());
	AStorageShelf* Shelf = Cast<AStorageShelf>(CurrentAssignment.TargetZoneOwner.Get());
	if (!AIController || !Shelf || !PendingSlotReservation.bIsValid)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] ContinueShelfAssignment 중단 — AIController=%s, Shelf=%s, PendingSlotReservation.bIsValid=%s"),
			*GetName(), AIController ? TEXT("있음") : TEXT("없음"), Shelf ? TEXT("있음") : TEXT("없음"), PendingSlotReservation.bIsValid ? TEXT("true") : TEXT("false"));
		return;
	}

	const bool bEmit = (CurrentAssignment.ZoneType == EWorkZoneType::ShelfOutboundZone);
	const FGuid TripTaskID = PendingSlotReservation.TripTaskID;

	if (bEmit)
	{
		if (!HeldItem)
		{
			if (!TransferItem(Shelf, nullptr) || !HeldItem)
			{
				return;
			}
		}

		AFactoryTransportRobot* Robot = FindWaitingTransportRobot(TripTaskID, false);
		if (!Robot)
		{
			return;
		}

		if (!TransferItem(nullptr, Robot))
		{
			return;
		}
		ClearTransportRobotIgnore(Robot);
	}
	else // receive (ShelfInboundZone)
	{
		if (!HeldItem)
		{
			AFactoryTransportRobot* Robot = FindWaitingTransportRobot(TripTaskID, true);
			if (!Robot)
			{
				return;
			}

			if (!TransferItem(Robot, nullptr) || !HeldItem)
			{
				return;
			}
			ClearTransportRobotIgnore(Robot);
		}

		if (!TransferItem(nullptr, Shelf))
		{
			return;
		}
	}

	// 이번 슬롯의 leg 완료 — 남은 슬롯이 있으면 그 위치로, 없으면(RemainingCount<=0) 배정 종료.
	if (CurrentAssignment.RemainingCount > 0 && PopNextReservedSlot())
	{
		SetState(EAgentState::Moving);
		AIController->RequestMoveWithFilter(Shelf->GetAtlasWorkLocation(PendingSlotReservation.FloorIndex, PendingSlotReservation.SlotIndex, CurrentAssignment.ZoneType));
	}
	else
	{
		OnAssignmentExhausted();
	}
}

void AFactoryAtlasRobot::ContinueTrayAssignment()
{
	AHorizontalTray* Tray = Cast<AHorizontalTray>(CurrentAssignment.TargetZoneOwner.Get());
	if (!Tray || !PendingSlotReservation.bIsValid)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] ContinueTrayAssignment 중단 — Tray=%s, PendingSlotReservation.bIsValid=%s"),
			*GetName(), Tray ? TEXT("있음") : TEXT("없음"), PendingSlotReservation.bIsValid ? TEXT("true") : TEXT("false"));
		return;
	}

	const bool bEmit = (Tray->Direction == ETrayDirection::Inbound);
	const FGuid TripTaskID = PendingSlotReservation.TripTaskID;

	if (bEmit)
	{
		if (!HeldItem)
		{
			// 컨베이어가 끝(End)까지 다 이동해서 멈춘 뒤에만 집는다 — 아직 이동 중이면 다음 재시도 때 다시 확인.
			if (!Tray->bIsHaltedAtEnd)
			{
				return;
			}

			if (!TransferItem(Tray, nullptr) || !HeldItem)
			{
				return;
			}
		}

		AFactoryTransportRobot* Robot = FindWaitingTransportRobot(TripTaskID, false);
		if (!Robot)
		{
			return;
		}

		if (!TransferItem(nullptr, Robot))
		{
			return;
		}
		ClearTransportRobotIgnore(Robot);
	}
	else
	{
		if (!HeldItem)
		{
			AFactoryTransportRobot* Robot = FindWaitingTransportRobot(TripTaskID, true);
			if (!Robot)
			{
				return;
			}

			if (!TransferItem(Robot, nullptr) || !HeldItem)
			{
				return;
			}
			ClearTransportRobotIgnore(Robot);
		}

		if (!TransferItem(nullptr, Tray))
		{
			return;
		}
	}

	// 이번 트립 종료 — 다음 트립이 남아있으면(RemainingCount는 TransferItem이 이미 감소시킴) 새
	// TripTaskID만 꺼내고 위치는 그대로 머문다(Tray는 슬롯 이동이 없음). 없으면 배정을 종료한다.
	if (CurrentAssignment.RemainingCount > 0 && PopNextReservedSlot())
	{
		if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
		{
			IgnoreTransportRobotForCurrentTrip(AIController);
		}
		return;
	}

	OnAssignmentExhausted();
}

void AFactoryAtlasRobot::OnAssignmentExhausted()
{
	if (AStorageShelf* Shelf = Cast<AStorageShelf>(CurrentAssignment.TargetZoneOwner.Get()))
	{
		if (CurrentAssignment.ZoneType == EWorkZoneType::ShelfInboundZone)
		{
			Shelf->ReleaseInboundZone(this);
		}
		else if (CurrentAssignment.ZoneType == EWorkZoneType::ShelfOutboundZone)
		{
			Shelf->ReleaseOutboundZone(this);
		}
	}
	else if (AHorizontalTray* Tray = Cast<AHorizontalTray>(CurrentAssignment.TargetZoneOwner.Get()))
	{
		Tray->ReleaseWorkZone();
	}

	const FGuid CompletedAssignmentID = CurrentAssignment.AssignmentID;

	SetState(EAgentState::Idle);
	OnTaskCompleted();

	UOutboundDispatchSubsystem* Dispatch = GetWorld()->GetSubsystem<UOutboundDispatchSubsystem>();
	if (Dispatch)
	{
		Dispatch->OnStationAssignmentCompleted(CompletedAssignmentID);
	}

	CurrentAssignment = FStationAssignment();
	PendingSlotReservation = FPendingSlotReservation();

	// 유휴 전환 즉시 재배정을 시도한다. 자기 자신만 재시도하면, 방금 반납한 존을 노리며 대기 중이던
	// 다른 아틀라스가 여전히 놓칠 수 있어(IsZoneOccupied 체크로 건너뛰어져 있던 배정) 전체 스윕으로 처리한다.
	if (Dispatch)
	{
		Dispatch->TryDispatchIdleAgents();
	}
}

void AFactoryAtlasRobot::OnTaskCompleted()
{
	++OperationCount;

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

void AFactoryAtlasRobot::ClearIKReachFlag()
{
	bIsReachingForItem = false;
}

void AFactoryAtlasRobot::AttachHeldItem(ALogisticsItem* Item)
{
	HeldItem = Item;
	if (Item && GetMesh())
	{
		// 버그 수정 — 풀에서 꺼낸 물품은 콜리전이 켜져 있어(TryAcquireItem), 그대로 부착하면 아틀라스
		// 콜리전과 겹쳐 물리 디페네트레이션으로 아틀라스가 멀리 튕겨나가는 문제가 있었다. 들려있는 동안은
		// 아무와도 충돌할 필요가 없으므로 부착 시점에 꺼둔다.
		Item->SetActorEnableCollision(false);

		// 버그 수정 — 아직 스켈레탈 메시 애셋이 없는 상태(아트 미제작)에서 소켓 이름을 그대로 넘기면
		// 매 틱 "No SkeletalMesh for Component" 경고가 반복 출력된다. 소켓이 실제로 있을 때만 지정.
		const FName SocketName = GetMesh()->DoesSocketExist(HeldItemSocketName) ? HeldItemSocketName : NAME_None;
		Item->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
	}
}

void AFactoryAtlasRobot::DetachHeldItem()
{
	if (HeldItem)
	{
		HeldItem->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
	HeldItem = nullptr;
}

void AFactoryAtlasRobot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFactoryAtlasRobot, HeldItem);
}
