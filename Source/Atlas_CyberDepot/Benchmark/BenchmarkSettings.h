// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BenchmarkSettings.generated.h"

class AFactoryAgentBase;

// Docs/10_Benchmark_Replay.md — UBenchmarkHarnessSubsystem(UWorldSubsystem)은 레벨에 배치되는 액터가
// 아니라 EditAnywhere 프로퍼티를 직접 두면 에디터에서 편집할 CDO 접점이 없다(UReplaySettings와 동일한
// 이유 — FSubsystemCollectionBase::AddAndInitializeSubsystems가 구체 클래스마다 독립 인스턴스를 만드는
// 구조라서 Blueprint 서브클래스도 해결책이 안 됨). 대신 프로젝트 세팅(Edit > Project Settings > Game >
// Atlas Benchmark Settings) 패널로 노출되는 UDeveloperSettings에 모은다.
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Atlas Benchmark Settings"))
class UBenchmarkSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 스폰 대상 클래스 — 소프트 참조. 실제 벤치마크 실행(콘솔 커맨드) 시점에만 로드하면 되고, 에디터
	// 시작 시점의 세팅 CDO 로드 때는 불필요하게 미리 로드할 이유가 없다(UReplaySettings의 고스트 클래스와
	// 동일한 이유).
	UPROPERTY(EditAnywhere, config, Category = "Benchmark|Spawn")
	TSoftClassPtr<AFactoryAgentBase> AgentClassToSpawn;

	UPROPERTY(EditAnywhere, config, Category = "Benchmark|Spawn")
	FVector SpawnOrigin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, config, Category = "Benchmark|Spawn")
	float SpawnRadius = 500.f;

	UPROPERTY(EditAnywhere, config, Category = "Benchmark|Measurement")
	float SampleIntervalSeconds = 1.f;

	UPROPERTY(EditAnywhere, config, Category = "Benchmark|Measurement")
	float PhaseDurationSeconds = 30.f;

	// 에이전트 수 증가율 대비 이 배수를 넘는 리소스 증가율을 비정상 병목으로 판정(Docs에 값이 없어 노출)
	UPROPERTY(EditAnywhere, config, Category = "Benchmark|Measurement")
	float NonLinearBottleneckToleranceMultiplier = 1.5f;
};
