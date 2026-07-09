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
	Super::ResumeAfterRepair();

	// 고장 직전 진행 중이던 배정이 남아있으면(자연 발생 고장은 항상 Working 도중 롤링되므로 CurrentAssignment가
	// 유효하다) 새 배정을 끼워넣지 않는다 — 그 경우의 "이어서 재개"는 Working 재진입이 필요한 별개 문제라
	// 이번 스코프 밖. 배정이 없던 상태(디버그 강제 고장 등)에서 복구됐을 때만 유휴 스윕으로 새 일감을 받는다.
	if (CurrentAssignment.IsValid())
	{
		return;
	}

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
	AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController());
	if (!AIController)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] StartCurrentAssignment 실패 — AFactoryAIController가 없음. CurrentAssignment가 시작되지 못하고 방치됨"), *GetName());
		return;
	}

	if (AHorizontalTray* Tray = Cast<AHorizontalTray>(CurrentAssignment.TargetZoneOwner.Get()))
	{
		if (!Tray->TryReserveWorkZone(this))
		{
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] StartCurrentAssignment 실패 — %s의 WorkZone 예약 실패(다른 아틀라스가 점유 중). CurrentAssignment가 시작되지 못하고 방치됨"), *GetName(), *Tray->GetName());
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

		SetState(EAgentState::Moving);
		AIController->RequestMoveWithFilter(Tray->GetAtlasWorkLocation());
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
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] StartCurrentAssignment 실패 — %s의 %s 예약 실패(다른 아틀라스가 점유 중). CurrentAssignment가 시작되지 못하고 방치됨"),
			*GetName(), *Shelf->GetName(), bEmit ? TEXT("OutboundZone") : TEXT("InboundZone"));
		return;
	}

	// 버그 수정 — 층(Z)과 무관하게 슬롯의 (X,Y) 위치로 아틀라스와 배송로봇이 직접 만난다.
	// 별도의 스테이징 지점을 거치지 않으므로 Inbound/Outbound 모두 동일하게 슬롯 위치로 바로 이동한다.
	if (!PopNextReservedSlot())
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] StartCurrentAssignment 실패 — %s에 ReservedSlots가 비어있음(생성 시점 예약 로직 확인 필요)"), *GetName(), *Shelf->GetName());
		if (bEmit)
		{
			Shelf->ReleaseOutboundZone();
		}
		else
		{
			Shelf->ReleaseInboundZone();
		}
		return;
	}

	const FVector Destination = Shelf->GetAtlasWorkLocation(PendingSlotReservation.FloorIndex, PendingSlotReservation.SlotIndex, CurrentAssignment.ZoneType);

	SetState(EAgentState::Moving);
	AIController->RequestMoveWithFilter(Destination);
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
			Shelf->ReleaseInboundZone();
		}
		else if (CurrentAssignment.ZoneType == EWorkZoneType::ShelfOutboundZone)
		{
			Shelf->ReleaseOutboundZone();
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
