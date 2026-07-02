// Copyright Epic Games, Inc. All Rights Reserved.

#include "Infrastructure/IdleWaitingZone.h"
#include "Agent/FactoryAgentBase.h"
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
	// AFactoryAgentBase에는 OperationCount/MaintenanceThreshold가 없다(아틀라스/운송로봇 서브클래스 전용,
	// Docs/04_Agent_AI.md 5단계). 그 서브클래스가 생기면 여기서 실제 판정을 채운다.
	return nullptr;
}

void AIdleWaitingZone::OnRestDecayInterval()
{
	// OperationCount 감쇠도 서브클래스 전용 필드라 5단계에서 채운다.
}

bool AIdleWaitingZone::ShouldDispatchNPCForMaintenance() const
{
	// IsMaintenanceDue()가 서브클래스 전용이라 지금은 항상 false — 5단계에서 실제 조건으로 교체한다.
	return false;
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

	// NPC->AssignMaintenance(FirstTarget, ERepairType::QuickCheck) 호출은
	// AFactoryNPCHuman이 구현되는 5단계에서 연결한다.
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
	// 개별 로봇의 IsMaintenanceDue()==false 전환 감지는 서브클래스 전용이라 5단계에서 채운다.
	if (BatchMaintenanceTargetSet.Num() == 0 && MaintenanceState == EZoneMaintenanceState::Active)
	{
		EndBatchMaintenance();
	}
}

void AIdleWaitingZone::EndBatchMaintenance()
{
	// NPC 복귀 지시 호출은 AFactoryNPCHuman이 구현되는 5단계에서 연결한다.
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
