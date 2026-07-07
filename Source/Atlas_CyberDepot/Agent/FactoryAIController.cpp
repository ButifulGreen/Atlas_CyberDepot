// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryAIController.h"
#include "Atlas_CyberDepot.h"
#include "Agent/FactoryAgentBase.h"
#include "AITypes.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Navigation/CostZoneVolume.h"
#include "Navigation/FactoryNavAreas.h"

namespace
{
	UCrowdFollowingComponent* FindCrowdFollowingComponent(const APawn* Pawn)
	{
		const AAIController* PawnController = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
		return PawnController ? Cast<UCrowdFollowingComponent>(PawnController->GetPathFollowingComponent()) : nullptr;
	}

	// Filter->Areas에서 AreaClass 항목을 찾아 갱신하거나, 없으면 새로 추가한다.
	void ApplyAreaCostOverride(UNavigationQueryFilter& Filter, TSubclassOf<UNavArea> AreaClass, float Cost)
	{
		for (FNavigationFilterArea& AreaOverride : Filter.Areas)
		{
			if (AreaOverride.AreaClass == AreaClass)
			{
				AreaOverride.bOverrideTravelCost = true;
				AreaOverride.TravelCostOverride = Cost;
				return;
			}
		}

		FNavigationFilterArea NewOverride;
		NewOverride.AreaClass = AreaClass;
		NewOverride.bOverrideTravelCost = true;
		NewOverride.TravelCostOverride = Cost;
		Filter.Areas.Add(NewOverride);
	}
}

void AFactoryAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);

	if (!Result.IsSuccess())
	{
		// 진단용 — 이동 실패는 아무 데도 알려지지 않아 에이전트가 Moving 상태에 영구히 멈춰있게 된다.
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] 이동 실패(Code=%d) — CurrentState가 Moving에 멈춘 채로 남을 수 있음"),
			GetPawn() ? *GetPawn()->GetName() : TEXT("Unknown"), static_cast<int32>(Result.Code.GetValue()));
		return;
	}

	if (AFactoryAgentBase* Agent = Cast<AFactoryAgentBase>(GetPawn()))
	{
		Agent->OnArrivedAtDestination();
	}
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
	const APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!Filter || !MyPawn || !World)
	{
		return;
	}

	// QueryFilterClass는 bInstantiateForQuerier=true이므로 CDO(Areas)를 수정해도
	// 이동 요청 직후 동기적으로 InitializeFilter가 다시 읽어가 매 요청마다 새로 반영된다
	// (다른 에이전트와 뒤섞이지 않음 — RequestMoveWithFilter 안에서 수정→MoveTo가 같은 스택에서 동기 처리됨).
	const FVector MyLocation = MyPawn->GetActorLocation();
	const float CurrentTime = World->GetTimeSeconds();

	TArray<AActor*> FoundZones;
	UGameplayStatics::GetAllActorsOfClass(World, ACostZoneVolume::StaticClass(), FoundZones);

	for (AActor* ZoneActor : FoundZones)
	{
		ACostZoneVolume* Zone = Cast<ACostZoneVolume>(ZoneActor);
		if (!Zone || !Zone->AffectedAreaClass)
		{
			continue;
		}

		if (FVector::DistSquared(Zone->GetActorLocation(), MyLocation) > FMath::Square(CongestionSenseRadius))
		{
			continue;
		}

		Zone->TickPendingReset(CurrentTime);

		const float Multiplier = Zone->GetCurrentCostMultiplier();
		if (Multiplier <= 1.f)
		{
			continue;
		}

		const UNavArea* AreaCDO = Zone->AffectedAreaClass.GetDefaultObject();
		ApplyAreaCostOverride(*Filter, Zone->AffectedAreaClass, AreaCDO->DefaultCost * Multiplier);
	}
}

void AFactoryAIController::SetAvoidanceIgnoreActor(AActor* TargetActor, bool bIgnore)
{
	UCrowdFollowingComponent* MyCrowd = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent());
	UCrowdFollowingComponent* TargetCrowd = FindCrowdFollowingComponent(Cast<APawn>(TargetActor));
	if (!MyCrowd || !TargetCrowd)
	{
		return;
	}

	// 버그 수정 — 상호 무시가 되려면 양쪽 다 "GroupsToAvoid에서 빼고 AvoidanceGroup엔 더한다"를
	// 동일하게 적용해야 한다. 기존엔 My->GroupsToAvoid와 Target->AvoidanceGroup만 건드려
	// My가 Target을 피하지 않게만 됐을 뿐, Target은 여전히 My를 피하려 했다.
	MyCrowd->SetGroupsToAvoid(bIgnore
		? (MyCrowd->GetGroupsToAvoid() & ~MaintenanceIgnoreAvoidanceGroup)
		: (MyCrowd->GetGroupsToAvoid() | MaintenanceIgnoreAvoidanceGroup));
	MyCrowd->SetAvoidanceGroup(bIgnore
		? (MyCrowd->GetAvoidanceGroup() | MaintenanceIgnoreAvoidanceGroup)
		: (MyCrowd->GetAvoidanceGroup() & ~MaintenanceIgnoreAvoidanceGroup));

	TargetCrowd->SetGroupsToAvoid(bIgnore
		? (TargetCrowd->GetGroupsToAvoid() & ~MaintenanceIgnoreAvoidanceGroup)
		: (TargetCrowd->GetGroupsToAvoid() | MaintenanceIgnoreAvoidanceGroup));
	TargetCrowd->SetAvoidanceGroup(bIgnore
		? (TargetCrowd->GetAvoidanceGroup() | MaintenanceIgnoreAvoidanceGroup)
		: (TargetCrowd->GetAvoidanceGroup() & ~MaintenanceIgnoreAvoidanceGroup));
}
