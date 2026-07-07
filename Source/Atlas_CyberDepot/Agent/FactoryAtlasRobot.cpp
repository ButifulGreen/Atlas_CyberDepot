// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryAtlasRobot.h"
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

	const int32 OverageUnits = (OperationCount - MaintenanceThreshold) / 5;
	const float Chance = BreakdownChanceBase + static_cast<float>(OverageUnits) * BreakdownChanceOverageMultiplier;
	return FMath::Min(Chance, MaxBreakdownChanceCap);
}

void AFactoryAtlasRobot::AcceptStationAssignment(const FStationAssignment& Assignment, bool bIsHandoff)
{
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

void AFactoryAtlasRobot::EvaluateRotationOrContinue()
{
	if (FMath::FRand() < ComputeCurrentBreakdownChance())
	{
		SetState(EAgentState::Broken);

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

bool AFactoryAtlasRobot::ReserveNextSlot()
{
	AStorageShelf* Shelf = Cast<AStorageShelf>(CurrentAssignment.TargetZoneOwner.Get());
	if (!Shelf)
	{
		return false;
	}

	int32 FloorIndex = 0;
	int32 SlotIndex = 0;
	bool bReserved = false;

	if (CurrentAssignment.ZoneType == EWorkZoneType::ShelfInboundZone)
	{
		bReserved = Shelf->TryReserveEmptySlot(FloorIndex, SlotIndex);
	}
	else if (CurrentAssignment.ZoneType == EWorkZoneType::ShelfOutboundZone)
	{
		ALogisticsItem* Item = nullptr;
		bReserved = Shelf->TryReserveOldestOccupiedSlot(FloorIndex, SlotIndex, Item);
	}

	if (!bReserved)
	{
		return false;
	}

	PendingSlotReservation.bIsValid = true;
	PendingSlotReservation.FloorIndex = FloorIndex;
	PendingSlotReservation.SlotIndex = SlotIndex;
	return true;
}

void AFactoryAtlasRobot::TransferItem(AActor* Source, AActor* Destination)
{
	if (AStorageShelf* SourceShelf = Cast<AStorageShelf>(Source))
	{
		if (!PendingSlotReservation.bIsValid)
		{
			return;
		}

		const int32 FloorIndex = PendingSlotReservation.FloorIndex;
		const int32 SlotIndex = PendingSlotReservation.SlotIndex;

		CurrentIKHandTarget = SourceShelf->GetSlotMarkerTransform(FloorIndex, SlotIndex).GetLocation();
		bIsReachingForItem = true;

		ALogisticsItem* Item = SourceShelf->GetItemAt(FloorIndex, SlotIndex);
		AttachHeldItem(Item);
		SourceShelf->ConfirmOutboundRemoved(FloorIndex, SlotIndex);

		bIsReachingForItem = false;
		PendingSlotReservation = FPendingSlotReservation();
		return;
	}

	if (AStorageShelf* DestShelf = Cast<AStorageShelf>(Destination))
	{
		if (!HeldItem || !PendingSlotReservation.bIsValid)
		{
			return;
		}

		const int32 FloorIndex = PendingSlotReservation.FloorIndex;
		const int32 SlotIndex = PendingSlotReservation.SlotIndex;

		CurrentIKHandTarget = DestShelf->GetSlotMarkerTransform(FloorIndex, SlotIndex).GetLocation();
		bIsReachingForItem = true;

		ALogisticsItem* Item = HeldItem;
		DestShelf->ConfirmInbound(FloorIndex, SlotIndex, Item);
		DetachHeldItem();

		bIsReachingForItem = false;
		PendingSlotReservation = FPendingSlotReservation();
		return;
	}

	if (AHorizontalTray* SourceTray = Cast<AHorizontalTray>(Source))
	{
		if (ALogisticsItem* Item = SourceTray->CurrentItem.Get())
		{
			AttachHeldItem(Item);
			SourceTray->OnItemCleared();
		}
		return;
	}

	if (AHorizontalTray* DestTray = Cast<AHorizontalTray>(Destination))
	{
		if (HeldItem)
		{
			ALogisticsItem* Item = HeldItem;
			DetachHeldItem();
			DestTray->OnItemPlacedByAtlas(Item);
		}
		return;
	}

	if (AFactoryTransportRobot* SourceRobot = Cast<AFactoryTransportRobot>(Source))
	{
		if (ALogisticsItem* Item = SourceRobot->PayloadItem)
		{
			AttachHeldItem(Item);
			SourceRobot->OnItemCollectedByAtlas();
		}
		return;
	}

	if (AFactoryTransportRobot* DestRobot = Cast<AFactoryTransportRobot>(Destination))
	{
		if (HeldItem)
		{
			ALogisticsItem* Item = HeldItem;
			DetachHeldItem();
			DestRobot->OnItemGivenByAtlas(Item);
		}
	}
}

void AFactoryAtlasRobot::OnArrivedAtDestination()
{
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
		return;
	}

	if (AHorizontalTray* Tray = Cast<AHorizontalTray>(CurrentAssignment.TargetZoneOwner.Get()))
	{
		if (!Tray->TryReserveWorkZone(this))
		{
			return;
		}

		SetState(EAgentState::Moving);
		AIController->RequestMoveWithFilter(Tray->GetAtlasWorkLocation());
		return;
	}

	AStorageShelf* Shelf = Cast<AStorageShelf>(CurrentAssignment.TargetZoneOwner.Get());
	if (!Shelf)
	{
		return;
	}

	const bool bEmit = (CurrentAssignment.ZoneType == EWorkZoneType::ShelfOutboundZone);
	const bool bZoneReserved = bEmit ? Shelf->TryReserveOutboundZone(this) : Shelf->TryReserveInboundZone(this);
	if (!bZoneReserved)
	{
		return;
	}

	FVector Destination;
	if (bEmit)
	{
		if (!ReserveNextSlot())
		{
			Shelf->ReleaseOutboundZone();
			return;
		}
		Destination = Shelf->GetAtlasWorkLocation(PendingSlotReservation.FloorIndex, PendingSlotReservation.SlotIndex, CurrentAssignment.ZoneType);
	}
	else
	{
		Destination = Shelf->InboundStagingTransform.GetLocation();
	}

	SetState(EAgentState::Moving);
	AIController->RequestMoveWithFilter(Destination);
}

AFactoryTransportRobot* AFactoryAtlasRobot::FindWaitingTransportRobot(const FVector& Location, bool bNeedsPayload) const
{
	TArray<AActor*> FoundRobots;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFactoryTransportRobot::StaticClass(), FoundRobots);

	for (AActor* Actor : FoundRobots)
	{
		AFactoryTransportRobot* Robot = Cast<AFactoryTransportRobot>(Actor);
		if (!Robot || (Robot->PayloadItem != nullptr) != bNeedsPayload)
		{
			continue;
		}

		if (FVector::DistSquared(Robot->GetActorLocation(), Location) <= FMath::Square(RendezvousSearchRadius))
		{
			return Robot;
		}
	}

	return nullptr;
}

void AFactoryAtlasRobot::ContinueShelfAssignment()
{
	AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController());
	AStorageShelf* Shelf = Cast<AStorageShelf>(CurrentAssignment.TargetZoneOwner.Get());
	if (!AIController || !Shelf)
	{
		return;
	}

	const bool bEmit = (CurrentAssignment.ZoneType == EWorkZoneType::ShelfOutboundZone);
	const FVector StagingLocation = bEmit ? Shelf->OutboundStagingTransform.GetLocation() : Shelf->InboundStagingTransform.GetLocation();

	if (bEmit)
	{
		if (!HeldItem)
		{
			// 슬롯 위치에 막 도착했거나 재시도 중 — PendingSlotReservation은 StartCurrentAssignment/이전 루프에서 이미 예약됨.
			TransferItem(Shelf, nullptr);
			if (!HeldItem)
			{
				return;
			}

			SetState(EAgentState::Moving);
			AIController->RequestMoveWithFilter(StagingLocation);
			return;
		}

		AFactoryTransportRobot* Robot = FindWaitingTransportRobot(StagingLocation, false);
		if (!Robot)
		{
			return;
		}

		TransferItem(nullptr, Robot);
		--CurrentAssignment.RemainingCount;

		if (CurrentAssignment.RemainingCount > 0 && ReserveNextSlot())
		{
			SetState(EAgentState::Moving);
			AIController->RequestMoveWithFilter(Shelf->GetAtlasWorkLocation(PendingSlotReservation.FloorIndex, PendingSlotReservation.SlotIndex, CurrentAssignment.ZoneType));
		}
		else
		{
			OnAssignmentExhausted();
		}
		return;
	}

	// receive (ShelfInboundZone)
	if (!HeldItem)
	{
		AFactoryTransportRobot* Robot = FindWaitingTransportRobot(StagingLocation, true);
		if (!Robot)
		{
			return;
		}

		TransferItem(Robot, nullptr);
		if (!HeldItem || !ReserveNextSlot())
		{
			return;
		}

		SetState(EAgentState::Moving);
		AIController->RequestMoveWithFilter(Shelf->GetAtlasWorkLocation(PendingSlotReservation.FloorIndex, PendingSlotReservation.SlotIndex, CurrentAssignment.ZoneType));
		return;
	}

	TransferItem(nullptr, Shelf);
	--CurrentAssignment.RemainingCount;

	if (CurrentAssignment.RemainingCount > 0)
	{
		SetState(EAgentState::Moving);
		AIController->RequestMoveWithFilter(StagingLocation);
	}
	else
	{
		OnAssignmentExhausted();
	}
}

