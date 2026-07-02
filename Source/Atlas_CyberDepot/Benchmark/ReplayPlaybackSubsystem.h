// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "EventBus/FactoryEventTypes.h"
#include "ReplayPlaybackSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnPlaybackFrame, const FStateSnapshot&);

// Docs/10_Benchmark_Replay.md §10 — 마지막 단계. 로직 재실행이 아니라 기록된 상태(State)의 재생.
// 액터 스폰/매핑은 Docs에 명시돼 있지 않아, 재생 프레임 도달 시 OnPlaybackFrame 델리게이트로 스냅샷만
// 방출한다 — 실제 시각화(고스트 액터 스폰 등)는 이를 구독하는 후속 소비자(9단계 시각화 등)의 몫으로 둔다.
UCLASS()
class UReplayPlaybackSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	TArray<FStateSnapshot> LoadedFrames;

	UPROPERTY(BlueprintReadOnly)
	int32 CurrentFrameIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Replay")
	float PlaybackSpeed = 1.f;

	FOnPlaybackFrame OnPlaybackFrame;

	bool LoadRecording(const FString& FilePath);
	void Play();
	void Pause();
	void SeekToTime(float TimeSeconds);

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

private:
	double ElapsedSecondsFromFrame(int32 FrameIndex) const;

	bool bIsPlaying = false;
	double PlaybackElapsedSeconds = 0.0;
};
