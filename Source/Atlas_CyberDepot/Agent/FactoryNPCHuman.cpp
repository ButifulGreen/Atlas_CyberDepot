// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryNPCHuman.h"
#include "Atlas_CyberDepot.h"
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

	// 버그 수정(사용자 지시) — 사무실에서 랜덤 대기 중(OfficeWaitTimerHandle)에 새 정비가 배정되면,
	// 대기 종료 후 StartPatrol()이 뒤늦게 불려 지금 진행 중인 정비를 무시하고 순찰로 튀어버릴 수 있다.
	GetWorldTimerManager().ClearTimer(OfficeWaitTimerHandle);

	AssignedMaintenanceTarget = Target;
	SetState(EAgentState::UnderRepair);

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Repair] %s: %s를 향해 이동 시작(RepairType=%s)"), *GetName(), *Target->GetName(),
		RepairType == ERepairType::FullRepair ? TEXT("FullRepair") : TEXT("QuickCheck"));

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

void AFactoryNPCHuman::OnArrivedAtDestination()
{
	// 버그 수정(사용자 지시) — 기본 구현(AFactoryAgentBase::OnArrivedAtDestination, 빈 함수)이라 사무실
	// 도착이 감지되지 않아 CurrentState/PatrolState가 Moving/ReturningToOffice에 영구히 눌러붙었다.
	// 순찰 중 도착(PatrolState==Patrolling)이나 정비 대상 도착(AssignMaintenance가 이동 시작 시점에
	// 이미 UnderRepair로 전환해둠)은 여기서 다룰 대상이 아니다 — 사무실 복귀 도착만 처리한다.
	if (PatrolState != EPatrolState::ReturningToOffice)
	{
		return;
	}

	PatrolState = EPatrolState::InOffice;
	SetState(EAgentState::Idle);

	const int32 WaitSeconds = FMath::RandRange(0, MaxOfficeWaitSeconds);
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Patrol] %s 사무실 도착 — %d초 대기 후 순찰 재개"), *GetName(), WaitSeconds);

	if (WaitSeconds <= 0)
	{
		StartPatrol();
		return;
	}

	GetWorldTimerManager().SetTimer(OfficeWaitTimerHandle, this, &AFactoryNPCHuman::StartPatrol, static_cast<float>(WaitSeconds), false);
}

bool AFactoryNPCHuman::TryPossessByPlayer(APlayerController* RequestingController)
{
	if (!RequestingController || RequestingController->GetPawn() == this)
	{
		return false;
	}

	RequestingController->Possess(this);
	return true;
}

void AFactoryNPCHuman::CallToOfficeExit()
{
	PatrolState = EPatrolState::InOffice;
	StartPatrol();
}

bool AFactoryNPCHuman::CanBePossessedBy(APlayerController* RequestingController) const
{
	const APlayerController* CurrentPlayerController = Cast<APlayerController>(GetController());
	return !CurrentPlayerController || CurrentPlayerController == RequestingController;
}

void AFactoryNPCHuman::ReleasePossession()
{
	// AutoPossessAI/AIControllerClass 설정에 따라 AI 제어를 되찾는다(레벨 배치 시 설정 필요 — 8단계 범위 밖).
	SpawnDefaultController();
}
