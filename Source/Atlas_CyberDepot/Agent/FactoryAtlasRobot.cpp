// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryAtlasRobot.h"
#include "Infrastructure/LogisticsItem.h"
#include "Infrastructure/StorageShelf.h"
#include "Infrastructure/HorizontalTray.h"
#include "EventBus/FactoryEventBusSubsystem.h"
#include "Net/UnrealNetwork.h"

AFactoryAtlasRobot::AFactoryAtlasRobot()
{
	AgentType = EActorType::AtlasRobot;
}

bool AFactoryAtlasRobot::IsMaintenanceDue() const
{
	return OperationCount >= MaintenanceThreshold;
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
		return;
	}

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
		return;
	}

	if (IsMaintenanceDue())
	{
		// 대기실에 초기화된 로봇이 있으면 UOutboundDispatchSubsystem::HandoffStationAssignment로 교대한다.
		// UOutboundDispatchSubsystem(Docs/07_TaskAssignment.md, 6단계)이 아직 없어 이 분기는 6단계에서 연결하고,
		// 지금은 교대 불가로 간주해 계속 진행한다.
	}
}

void AFactoryAtlasRobot::TransferItem(AActor* Source, AActor* Destination)
{
	if (AStorageShelf* SourceShelf = Cast<AStorageShelf>(Source))
	{
		int32 FloorIndex = 0;
		int32 SlotIndex = 0;
		ALogisticsItem* Item = nullptr;
		if (SourceShelf->TryReserveOldestOccupiedSlot(FloorIndex, SlotIndex, Item))
		{
			PendingSlotReservation.bIsValid = true;
			PendingSlotReservation.FloorIndex = FloorIndex;
			PendingSlotReservation.SlotIndex = SlotIndex;

			AttachHeldItem(Item);
			SourceShelf->ConfirmOutboundRemoved(FloorIndex, SlotIndex);

			PendingSlotReservation = FPendingSlotReservation();
		}
		return;
	}

	if (AStorageShelf* DestShelf = Cast<AStorageShelf>(Destination))
	{
		int32 FloorIndex = 0;
		int32 SlotIndex = 0;
		if (HeldItem && DestShelf->TryReserveEmptySlot(FloorIndex, SlotIndex))
		{
			PendingSlotReservation.bIsValid = true;
			PendingSlotReservation.FloorIndex = FloorIndex;
			PendingSlotReservation.SlotIndex = SlotIndex;

			ALogisticsItem* Item = HeldItem;
			DestShelf->ConfirmInbound(FloorIndex, SlotIndex, Item);
			DetachHeldItem();

			PendingSlotReservation = FPendingSlotReservation();
		}
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
	}
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

	SetState(EAgentState::Idle);
	OnTaskCompleted();

	// UOutboundDispatchSubsystem::OnStationAssignmentCompleted 호출과 그에 따른
	// FTaskLifecycleEvent(Completed) 발행은 6단계에서 연결한다.
	CurrentAssignment = FStationAssignment();
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
		Item->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, HeldItemSocketName);
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
