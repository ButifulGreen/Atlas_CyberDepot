// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assignment/SmartFactoryManager.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/FactoryNPCHuman.h"
#include "Infrastructure/IdleWaitingZone.h"
#include "Repair/RepairProgressComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

void AMSmartFactoryManager::AdjustReputation(float Delta, FName Reason)
{
	ReputationScore += Delta;
}

AFactoryNPCHuman* AMSmartFactoryManager::FindNearestAvailableNPC(const FVector& Location) const
{
	TArray<AActor*> FoundNPCs;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFactoryNPCHuman::StaticClass(), FoundNPCs);

	AFactoryNPCHuman* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	for (AActor* Actor : FoundNPCs)
	{
		AFactoryNPCHuman* NPC = Cast<AFactoryNPCHuman>(Actor);
		// 이미 정비 중인 NPC는 제외한다. 플레이어 빙의 여부까지 구분하는 건 8단계 멀티플레이어에서 정교화한다.
		if (!NPC || NPC->CurrentState == EAgentState::UnderRepair)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(NPC->GetActorLocation(), Location);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = NPC;
		}
	}

	return Best;
}

void AMSmartFactoryManager::RequestMaintenance(AFactoryAgentBase* Agent, ERepairType RepairType)
{
	if (!Agent)
	{
		return;
	}

	// 이미 참여 중인 정비자(AI든 빙의 플레이어든)가 있으면 추가 AI NPC를 배정하지 않는다.
	if (URepairProgressComponent* ExistingRepair = Agent->GetRepairComponent())
	{
		if (ExistingRepair->GetValidRepairerCount() > 0)
		{
			return;
		}
	}

	AFactoryNPCHuman* NPC = FindNearestAvailableNPC(Agent->GetActorLocation());
	if (!NPC)
	{
		return;
	}

	NPC->AssignMaintenance(Agent, RepairType);
}

void AMSmartFactoryManager::OnAgentBecameIdle(AFactoryAgentBase* Agent)
{
	if (Agent && Agent->IsMaintenanceDue())
	{
		RequestMaintenance(Agent, ERepairType::QuickCheck);
	}
}

void AMSmartFactoryManager::OnRepairCompleted(AFactoryAgentBase* Agent)
{
	TArray<AActor*> FoundZones;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AIdleWaitingZone::StaticClass(), FoundZones);

	for (AActor* ZoneActor : FoundZones)
	{
		AIdleWaitingZone* Zone = Cast<AIdleWaitingZone>(ZoneActor);
		if (!Zone || !Zone->ShouldDispatchNPCForMaintenance())
		{
			continue;
		}

		if (AFactoryNPCHuman* NPC = FindNearestAvailableNPC(Zone->GetActorLocation()))
		{
			Zone->BeginBatchMaintenance(NPC);
		}
	}
}

void AMSmartFactoryManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMSmartFactoryManager, ReputationScore);
}
