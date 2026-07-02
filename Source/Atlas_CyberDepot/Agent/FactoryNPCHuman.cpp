// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryNPCHuman.h"
#include "Agent/FactoryAIController.h"
#include "Repair/RepairProgressComponent.h"
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
	SetState(EAgentState::UnderRepair);

	if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
	{
		AIController->SetAvoidanceIgnoreActor(Target, true);
		AIController->RequestMoveWithFilter(Target->GetActorLocation());
	}

	// 정확한 도착 판정(이동 완료 콜백)은 아직 없어, 배정 시점에 바로 참여시키는 것으로 단순화했다.
	if (URepairProgressComponent* RepairComponent = Target->GetRepairComponent())
	{
		RepairComponent->CurrentRepairType = RepairType;
		RepairComponent->Server_JoinRepair(this);
	}
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

		if (URepairProgressComponent* RepairComponent = AssignedMaintenanceTarget->GetRepairComponent())
		{
			RepairComponent->Server_LeaveRepair(this);
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
