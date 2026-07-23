// Copyright Epic Games, Inc. All Rights Reserved.

#include "Benchmark/ReplayPlaybackSubsystem.h"
#include "Atlas_CyberDepot.h"
#include "Misc/FileHelper.h"
#include "JsonObjectConverter.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"

bool UReplayPlaybackSubsystem::LoadRecording(const FString& FilePath)
{
	// 버그 수정(사용자 리포트) — FFileHelper::LoadFileToStringArray는 내부적으로 bAllowWrite 없이 파일을
	// 열어서, 기록기가 아직 쓰기 핸들을 쥐고 있는 "지금 기록 중인" 파일을 읽으려 하면 공유 위반으로 항상
	// 실패했다(리플레이 토글이 CurrentRecordingFilePath를 그대로 불러오는 용도라 거의 항상 이 상황이다).
	// IPlatformFile::OpenRead(bAllowWrite=true)로 직접 열어야 쓰기 중인 파일도 읽을 수 있다.
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> ReadHandle(PlatformFile.OpenRead(*FilePath, /*bAllowWrite=*/true));
	if (!ReadHandle)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[ReplayPlayback] LoadRecording 실패 — 파일을 열 수 없음(%s)"), *FilePath);
		return false;
	}

	const int64 FileSize = ReadHandle->Size();
	TArray<uint8> RawBytes;
	RawBytes.SetNumUninitialized(FileSize);
	if (FileSize > 0 && !ReadHandle->Read(RawBytes.GetData(), FileSize))
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[ReplayPlayback] LoadRecording 실패 — 읽기 실패(%s)"), *FilePath);
		return false;
	}
	ReadHandle.Reset();

	FString FullText;
	FFileHelper::BufferToString(FullText, RawBytes.GetData(), RawBytes.Num());

	TArray<FString> Lines;
	FullText.ParseIntoArrayLines(Lines);

	LoadedFrames.Empty();
	for (const FString& Line : Lines)
	{
		if (Line.IsEmpty())
		{
			continue;
		}

		FStateSnapshot Snapshot;
		if (FJsonObjectConverter::JsonObjectStringToUStruct(Line, &Snapshot, 0, 0))
		{
			LoadedFrames.Add(Snapshot);
		}
	}

	CurrentFrameIndex = 0;
	PlaybackElapsedSeconds = 0.0;
	bIsPlaying = false;

	// 로깅 규율 — 파일은 정상적으로 열고 읽었는데 유효한 스냅샷이 0개인 경우(공유 위반 등과는 다른 원인,
	// 예: 재생을 너무 일찍 눌러 아직 아무 에이전트도 이동/상태변화를 기록하지 않은 경우)를 위 두 실패
	// 로그와 구분해서 남긴다.
	if (LoadedFrames.Num() == 0)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[ReplayPlayback] LoadRecording — 파일은 정상적으로 읽었지만 유효한 스냅샷이 0개(%s). 아직 기록된 데이터가 없을 수 있음(너무 이른 재생 시도 등)"), *FilePath);
	}

	return LoadedFrames.Num() > 0;
}

void UReplayPlaybackSubsystem::Play()
{
	if (LoadedFrames.Num() > 0)
	{
		bIsPlaying = true;
	}
}

void UReplayPlaybackSubsystem::Pause()
{
	bIsPlaying = false;
}

void UReplayPlaybackSubsystem::Stop()
{
	bIsPlaying = false;
	LoadedFrames.Empty();
	CurrentFrameIndex = 0;
	PlaybackElapsedSeconds = 0.0;
}

void UReplayPlaybackSubsystem::SeekToTime(float TimeSeconds)
{
	PlaybackElapsedSeconds = TimeSeconds;

	CurrentFrameIndex = 0;
	while (CurrentFrameIndex < LoadedFrames.Num() && ElapsedSecondsFromFrame(CurrentFrameIndex) <= PlaybackElapsedSeconds)
	{
		++CurrentFrameIndex;
	}

	// 버그 수정 — 기존엔 인덱스만 갱신하고 아무 것도 방출하지 않아, 탐색해도 고스트가 다음 자연 진행 때까지
	// 그 자리에 멈춰 있었다(스크럽바에서 드래그해도 화면이 안 바뀜). CurrentFrameIndex는 Tick()과 동일한
	// 관례로 "다음에 재생될 프레임"을 가리키므로, 탐색 시점의 상태는 그 직전 프레임이다.
	if (CurrentFrameIndex > 0)
	{
		OnPlaybackFrame.Broadcast(LoadedFrames[CurrentFrameIndex - 1]);
	}
}

void UReplayPlaybackSubsystem::Tick(float DeltaTime)
{
	if (!bIsPlaying || LoadedFrames.Num() == 0)
	{
		return;
	}

	PlaybackElapsedSeconds += DeltaTime * PlaybackSpeed;

	while (CurrentFrameIndex < LoadedFrames.Num() && ElapsedSecondsFromFrame(CurrentFrameIndex) <= PlaybackElapsedSeconds)
	{
		OnPlaybackFrame.Broadcast(LoadedFrames[CurrentFrameIndex]);
		++CurrentFrameIndex;
	}

	if (CurrentFrameIndex >= LoadedFrames.Num())
	{
		bIsPlaying = false;
	}
}

bool UReplayPlaybackSubsystem::IsTickable() const
{
	return !IsTemplate();
}

TStatId UReplayPlaybackSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UReplayPlaybackSubsystem, STATGROUP_Tickables);
}

double UReplayPlaybackSubsystem::ElapsedSecondsFromFrame(int32 FrameIndex) const
{
	if (LoadedFrames.Num() == 0)
	{
		return 0.0;
	}

	return (LoadedFrames[FrameIndex].Timestamp - LoadedFrames[0].Timestamp).GetTotalSeconds();
}
