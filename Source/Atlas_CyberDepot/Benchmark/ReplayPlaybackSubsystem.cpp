// Copyright Epic Games, Inc. All Rights Reserved.

#include "Benchmark/ReplayPlaybackSubsystem.h"
#include "Misc/FileHelper.h"
#include "JsonObjectConverter.h"

bool UReplayPlaybackSubsystem::LoadRecording(const FString& FilePath)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *FilePath))
	{
		return false;
	}

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

void UReplayPlaybackSubsystem::SeekToTime(float TimeSeconds)
{
	PlaybackElapsedSeconds = TimeSeconds;

	CurrentFrameIndex = 0;
	while (CurrentFrameIndex < LoadedFrames.Num() && ElapsedSecondsFromFrame(CurrentFrameIndex) <= PlaybackElapsedSeconds)
	{
		++CurrentFrameIndex;
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
