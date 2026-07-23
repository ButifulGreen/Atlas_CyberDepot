// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EventBus/FactoryEventTypes.h"
#include "ReplayVisualizationSubsystem.generated.h"

class AReplayGhostActor;

// Docs/10_Benchmark_Replay.md — UReplayPlaybackSubsystem::OnPlaybackFrame을 구독해 FStateSnapshot을
// 실제 화면(고스트 액터)에 매핑하는 소비자. 재생 엔진(UReplayPlaybackSubsystem)과 화면 표시를 의도적으로
// 분리해둔 설계를 그대로 따라 별도 클래스로 둔다 — 재생기 자체는 이 클래스의 존재를 모른다. ActorType별
// 고스트 클래스는 이 클래스가 아니라 UReplaySettings(프로젝트 세팅 패널)에서 편집한다 — WorldSubsystem엔
// 에디터에서 EditAnywhere 값을 편집할 CDO 접점이 마땅치 않아서다.
UCLASS()
class UReplayVisualizationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 재생 토글 ON — OnPlaybackFrame 구독 시작(AFactorySpectatorPawn::OnToggleReplayTriggered가 호출).
	void BeginVisualizing();
	// 재생 토글 OFF — 구독 해제 + 스폰된 고스트 전부 제거.
	void StopVisualizing();

private:
	void OnPlaybackFrameReceived(const FStateSnapshot& Snapshot);
	TSubclassOf<AReplayGhostActor> ResolveGhostClass(EActorType ActorType) const;

	UPROPERTY()
	TMap<FGuid, TObjectPtr<AReplayGhostActor>> GhostActors;

	FDelegateHandle PlaybackFrameHandle;
};
