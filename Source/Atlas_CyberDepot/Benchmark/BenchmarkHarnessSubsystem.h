// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Benchmark/BenchmarkTypes.h"
#include "BenchmarkHarnessSubsystem.generated.h"

class AFactoryAgentBase;

enum class EBenchmarkPhase : uint8
{
	Idle,
	RunningBaseline,
	RunningScaled
};

// Docs/10_Benchmark_Replay.md §10 — 마지막 단계. 절대적인 최대 수용 대수가 아니라
// 기준 규모 대비 리소스 사용량의 상대적 증가율을 측정한다.
UCLASS()
class UBenchmarkHarnessSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	TArray<FPerfSample> RecordedSamples;

	UPROPERTY(BlueprintReadOnly)
	FPerfSample BaselineAverage;

	// Docs에 스폰 대상/방식이 명시돼 있지 않아 노출 — 레벨/내비메시가 아직 없어(기획단계) 실기 테스트는 후속 단계.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Benchmark")
	TSubclassOf<AFactoryAgentBase> AgentClassToSpawn;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Benchmark")
	FVector SpawnOrigin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Benchmark")
	float SpawnRadius = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Benchmark")
	float SampleIntervalSeconds = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Benchmark")
	float PhaseDurationSeconds = 30.f;

	// 에이전트 수 증가율 대비 이 배수를 넘는 리소스 증가율을 비정상 병목으로 판정(Docs에 값이 없어 노출)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Benchmark")
	float NonLinearBottleneckToleranceMultiplier = 1.5f;

	void RunScalingComparison(int32 BaselineAgentCount, float ScaleMultiplier);
	void RecordPerfSample();
	FScalingReport ComputeScalingReport() const;
	void ExportPerfReport(const FString& FilePath);
	void StartForcedDeadlockDemo(const FStressTestParams& Params);

private:
	void SpawnBenchmarkAgents(int32 Count);
	void DespawnBenchmarkAgents();
	void BeginPhase(EBenchmarkPhase Phase, int32 AgentCount);
	void OnPhaseComplete();
	void OnDeadlockDemoEnd();
	FPerfSample AveragePerfSamples(const TArray<FPerfSample>& Samples) const;

	EBenchmarkPhase CurrentPhase = EBenchmarkPhase::Idle;
	float PendingScaleMultiplier = 1.f;

	int32 LastBaselineAgentCount = 0;
	int32 LastScaledAgentCount = 0;
	FPerfSample ScaledAverage;

	TArray<FPerfSample> BaselineSamples;
	TArray<FPerfSample> ScaledSamples;

	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> SpawnedAgents;

	FTimerHandle SampleTimerHandle;
	FTimerHandle PhaseTimerHandle;
	FTimerHandle DeadlockDemoTimerHandle;
};
