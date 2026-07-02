// Copyright Epic Games, Inc. All Rights Reserved.

#include "Benchmark/BenchmarkHarnessSubsystem.h"
#include "Agent/FactoryAgentBase.h"
#include "EventBus/FactoryEventBusSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMemory.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"

void UBenchmarkHarnessSubsystem::RunScalingComparison(int32 BaselineAgentCount, float ScaleMultiplier)
{
	if (CurrentPhase != EBenchmarkPhase::Idle)
	{
		return;
	}

	BaselineSamples.Empty();
	ScaledSamples.Empty();
	PendingScaleMultiplier = ScaleMultiplier;

	BeginPhase(EBenchmarkPhase::RunningBaseline, BaselineAgentCount);
}

void UBenchmarkHarnessSubsystem::RecordPerfSample()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FPerfSample Sample;
	Sample.Timestamp = FPlatformTime::Seconds();
	Sample.GameThreadTickTimeMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
	Sample.GPUFrameTimeMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
	Sample.NavigationTickTimeMs = 0.f;

	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	Sample.MemoryUsageMB = static_cast<float>(MemStats.UsedPhysical) / (1024.f * 1024.f);

	TArray<AActor*> FoundAgents;
	UGameplayStatics::GetAllActorsOfClass(World, AFactoryAgentBase::StaticClass(), FoundAgents);
	Sample.ActiveAgentCount = FoundAgents.Num();

	RecordedSamples.Add(Sample);

	if (CurrentPhase == EBenchmarkPhase::RunningBaseline)
	{
		BaselineSamples.Add(Sample);
	}
	else if (CurrentPhase == EBenchmarkPhase::RunningScaled)
	{
		ScaledSamples.Add(Sample);
	}
}

FScalingReport UBenchmarkHarnessSubsystem::ComputeScalingReport() const
{
	FScalingReport Report;
	Report.BaselineAgentCount = LastBaselineAgentCount;
	Report.ScaledAgentCount = LastScaledAgentCount;

	auto PercentIncrease = [](float Base, float Scaled) -> float
	{
		return Base > 0.f ? (Scaled - Base) / Base * 100.f : 0.f;
	};

	Report.TickTimeIncreasePercent = PercentIncrease(BaselineAverage.GameThreadTickTimeMs, ScaledAverage.GameThreadTickTimeMs);
	Report.MemoryIncreasePercent = PercentIncrease(BaselineAverage.MemoryUsageMB, ScaledAverage.MemoryUsageMB);
	Report.GPUTimeIncreasePercent = PercentIncrease(BaselineAverage.GPUFrameTimeMs, ScaledAverage.GPUFrameTimeMs);

	const float ExpectedLinearIncreasePercent = LastBaselineAgentCount > 0
		? (static_cast<float>(LastScaledAgentCount) / LastBaselineAgentCount - 1.f) * 100.f
		: 0.f;

	Report.bNonLinearBottleneckDetected = Report.TickTimeIncreasePercent > ExpectedLinearIncreasePercent * NonLinearBottleneckToleranceMultiplier;

	return Report;
}

void UBenchmarkHarnessSubsystem::ExportPerfReport(const FString& FilePath)
{
	FString Csv = TEXT("Timestamp,GameThreadTickTimeMs,NavigationTickTimeMs,GPUFrameTimeMs,MemoryUsageMB,ActiveAgentCount\n");
	for (const FPerfSample& Sample : RecordedSamples)
	{
		Csv += FString::Printf(TEXT("%f,%f,%f,%f,%f,%d\n"),
			Sample.Timestamp, Sample.GameThreadTickTimeMs, Sample.NavigationTickTimeMs,
			Sample.GPUFrameTimeMs, Sample.MemoryUsageMB, Sample.ActiveAgentCount);
	}

	FFileHelper::SaveStringToFile(Csv, *FilePath);
}

void UBenchmarkHarnessSubsystem::StartForcedDeadlockDemo(const FStressTestParams& Params)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> FoundAgents;
	UGameplayStatics::GetAllActorsOfClass(World, AFactoryAgentBase::StaticClass(), FoundAgents);

	UGameInstance* GI = World->GetGameInstance();
	UFactoryEventBusSubsystem* EventBus = GI ? GI->GetSubsystem<UFactoryEventBusSubsystem>() : nullptr;

	const int32 PickCount = FMath::Min(Params.ForcedDeadlockCount, FoundAgents.Num());
	for (int32 i = 0; i < PickCount; ++i)
	{
		AFactoryAgentBase* Agent = Cast<AFactoryAgentBase>(FoundAgents[i]);
		if (!Agent || !EventBus)
		{
			continue;
		}

		FAnomalyEvent Event;
		Event.Timestamp = FDateTime::UtcNow();
		Event.LogID = FGuid::NewGuid();
		Event.Severity = EEventSeverity::Critical;
		Event.ActorID = Agent->AgentID;
		Event.ActorType = Agent->AgentType;
		Event.AnomalyCode = TEXT("Code:001");
		Event.Location = Agent->GetActorLocation();
		EventBus->PublishAnomaly(Event);
	}

	World->GetTimerManager().SetTimer(DeadlockDemoTimerHandle, this, &UBenchmarkHarnessSubsystem::OnDeadlockDemoEnd, Params.DurationSeconds, false);
}

