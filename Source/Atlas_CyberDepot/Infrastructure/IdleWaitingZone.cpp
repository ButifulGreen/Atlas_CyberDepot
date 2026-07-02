// Copyright Epic Games, Inc. All Rights Reserved.

#include "Infrastructure/IdleWaitingZone.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/FactoryNPCHuman.h"
#include "Assignment/SmartFactoryManager.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AIdleWaitingZone::AIdleWaitingZone()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
}

void AIdleWaitingZone::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(RestDecayTimerHandle, this, &AIdleWaitingZone::OnRestDecayInterval, RestDecayIntervalSeconds, true);
	}
}

bool AIdleWaitingZone::TryReserveSlot(AFactoryAgentBase* Agent, FTransform& OutSlotTransform)
{
	if (!Agent)
	{
		return false;
	}

	for (int32 SlotIndex = 0; SlotIndex < ParkingSlots.Num(); ++SlotIndex)
	{
		const TWeakObjectPtr<AFactoryAgentBase>* Occupant = SlotOccupancy.Find(SlotIndex);
		if (!Occupant || !Occupant->IsValid())
		{
			SlotOccupancy.Add(SlotIndex, Agent);
			OutSlotTransform = ParkingSlots[SlotIndex];
			Agent->bIsParkedInIdleZone = true;

			if (AMSmartFactoryManager* Manager = GetWorld() ? GetWorld()->GetGameState<AMSmartFactoryManager>() : nullptr)
			{
				Manager->OnAgentBecameIdle(Agent);
			}

			return true;
		}
	}

	return false;
}

void AIdleWaitingZone::ReleaseSlot(AFactoryAgentBase* Agent)
{
	if (!Agent)
	{
		return;
	}

	for (auto It = SlotOccupancy.CreateIterator(); It; ++It)
	{
		if (It.Value().Get() == Agent)
		{
			It.RemoveCurrent();
			break;
		}
	}

	Agent->bIsParkedInIdleZone = false;
	OnBatchTargetLeft(Agent);
}

bool AIdleWaitingZone::IsAgentParked(const AFactoryAgentBase* Agent) const
{
	for (const auto& Pair : SlotOccupancy)
	{
		if (Pair.Value.Get() == Agent)
		{
			return true;
		}
	}

	return false;
}

AFactoryAgentBase* AIdleWaitingZone::FindRestedOccupant() const
{
	for (const auto& Pair : SlotOccupancy)
	{
		AFactoryAgentBase* Agent = Pair.Value.Get();
		if (Agent && Agent->GetOperationRatio() <= FullyRestedThresholdRatio)
		{
			return Agent;
		}
	}

	return nullptr;
}

void AIdleWaitingZone::OnRestDecayInterval()
{
	for (const auto& Pair : SlotOccupancy)
	{
		if (AFactoryAgentBase* Agent = Pair.Value.Get())
		{
			Agent->ApplyRestDecay(RestDecayAmountPerInterval);
		}
	}

	OnBatchMaintenanceProgress();

	if (ShouldDispatchNPCForMaintenance())
	{
		if (AMSmartFactoryManager* Manager = GetWorld() ? GetWorld()->GetGameState<AMSmartFactoryManager>() : nullptr)
		{
			if (AFactoryNPCHuman* NPC = Manager->FindNearestAvailableNPC(GetActorLocation()))
			{
				BeginBatchMaintenance(NPC);
			}
		}
	}
}

bool AIdleWaitingZone::ShouldDispatchNPCForMaintenance() const
{
	if (SlotOccupancy.Num() == 0 || MaintenanceState != EZoneMaintenanceState::Idle)
	{
		return false;
	}

	for (const auto& Pair : SlotOccupancy)
	{
		const AFactoryAgentBase* Agent = Pair.Value.Get();
		if (!Agent || !Agent->IsMaintenanceDue())
		{
			return false;
		}
	}

	return true;
}

void AIdleWaitingZone::BeginBatchMaintenance(AFactoryNPCHuman* NPC)
{
	BatchMaintenanceTargetSet.Reset();
	for (const auto& Pair : SlotOccupancy)
	{
		if (Pair.Value.IsValid())
		{
			BatchMaintenanceTargetSet.Add(Pair.Value);
		}
	}

	MaintenanceState = EZoneMaintenanceState::Active;
	BatchMaintenanceNPC = NPC;

	if (NPC && BatchMaintenanceTargetSet.Num() > 0)
	{
		if (AFactoryAgentBase* FirstTarget = BatchMaintenanceTargetSet[0].Get())
		{
			NPC->AssignMaintenance(FirstTarget, ERepairType::QuickCheck);
		}
	}
}

void AIdleWaitingZone::OnBatchTargetLeft(AFactoryAgentBase* Agent)
{
	if (!Agent || BatchMaintenanceTargetSet.Num() == 0)
	{
		return;
	}

	BatchMaintenanceTargetSet.RemoveAll([Agent](const TWeakObjectPtr<AFactoryAgentBase>& Entry)
	{
		return Entry.Get() == Agent;
	});

	if (BatchMaintenanceTargetSet.Num() == 0 && MaintenanceState == EZoneMaintenanceState::Active)
	{
		EndBatchMaintenance();
	}
}

void AIdleWaitingZone::OnBatchMaintenanceProgress()
{
	BatchMaintenanceTargetSet.RemoveAll([](const TWeakObjectPtr<AFactoryAgentBase>& Entry)
	{
		const AFactoryAgentBase* Agent = Entry.Get();
		return !Agent || !Agent->IsMaintenanceDue();
	});

	if (BatchMaintenanceTargetSet.Num() == 0 && MaintenanceState == EZoneMaintenanceState::Active)
	{
		EndBatchMaintenance();
	}
}

void AIdleWaitingZone::EndBatchMaintenance()
{
	if (AFactoryNPCHuman* NPC = BatchMaintenanceNPC.Get())
	{
		NPC->ReturnToOfficeRoom();
	}

	BatchMaintenanceTargetSet.Reset();
	MaintenanceState = EZoneMaintenanceState::Idle;
	BatchMaintenanceNPC.Reset();
}

void AIdleWaitingZone::OnBatchMaintenanceInterrupted()
{
	BatchMaintenanceTargetSet.Reset();
	MaintenanceState = EZoneMaintenanceState::Idle;
	BatchMaintenanceNPC.Reset();
}

void AIdleWaitingZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AIdleWaitingZone, MaintenanceState);
}
