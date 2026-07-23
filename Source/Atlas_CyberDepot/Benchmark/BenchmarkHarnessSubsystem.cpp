// Copyright Epic Games, Inc. All Rights Reserved.

#include "Benchmark/BenchmarkHarnessSubsystem.h"
#include "Atlas_CyberDepot.h"
#include "Benchmark/BenchmarkSettings.h"
#include "Agent/FactoryAgentBase.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMemory.h"
#include "HAL/IConsoleManager.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"

void UBenchmarkHarnessSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 스폰/이상탐지 발행 등 권한이 필요한 동작이라 서버(또는 싱글) 월드에서만 연다.
	// 리슨 서버 PIE는 서버+클라이언트 월드가 한 프로세스에 공존하므로, 클라이언트 월드는 등록 자체를
	// 건너뛰어 콘솔 커맨드 이름 중복 등록도 함께 막는다.
	if (GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}

	IConsoleManager& ConsoleManager = IConsoleManager::Get();

	RegisteredConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("Benchmark.RunScalingComparison"),
		TEXT("Benchmark.RunScalingComparison <BaselineAgentCount> <ScaleMultiplier> - 기준/확대 2단계 스케일링 성능 비교 시작"),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &UBenchmarkHarnessSubsystem::HandleRunScalingComparisonCommand),
		ECVF_Default));

	RegisteredConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("Benchmark.PrintScalingReport"),
		TEXT("Benchmark.PrintScalingReport - 직전 RunScalingComparison 결과 리포트를 로그로 출력"),
		FConsoleCommandDelegate::CreateUObject(this, &UBenchmarkHarnessSubsystem::HandlePrintScalingReportCommand),
		ECVF_Default));

	RegisteredConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("Benchmark.ExportPerfReport"),
		TEXT("Benchmark.ExportPerfReport <FilePath> - 누적된 성능 표본을 CSV로 저장"),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &UBenchmarkHarnessSubsystem::HandleExportPerfReportCommand),
		ECVF_Default));
}

bool UBenchmarkHarnessSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UBenchmarkHarnessSubsystem::Deinitialize()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	for (IConsoleObject* Command : RegisteredConsoleCommands)
	{
		ConsoleManager.UnregisterConsoleObject(Command);
	}
	RegisteredConsoleCommands.Empty();

	Super::Deinitialize();
}

bool UBenchmarkHarnessSubsystem::RunScalingComparison(int32 BaselineAgentCount, float ScaleMultiplier)
{
	if (CurrentPhase != EBenchmarkPhase::Idle)
	{
		return false;
	}

	BaselineSamples.Empty();
	ScaledSamples.Empty();
	PendingScaleMultiplier = ScaleMultiplier;

	BeginPhase(EBenchmarkPhase::RunningBaseline, BaselineAgentCount);
	return true;
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

	Report.bNonLinearBottleneckDetected = Report.TickTimeIncreasePercent > ExpectedLinearIncreasePercent * GetDefault<UBenchmarkSettings>()->NonLinearBottleneckToleranceMultiplier;

	return Report;
}

bool UBenchmarkHarnessSubsystem::ExportPerfReport(const FString& FilePath)
{
	FString Csv = TEXT("Timestamp,GameThreadTickTimeMs,GPUFrameTimeMs,MemoryUsageMB,ActiveAgentCount\n");
	for (const FPerfSample& Sample : RecordedSamples)
	{
		Csv += FString::Printf(TEXT("%f,%f,%f,%f,%d\n"),
			Sample.Timestamp, Sample.GameThreadTickTimeMs,
			Sample.GPUFrameTimeMs, Sample.MemoryUsageMB, Sample.ActiveAgentCount);
	}

	return FFileHelper::SaveStringToFile(Csv, *FilePath);
}