void UBenchmarkHarnessSubsystem::SpawnBenchmarkAgents(int32 Count)
{
	UWorld* World = GetWorld();
	if (!World || !AgentClassToSpawn)
	{
		return;
	}

	for (int32 i = 0; i < Count; ++i)
	{
		const FVector2D RandomOffset = FMath::RandPointInCircle(SpawnRadius);
		const FVector SpawnLocation = SpawnOrigin + FVector(RandomOffset.X, RandomOffset.Y, 0.f);

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		if (AActor* Spawned = World->SpawnActor<AActor>(AgentClassToSpawn, SpawnLocation, FRotator::ZeroRotator, SpawnParams))
		{
			SpawnedAgents.Add(Spawned);
		}
	}
}

void UBenchmarkHarnessSubsystem::DespawnBenchmarkAgents()
{
	for (const TWeakObjectPtr<AActor>& Agent : SpawnedAgents)
	{
		if (AActor* ValidAgent = Agent.Get())
		{
			ValidAgent->Destroy();
		}
	}
	SpawnedAgents.Empty();
}

void UBenchmarkHarnessSubsystem::BeginPhase(EBenchmarkPhase Phase, int32 AgentCount)
{
	CurrentPhase = Phase;
	SpawnBenchmarkAgents(AgentCount);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().SetTimer(SampleTimerHandle, this, &UBenchmarkHarnessSubsystem::RecordPerfSample, SampleIntervalSeconds, true);
	World->GetTimerManager().SetTimer(PhaseTimerHandle, this, &UBenchmarkHarnessSubsystem::OnPhaseComplete, PhaseDurationSeconds, false);
}

void UBenchmarkHarnessSubsystem::OnPhaseComplete()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SampleTimerHandle);
	}

	if (CurrentPhase == EBenchmarkPhase::RunningBaseline)
	{
		LastBaselineAgentCount = SpawnedAgents.Num();
		BaselineAverage = AveragePerfSamples(BaselineSamples);
		DespawnBenchmarkAgents();

		const int32 ScaledCount = FMath::Max(1, FMath::RoundToInt(LastBaselineAgentCount * PendingScaleMultiplier));
		BeginPhase(EBenchmarkPhase::RunningScaled, ScaledCount);
		return;
	}

	if (CurrentPhase == EBenchmarkPhase::RunningScaled)
	{
		LastScaledAgentCount = SpawnedAgents.Num();
		ScaledAverage = AveragePerfSamples(ScaledSamples);
		DespawnBenchmarkAgents();
		CurrentPhase = EBenchmarkPhase::Idle;
	}
}

void UBenchmarkHarnessSubsystem::OnDeadlockDemoEnd()
{
	// 정성적 데모 종료 시점 마커 — 실제 상태 복구는 각 에이전트의 기존 재개 원칙(Docs/00_DesignPrinciples.md)에 맡긴다.
}

FPerfSample UBenchmarkHarnessSubsystem::AveragePerfSamples(const TArray<FPerfSample>& Samples) const
{
	FPerfSample Result;
	if (Samples.Num() == 0)
	{
		return Result;
	}

	int32 AgentCountSum = 0;
	for (const FPerfSample& Sample : Samples)
	{
		Result.GameThreadTickTimeMs += Sample.GameThreadTickTimeMs;
		Result.NavigationTickTimeMs += Sample.NavigationTickTimeMs;
		Result.GPUFrameTimeMs += Sample.GPUFrameTimeMs;
		Result.MemoryUsageMB += Sample.MemoryUsageMB;
		AgentCountSum += Sample.ActiveAgentCount;
	}

	const float Count = static_cast<float>(Samples.Num());
	Result.GameThreadTickTimeMs /= Count;
	Result.NavigationTickTimeMs /= Count;
	Result.GPUFrameTimeMs /= Count;
	Result.MemoryUsageMB /= Count;
	Result.ActiveAgentCount = FMath::RoundToInt(AgentCountSum / Count);
	Result.Timestamp = Samples.Last().Timestamp;

	return Result;
}
