// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryAtlasRobot.h"
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

	const FGuid CompletedAssignmentID = CurrentAssignment.AssignmentID;

	SetState(EAgentState::Idle);
	OnTaskCompleted();

	if (UOutboundDispatchSubsystem* Dispatch = GetWorld()->GetSubsystem<UOutboundDispatchSubsystem>())
	{
		Dispatch->OnStationAssignmentCompleted(CompletedAssignmentID);
	}

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
