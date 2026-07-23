// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ReplaySettings.generated.h"

class AReplayGhostActor;

// Docs/10_Benchmark_Replay.md — 리플레이/AI 학습 로그 파이프라인의 서브시스템(UWorldSubsystem)은 레벨에
// 배치되는 액터가 아니라, EditAnywhere 프로퍼티를 각 서브시스템에 직접 두면 에디터에서 편집할 방법이
// 마땅치 않다(Blueprint 서브클래스를 만들어도 별개의 서브시스템 인스턴스가 하나 더 생길 뿐, 실제 코드가
// GetSubsystem<T>()로 참조하는 C++ 클래스 자체의 인스턴스에는 반영되지 않는다 — 엔진 소스
// FSubsystemCollectionBase::AddAndInitializeSubsystems가 구체 클래스마다 독립된 인스턴스를 생성하는
// 구조라서다). 대신 프로젝트 세팅(Edit > Project Settings > Game > Atlas Replay Settings) 패널로 노출되는
// UDeveloperSettings에 모아, 재컴파일 없이 에디터에서 조정 가능하게 한다.
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Atlas Replay Settings"))
class UReplaySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 리플레이 블랙박스 보존 기간(초) — 이보다 오래된 기록은 UReplayRecorderSubsystem이 주기적으로 폐기한다.
	// 현재는 테스트를 위해 짧은 값(기본 600초=10분)으로 시작.
	UPROPERTY(EditAnywhere, config, Category = "Replay|Recording")
	float RetentionSeconds = 600.f;

	// 블랙박스 정리(디스크 재작성) 주기(초) — 매 스냅샷마다 다시 쓰면 부하가 커서 절충한 값.
	UPROPERTY(EditAnywhere, config, Category = "Replay|Recording")
	float PruneIntervalSeconds = 10.f;

	// AI 학습 로그 실시간 저장의 강제 디스크 Flush 주기(초) — 매 Write 자체는 이미 즉시 나가므로 안전
	// 마진일 뿐이다.
	UPROPERTY(EditAnywhere, config, Category = "Replay|Recording")
	float TrainingDataFlushIntervalSeconds = 5.f;

	// ActorType별 고스트 액터 BP — 실제 메시/머티리얼은 BP에서 자유롭게 구성. 비워두면 해당 타입은
	// 스폰되지 않고 경고 로그만 남는다(EActorType 값이 3개뿐이라 맵 대신 명시적 슬롯).
	UPROPERTY(EditAnywhere, config, Category = "Replay|Visualization")
	TSoftClassPtr<AReplayGhostActor> AtlasRobotGhostClass;

	UPROPERTY(EditAnywhere, config, Category = "Replay|Visualization")
	TSoftClassPtr<AReplayGhostActor> TransportRobotGhostClass;

	UPROPERTY(EditAnywhere, config, Category = "Replay|Visualization")
	TSoftClassPtr<AReplayGhostActor> NPCHumanGhostClass;
};
