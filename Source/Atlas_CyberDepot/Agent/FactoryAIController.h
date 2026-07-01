// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DetourCrowdAIController.h"
#include "FactoryAIController.generated.h"

class UNavigationQueryFilter;

// Docs/04_Agent_AI.md §4, Docs/08_Navigation.md §8 — 2단계(스켈레톤) 대상.
// ApplyDynamicCongestionCost의 실제 코스트 반영 로직은 ACostZoneVolume이 생기는 3단계에서 채운다.
UCLASS()
class AFactoryAIController : public ADetourCrowdAIController
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UNavigationQueryFilter> QueryFilterClass;

	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

	void RequestMoveWithFilter(const FVector& Destination);
	void ApplyDynamicCongestionCost(UNavigationQueryFilter* Filter);

	// Crowd Avoidance에서 TargetActor를 상호 무시(bIgnore=true) 또는 재고려 대상으로 토글
	void SetAvoidanceIgnoreActor(AActor* TargetActor, bool bIgnore);

private:
	// Crowd 회피 그룹 비트 중 정비 접근 시 상호 무시 용도로 예약한 비트
	static constexpr int32 MaintenanceIgnoreAvoidanceGroup = 1 << 7;
};
