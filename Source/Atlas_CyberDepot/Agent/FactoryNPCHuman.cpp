// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryNPCHuman.h"
#include "Agent/FactoryAIController.h"
#include "NavigationSystem.h"
#include "GameFramework/PlayerController.h"

AFactoryNPCHuman::AFactoryNPCHuman()
{
	AgentType = EActorType::NPCHuman;
}

void AFactoryNPCHuman::StartPatrol()
{
	PatrolState = EPatrolState::Patrolling;
	PatrolStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	SetState(EAgentState::Patrolling);

	AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController());
	UNavigationSystemV1* NavSys = GetWorld() ? UNavigationSystemV1::GetCurrent(GetWorld()) : nullptr;
	if (!AIController || !NavSys)
	{
		return;
	}

	FNavLocation RandomLocation;
	if (NavSys->GetRandomReachablePointInRadius(GetActorLocation(), PatrolRadius, RandomLocation))
	{
		AIController->RequestMoveWithFilter(RandomLocation.Location);
	}
}

void AFactoryNPCHuman::AssignMaintenance(AFactoryAgentBase* Target, ERepairType RepairType)
{
	if (!Target)
	{
		return;
	}

	AssignedMaintenanceTarget = Target;

	if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
	{
		AIController->SetAvoidanceIgnoreActor(Target, true);
		AIController->RequestMoveWithFilter(Target->GetActorLocation());
	}

	// 실제 정비 참여(URepairProgressComponent::Server_JoinRepair)는 컴포넌트가 생기는 7단계에서 연결한다.
}

void AFactoryNPCHuman::ReturnToOfficeRoom()
{
	PatrolState = EPatrolState::ReturningToOffice;

	if (AssignedMaintenanceTarget)
	{
		if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
		{
			AIController->SetAvoidanceIgnoreActor(AssignedMaintenanceTarget, false);
		}
		AssignedMaintenanceTarget = nullptr;
	}

	if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
	{
		AIController->RequestMoveWithFilter(OfficeRoomTransform.GetLocation());
	}

	SetState(EAgentState::Moving);
}

bool AFactoryNPCHuman::TryPossessByPlayer(APlayerController* Controller)
{
	if (!Controller || Controller->GetPawn() == this)
	{
		return false;
	}

	Controller->Possess(this);
	return true;
}

void AFactoryNPCHuman::CallToOfficeExit()
{
	PatrolState = EPatrolState::InOffice;
	StartPatrol();
}
