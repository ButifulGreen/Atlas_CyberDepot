// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Benchmark/BenchmarkTypes.h"
#include "BenchmarkHarnessSubsystem.generated.h"

class IConsoleObject;

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
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// 기본값(Game/Editor/PIE)은 Play를 누르지 않은 평범한 에디터 편집 월드까지 포함한다 — 스폰/콘솔 커맨드
	// 등록은 실제 플레이 세션에서만 의미가 있으므로 Editor를 제외한다.
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	UPROPERTY(BlueprintReadOnly)
	TArray<FPerfSample> RecordedSamples;

	UPROPERTY(BlueprintReadOnly)
	FPerfSample BaselineAverage;

	// 이미 진행 중(CurrentPhase != Idle)이면 아무 것도 하지 않고 false를 반환한다.
	bool RunScalingComparison(int32 BaselineAgentCount, float ScaleMultiplier);
	void RecordPerfSample();
	FScalingReport ComputeScalingReport() const;
	// 실제 파일 저장 성공 여부(FFileHelper::SaveStringToFile 결과)를 그대로 반환한다.
	bool ExportPerfReport(const FString& FilePath);

private:
	void SpawnBenchmarkAgents(int32 Count);
	void DespawnBenchmarkAgents();
	void BeginPhase(EBenchmarkPhase Phase, int32 AgentCount);
	void OnPhaseComplete();
	FPerfSample AveragePerfSamples(const TArray<FPerfSample>& Samples) const;

	// 콘솔 커맨드(Benchmark.*) 핸들러 — 파싱 후 위 공개 함수로 위임한다.
	void HandleRunScalingComparisonCommand(const TArray<FString>& Args);
	void HandlePrintScalingReportCommand();
	void HandleExportPerfReportCommand(const TArray<FString>& Args);

	TArray<IConsoleObject*> RegisteredConsoleCommands;

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
};
