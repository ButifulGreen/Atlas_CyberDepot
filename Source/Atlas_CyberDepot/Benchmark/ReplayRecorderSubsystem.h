// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ReplayRecorderSubsystem.generated.h"

struct FStateSnapshot;
class IFileHandle;

// Docs/10_Benchmark_Replay.md §10 — 마지막 단계. 로직 재실행이 아니라 기록된 상태(State)의 재생을 위한 기록기.
// 이벤트 버스의 OnSnapshotPublished를 구독해, 스냅샷을 JSON Lines(한 줄에 하나씩) 형식으로 파일에 남긴다.
// 사용자 지시 — 월드 시작과 동시에 자동으로 기록을 시작하는 블랙박스: 항목이 들어올 때마다 즉시 파일에
// 기록하고(강제종료 대비), UReplaySettings::RetentionSeconds보다 오래된 항목은 PruneIntervalSeconds 주기로
// 실제 파일에서도 지운다(무제한 누적 방지 — AI 학습 로그 전용인 UTrainingDataRecorderSubsystem과 반대로
// 계속 누적하지 않음). 보존/정리 주기 값은 이 클래스가 아니라 UReplaySettings(프로젝트 세팅 패널)에서
// 편집한다 — WorldSubsystem엔 에디터에서 EditAnywhere 값을 편집할 CDO 접점이 마땅치 않아서다.
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

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	void PruneAndRewrite();

	// 보존 기간 내 항목만 유지 — 새 항목은 즉시 파일에도 append하고, PruneAndRewrite가 주기적으로
	// 이 배열 기준(오래된 항목을 걷어낸 뒤)으로 파일을 다시 써서 실제로 지운다.
	TArray<FStateSnapshot> RecentEntries;
	TUniquePtr<IFileHandle> RecordingFile;
	FDelegateHandle SnapshotHandle;
	FTimerHandle PruneTimerHandle;
};
