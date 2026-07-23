// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TrainingDataRecorderSubsystem.generated.h"

struct FTrainingLogEntry;
class IFileHandle;

// Docs/10_Benchmark_Replay.md — AI 학습용 데이터 전용 기록기. UReplayRecorderSubsystem(리플레이 재생용,
// 블랙박스로 오래된 프레임을 폐기)과 달리 계속 누적 보존해야 의미가 있는 데이터라 별도 클래스로 분리했다.
// EventBus의 OnTrainingLogPublished를 구독해 JSON Lines로 남기되, StopRecording까지 메모리에 모아뒀다
// 한번에 쓰는 대신 매 항목을 즉시 파일 핸들에 Write한다(강제종료 대비 — 사용자 지시). Write 자체가 이미
// OS로 데이터를 넘기므로 우리 프로세스가 죽어도 안전하고, UReplaySettings::TrainingDataFlushIntervalSeconds는
// 정전 등 OS 레벨 사고에 대한 추가 보강일 뿐이다. 그 값은 이 클래스가 아니라 UReplaySettings(프로젝트
// 세팅 패널)에서 편집한다 — WorldSubsystem엔 에디터에서 EditAnywhere 값을 편집할 CDO 접점이 마땅치 않아서다.
UCLASS()
class UTrainingDataRecorderSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	bool bIsRecording = false;

	UPROPERTY(BlueprintReadOnly)
	FString CurrentRecordingFilePath;

	void StartRecording(const FString& FilePath);
	void StopRecording();
	void OnTrainingLogReceived(const FTrainingLogEntry& Entry);

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	void FlushToDisk();

	TUniquePtr<IFileHandle> RecordingFile;
	// ActorID별 직전 기록 시각 — 수신한 항목의 ElapsedSinceLastEntrySeconds를 여기서 계산해 채운다.
	TMap<FGuid, FDateTime> LastEntryTimestamps;
	FDelegateHandle TrainingLogHandle;
	FTimerHandle FlushTimerHandle;
};
