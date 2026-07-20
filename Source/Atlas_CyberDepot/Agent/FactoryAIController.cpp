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

	if (Result.IsSuccess())
	{
		MoveFailureRetryCount = 0;
		if (AFactoryAgentBase* Agent = Cast<AFactoryAgentBase>(GetPawn()))
		{
			Agent->OnArrivedAtDestination();
		}
		return;
	}

	// 버그 수정 — 이동 실패는 예전엔 로그만 남기고 아무 조치가 없어 에이전트가 Moving에 영구히 멈췄다.
	// Aborted는 보통 더 최신 RequestMoveWithFilter 호출(예: 정비 재배정)이 이 요청을 대체해서 생기는
	// 정상적인 상황이다 — 그 최신 요청이 알아서 자기 결과를 다시 보고하므로 여기서 재시도하면
	// 서로 충돌만 한다. 재시도는 진짜 길찾기 실패(Blocked/OffPath 등)에만 적용한다.
	if (Result.Code == EPathFollowingResult::Aborted)
	{
		return;
	}

	if (MoveFailureRetryCount >= MaxMoveRetryAttempts)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] 이동 실패(Code=%d) — 재시도 %d회 모두 실패"),
			GetPawn() ? *GetPawn()->GetName() : TEXT("Unknown"), static_cast<int32>(Result.Code.GetValue()), MaxMoveRetryAttempts);

		// 버그 수정 — 재큐잉된 배정을 같은 에이전트가 바로 다시 집어 같은 목적지로 재시도할 때, 여기서
		// 카운트를 리셋하지 않으면 RequestMoveWithFilter가 "이전과 같은 목적지"로 보고 카운트를 그대로
		// 최대치에 둔 채 시작한다. 그 상태로 MoveTo가 동기적으로 즉시 실패를 보고하면 재시도 유예 없이
		// 곧장 다시 OnMoveFailedPermanently를 호출해, 같은 콜스택 안에서 재큐잉→재배정→즉시재실패가
		// 반복되며 스택 오버플로우로 이어졌다(실제 재현됨). 다음 시도는 항상 정상적으로 재시도 유예
		// (MaxMoveRetryAttempts회, MoveRetryDelaySeconds 간격)를 다시 거치도록 리셋한다.
		MoveFailureRetryCount = 0;

		// 버그 수정 — 예전엔 여기서 그냥 포기해 CurrentState가 Moving에 영구히 멈췄다(배정/트립을 붙든 채
		// 함대에서 영구 이탈). 에이전트가 자기 배정을 정리하고 Idle로 복귀하도록 위임한다.
		if (AFactoryAgentBase* Agent = Cast<AFactoryAgentBase>(GetPawn()))
		{
			Agent->OnMoveFailedPermanently();
		}
		return;
	}

	++MoveFailureRetryCount;
	UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] 이동 실패(Code=%d) — %.1f초 후 재시도(%d/%d)"),
		GetPawn() ? *GetPawn()->GetName() : TEXT("Unknown"), static_cast<int32>(Result.Code.GetValue()),
		MoveRetryDelaySeconds, MoveFailureRetryCount, MaxMoveRetryAttempts);
	GetWorldTimerManager().SetTimer(MoveRetryTimerHandle, this, &AFactoryAIController::RetryLastMove, MoveRetryDelaySeconds, false);
}

void AFactoryAIController::RetryLastMove()
{
	RequestMoveWithFilter(LastRequestedDestination);
}

void AFactoryAIController::RequestMoveWithFilter(const FVector& Destination)
{
	// 새 목적지면 재시도 카운트를 리셋한다 — RetryLastMove가 같은 목적지로 다시 부를 때는
	// (LastRequestedDestination과 동일) 리셋하지 않아 OnMoveCompleted가 늘린 카운트가 유지된다.
	if (!Destination.Equals(LastRequestedDestination))
	{
		MoveFailureRetryCount = 0;
	}
	LastRequestedDestination = Destination;

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
