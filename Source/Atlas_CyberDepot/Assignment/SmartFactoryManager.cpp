// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assignment/SmartFactoryManager.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/FactoryNPCHuman.h"
#include "Infrastructure/IdleWaitingZone.h"
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
		// 빙의 여부/FullRepair 진행 중 여부는 URepairProgressComponent가 생기는 7단계에서 정교화한다.
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

	AFactoryNPCHuman* NPC = FindNearestAvailableNPC(Agent->GetActorLocation());
	if (!NPC)
	{
		return;
	}

	// 로봇에 AI NPC가 이미 배정돼 있는지는 URepairProgressComponent가 생기는 7단계에서 확인한다.
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
