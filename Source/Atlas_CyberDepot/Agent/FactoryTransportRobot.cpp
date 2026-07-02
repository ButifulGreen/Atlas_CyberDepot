// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryTransportRobot.h"
#include "Agent/FactoryAIController.h"
#include "Infrastructure/LogisticsItem.h"
#include "Infrastructure/StorageShelf.h"
#include "Infrastructure/HorizontalTray.h"
#include "Infrastructure/IdleWaitingZone.h"
#include "Navigation/CostZoneVolume.h"
#include "Assignment/OutboundDispatchSubsystem.h"
#include "Assignment/SmartFactoryManager.h"
#include "Repair/RepairProgressComponent.h"
#include "EventBus/FactoryEventBusSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

AFactoryTransportRobot::AFactoryTransportRobot()
{
	AgentType = EActorType::TransportRobot;
	RepairComponent = CreateDefaultSubobject<URepairProgressComponent>(TEXT("RepairComponent"));
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

	const int32 OverageUnits = (OperationCount - MaintenanceThreshold) / 5;
	const float Chance = BreakdownChanceBase + static_cast<float>(OverageUnits) * BreakdownChanceOverageMultiplier;
	return FMath::Min(Chance, MaxBreakdownChanceCap);
}

void AFactoryTransportRobot::AcceptTransportTask(const FTransportTask& Task)
{
	CurrentTask = Task;

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

void AFactoryTransportRobot::OnItemPickedUp()
{
	AActor* PickupActor = CurrentTask.PickupPoint.Get();
	ALogisticsItem* Item = nullptr;

	if (AStorageShelf* Shelf = Cast<AStorageShelf>(PickupActor))
	{
		int32 FloorIndex = 0;
		int32 SlotIndex = 0;
		if (Shelf->TryReserveOldestOccupiedSlot(FloorIndex, SlotIndex, Item) && Item)
		{
			Shelf->ConfirmOutboundRemoved(FloorIndex, SlotIndex);
		}
	}
	else if (AHorizontalTray* Tray = Cast<AHorizontalTray>(PickupActor))
	{
		Item = Tray->CurrentItem.Get();
		if (Item)
		{
			Tray->OnItemCleared();
		}
	}

	PayloadItem = Item;
	if (Item && GetMesh())
	{
		Item->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, PayloadItemSocketName);
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
}

void AFactoryTransportRobot::EvaluateRotationOrContinue()
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

	if (!IsMaintenanceDue() || PayloadItem)
	{
		return;
	}

	TArray<AActor*> FoundZones;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AIdleWaitingZone::StaticClass(), FoundZones);

	for (AActor* ZoneActor : FoundZones)
	{
		AIdleWaitingZone* Zone = Cast<AIdleWaitingZone>(ZoneActor);
		if (!Zone || Zone->AllowedAgentType != EActorType::TransportRobot || !Zone->FindRestedOccupant())
		{
			continue;
		}

		FTransform SlotTransform;
		if (Zone->TryReserveSlot(this, SlotTransform))
		{
			if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
			{
				AIController->RequestMoveWithFilter(SlotTransform.GetLocation());
			}
			SetState(EAgentState::Moving);
			return;
		}
	}

	// 교대할 자리를 못 찾으면 TryAssignIdleTransportRobot 경유로 다음 트립을 계속 수령한다(호출 측 책임).
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
			}
		}
	}
}

void AFactoryTransportRobot::OnTaskCompleted()
{
	if (AActor* DropoffActor = CurrentTask.DropoffPoint.Get())
	{
		if (AStorageShelf* Shelf = Cast<AStorageShelf>(DropoffActor))
		{
			int32 FloorIndex = 0;
			int32 SlotIndex = 0;
			if (PayloadItem && Shelf->TryReserveEmptySlot(FloorIndex, SlotIndex))
			{
				Shelf->ConfirmInbound(FloorIndex, SlotIndex, PayloadItem);
			}
		}
		else if (AHorizontalTray* Tray = Cast<AHorizontalTray>(DropoffActor))
		{
			if (PayloadItem)
			{
				Tray->OnItemPlacedByAtlas(PayloadItem);
			}
		}
	}

	if (PayloadItem)
	{
		PayloadItem->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		PayloadItem = nullptr;
	}

	++OperationCount;

	const FGuid CompletedTaskID = CurrentTask.TaskID;
	if (UOutboundDispatchSubsystem* Dispatch = GetWorld()->GetSubsystem<UOutboundDispatchSubsystem>())
	{
		Dispatch->OnTransportTaskCompleted(CompletedTaskID, this);
	}

	CurrentTask = FTransportTask();

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
