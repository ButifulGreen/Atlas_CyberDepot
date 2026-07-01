// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryAIController.h"
#include "AITypes.h"
#include "GameFramework/Pawn.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "NavFilters/NavigationQueryFilter.h"

namespace
{
	UCrowdFollowingComponent* FindCrowdFollowingComponent(const APawn* Pawn)
	{
		const AAIController* PawnController = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
		return PawnController ? Cast<UCrowdFollowingComponent>(PawnController->GetPathFollowingComponent()) : nullptr;
	}
}

void AFactoryAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);
}

void AFactoryAIController::RequestMoveWithFilter(const FVector& Destination)
{
	if (QueryFilterClass)
	{
		ApplyDynamicCongestionCost(QueryFilterClass.GetDefaultObject());
	}

	FAIMoveRequest MoveRequest(Destination);
	MoveRequest.SetNavigationFilter(QueryFilterClass);
	MoveTo(MoveRequest);
}

void AFactoryAIController::ApplyDynamicCongestionCost(UNavigationQueryFilter* Filter)
{
	// ACostZoneVolume(Docs/08_Navigation.md, 3단계)이 아직 없어 실제 코스트 반영은 3단계에서 채운다.
}

void AFactoryAIController::SetAvoidanceIgnoreActor(AActor* TargetActor, bool bIgnore)
{
	UCrowdFollowingComponent* MyCrowd = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent());
	UCrowdFollowingComponent* TargetCrowd = FindCrowdFollowingComponent(Cast<APawn>(TargetActor));
	if (!MyCrowd || !TargetCrowd)
	{
		return;
	}

	MyCrowd->SetGroupsToAvoid(bIgnore
		? (MyCrowd->GetGroupsToAvoid() & ~MaintenanceIgnoreAvoidanceGroup)
		: (MyCrowd->GetGroupsToAvoid() | MaintenanceIgnoreAvoidanceGroup));

	TargetCrowd->SetAvoidanceGroup(bIgnore
		? (TargetCrowd->GetAvoidanceGroup() | MaintenanceIgnoreAvoidanceGroup)
		: (TargetCrowd->GetAvoidanceGroup() & ~MaintenanceIgnoreAvoidanceGroup));
}
