// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ReplayRecorderSubsystem.generated.h"

struct FStateSnapshot;

// Docs/10_Benchmark_Replay.md §10 — 마지막 단계. 로직 재실행이 아니라 기록된 상태(State)의 재생을 위한 기록기.
// 이벤트 버스의 OnSnapshotPublished를 구독해, 스냅샷을 JSON Lines(한 줄에 하나씩) 형식으로 파일에 남긴다.
UCLASS()
class UReplayRecorderSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	bool bIsRecording = false;

	UPROPERTY(BlueprintReadOnly)
	FString CurrentRecordingFilePath;

	void StartRecording(const FString& FilePath);
	void StopRecording();
	void OnSnapshotReceived(const FStateSnapshot& Snapshot);

	virtual void Deinitialize() override;

private:
	TArray<FString> PendingLines;
	FDelegateHandle SnapshotHandle;
};
