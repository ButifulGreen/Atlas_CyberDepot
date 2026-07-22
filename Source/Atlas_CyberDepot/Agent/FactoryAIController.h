// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DetourCrowdAIController.h"
#include "FactoryAIController.generated.h"

class UNavigationQueryFilter;

// Docs/04_Agent_AI.md §4, Docs/08_Navigation.md §8 — 2단계(스켈레톤) 대상.
UCLASS()
class AFactoryAIController : public ADetourCrowdAIController
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UNavigationQueryFilter> QueryFilterClass;

	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

	void RequestMoveWithFilter(const FVector& Destination);

	// Crowd Avoidance에서 TargetActor를 상호 무시(bIgnore=true) 또는 재고려 대상으로 토글
	void SetAvoidanceIgnoreActor(AActor* TargetActor, bool bIgnore);

	// 버그 수정 — 이동 실패 시 아무 복구가 없어 에이전트가 Moving에 영구히 멈추는 문제가 있었다.
	// Blocked 등 진짜 길찾기 실패에 한해 같은 목적지로 자동 재시도한다(Docs에 없는 구현값).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Movement")
	int32 MaxMoveRetryAttempts = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Movement")
	float MoveRetryDelaySeconds = 1.f;

private:
	// Crowd 회피 그룹 비트 중 정비 접근 시 상호 무시 용도로 예약한 비트
	static constexpr int32 MaintenanceIgnoreAvoidanceGroup = 1 << 7;

	void RetryLastMove();

	FVector LastRequestedDestination = FVector::ZeroVector;
	int32 MoveFailureRetryCount = 0;
	FTimerHandle MoveRetryTimerHandle;
};
