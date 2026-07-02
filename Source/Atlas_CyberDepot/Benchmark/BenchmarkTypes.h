// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BenchmarkTypes.generated.h"

// Docs/10_Benchmark_Replay.md §10 — 마지막 단계.
USTRUCT(BlueprintType)
struct FPerfSample
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	double Timestamp = 0.0;

	UPROPERTY(BlueprintReadOnly)
	float GameThreadTickTimeMs = 0.f;

	// Docs에 없는 구현값: 언리얼 내비게이션 시스템은 GGameThreadTime류의 공개 전역 카운터가 없어
	// 라이브 계측이 불가능하다. 항상 0으로 남고, 실측은 여전히 `stat navigation`에 의존한다(Docs/14_OpenIssues.md).
	UPROPERTY(BlueprintReadOnly)
	float NavigationTickTimeMs = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float GPUFrameTimeMs = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float MemoryUsageMB = 0.f;

	UPROPERTY(BlueprintReadOnly)
	int32 ActiveAgentCount = 0;
};

USTRUCT(BlueprintType)
struct FScalingReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 BaselineAgentCount = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 ScaledAgentCount = 0;

	UPROPERTY(BlueprintReadOnly)
	float TickTimeIncreasePercent = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float MemoryIncreasePercent = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float GPUTimeIncreasePercent = 0.f;

	UPROPERTY(BlueprintReadOnly)
	bool bNonLinearBottleneckDetected = false;
};

// 정성적 데모 전용(Docs) — ForcedDeadlockCount만큼 활성 에이전트를 골라 Code:001(교착상태) 이상징후를
// DurationSeconds 동안 발행해 회복력을 시연한다. 실제 내비게이션 경합을 강제로 유도하는 것은 아니다.
USTRUCT(BlueprintType)
struct FStressTestParams
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 ForcedDeadlockCount = 0;

	UPROPERTY(BlueprintReadOnly)
	float DurationSeconds = 0.f;
};