void AFactoryAtlasRobot::ContinueTrayAssignment()
{
	AHorizontalTray* Tray = Cast<AHorizontalTray>(CurrentAssignment.TargetZoneOwner.Get());
	if (!Tray)
	{
		return;
	}

	const bool bEmit = (Tray->Direction == ETrayDirection::Inbound);
	const FVector RobotRendezvous = Tray->GetTransportRobotWorkLocation();

	if (bEmit)
	{
		if (!HeldItem)
		{
			TransferItem(Tray, nullptr);
		}
		if (!HeldItem)
		{
			return;
		}

		AFactoryTransportRobot* Robot = FindWaitingTransportRobot(RobotRendezvous, false);
		if (!Robot)
		{
			return;
		}

		TransferItem(nullptr, Robot);
	}
	else
	{
		if (!HeldItem)
		{
			AFactoryTransportRobot* Robot = FindWaitingTransportRobot(RobotRendezvous, true);
			if (!Robot)
			{
				return;
			}

			TransferItem(Robot, nullptr);
		}

		if (!HeldItem)
		{
			return;
		}

		TransferItem(nullptr, Tray);
	}

	--CurrentAssignment.RemainingCount;
	if (CurrentAssignment.RemainingCount <= 0)
	{
		OnAssignmentExhausted();
	}
	// RemainingCount > 0이면 이 자리에 그대로 머문다 — 다음 OnWorkingTick 재시도가 새로 도착한 로봇을 찾는다.
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

	// 유휴 전환 즉시 다음 작업을 스스로 이어받는다(Pull 방식 재배정).
	if (Dispatch)
	{
		FStationAssignment NewAssignment;
		Dispatch->TryAssignIdleAtlas(this, NewAssignment);
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