void UBenchmarkHarnessSubsystem::SpawnBenchmarkAgents(int32 Count)
{
	UWorld* World = GetWorld();
	const UBenchmarkSettings* Settings = GetDefault<UBenchmarkSettings>();
	const TSubclassOf<AFactoryAgentBase> AgentClass = Settings->AgentClassToSpawn.LoadSynchronous();
	if (!World || !AgentClass)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Benchmark] SpawnBenchmarkAgents 스킵 — AgentClassToSpawn이 비어있음(Project Settings > Atlas Benchmark Settings에서 지정 필요)"));
		return;
	}

	for (int32 i = 0; i < Count; ++i)
	{
		const FVector2D RandomOffset = FMath::RandPointInCircle(Settings->SpawnRadius);
		const FVector SpawnLocation = Settings->SpawnOrigin + FVector(RandomOffset.X, RandomOffset.Y, 0.f);

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		if (AActor* Spawned = World->SpawnActor<AActor>(AgentClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams))
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

	const UBenchmarkSettings* Settings = GetDefault<UBenchmarkSettings>();
	World->GetTimerManager().SetTimer(SampleTimerHandle, this, &UBenchmarkHarnessSubsystem::RecordPerfSample, Settings->SampleIntervalSeconds, true);
	World->GetTimerManager().SetTimer(PhaseTimerHandle, this, &UBenchmarkHarnessSubsystem::OnPhaseComplete, Settings->PhaseDurationSeconds, false);
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
		Result.GPUFrameTimeMs += Sample.GPUFrameTimeMs;
		Result.MemoryUsageMB += Sample.MemoryUsageMB;
		AgentCountSum += Sample.ActiveAgentCount;
	}

	const float Count = static_cast<float>(Samples.Num());
	Result.GameThreadTickTimeMs /= Count;
	Result.GPUFrameTimeMs /= Count;
	Result.MemoryUsageMB /= Count;
	Result.ActiveAgentCount = FMath::RoundToInt(AgentCountSum / Count);
	Result.Timestamp = Samples.Last().Timestamp;

	return Result;
}

void UBenchmarkHarnessSubsystem::HandleRunScalingComparisonCommand(const TArray<FString>& Args)
{
	if (Args.Num() < 2 || !FCString::IsNumeric(*Args[0]) || !FCString::IsNumeric(*Args[1]))
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Benchmark] 사용법: Benchmark.RunScalingComparison <BaselineAgentCount> <ScaleMultiplier> (숫자만)"));
		return;
	}

	const int32 BaselineAgentCount = FCString::Atoi(*Args[0]);
	const float ScaleMultiplier = FCString::Atof(*Args[1]);
	if (RunScalingComparison(BaselineAgentCount, ScaleMultiplier))
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Benchmark] RunScalingComparison 시작 — Baseline=%d, ScaleMultiplier=%.2f"), BaselineAgentCount, ScaleMultiplier);
	}
	else
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Benchmark] RunScalingComparison 거부됨 — 이미 다른 비교가 진행 중입니다."));
	}
}

void UBenchmarkHarnessSubsystem::HandlePrintScalingReportCommand()
{
	if (CurrentPhase != EBenchmarkPhase::Idle)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Benchmark] PrintScalingReport — RunScalingComparison이 아직 진행 중입니다. 완료 후 다시 시도하세요."));
		return;
	}

	if (LastScaledAgentCount == 0)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Benchmark] PrintScalingReport — 아직 완료된 RunScalingComparison 실행이 없습니다."));
		return;
	}

	const FScalingReport Report = ComputeScalingReport();
	UE_LOG(LogFactoryDispatch, Log,
		TEXT("[Benchmark] ScalingReport — Baseline=%d Scaled=%d TickTime%+.1f%% Memory%+.1f%% GPU%+.1f%% NonLinearBottleneck=%s"),
		Report.BaselineAgentCount, Report.ScaledAgentCount,
		Report.TickTimeIncreasePercent, Report.MemoryIncreasePercent, Report.GPUTimeIncreasePercent,
		Report.bNonLinearBottleneckDetected ? TEXT("true") : TEXT("false"));
}

void UBenchmarkHarnessSubsystem::HandleExportPerfReportCommand(const TArray<FString>& Args)
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Benchmark] 사용법: Benchmark.ExportPerfReport <FilePath>"));
		return;
	}

	if (ExportPerfReport(Args[0]))
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Benchmark] ExportPerfReport 완료 — %s (표본 %d개)"), *Args[0], RecordedSamples.Num());
	}
	else
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Benchmark] ExportPerfReport 실패 — 파일에 쓸 수 없음(%s)"), *Args[0]);
	}
}
