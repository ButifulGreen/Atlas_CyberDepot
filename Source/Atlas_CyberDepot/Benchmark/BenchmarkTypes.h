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
