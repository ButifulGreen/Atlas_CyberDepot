// Copyright Epic Games, Inc. All Rights Reserved.

#include "Benchmark/ReplayControlWidget.h"
#include "Atlas_CyberDepot.h"
#include "Benchmark/ReplayPlaybackSubsystem.h"

void UReplayControlWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UWorld* World = GetWorld())
	{
		PlaybackSubsystem = World->GetSubsystem<UReplayPlaybackSubsystem>();
	}

	if (!PlaybackSubsystem)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[ReplayControl] UReplayPlaybackSubsystem을 못 찾음"));
	}
}

void UReplayControlWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!PlaybackSubsystem)
	{
		return;
	}

	BP_OnPlaybackProgress(
		static_cast<float>(PlaybackSubsystem->GetPlaybackElapsedSeconds()),
		static_cast<float>(PlaybackSubsystem->GetTotalDurationSeconds()),
		PlaybackSubsystem->IsPlaying(),
		PlaybackSubsystem->PlaybackSpeed);
}

void UReplayControlWidget::Play()
{
	if (PlaybackSubsystem)
	{
		PlaybackSubsystem->Play();
	}
}

void UReplayControlWidget::Pause()
{
	if (PlaybackSubsystem)
	{
		PlaybackSubsystem->Pause();
	}
}

void UReplayControlWidget::TogglePause()
{
	if (!PlaybackSubsystem)
	{
		return;
	}

	if (PlaybackSubsystem->IsPlaying())
	{
		PlaybackSubsystem->Pause();
	}
	else
	{
		PlaybackSubsystem->Play();
	}
}

void UReplayControlWidget::SeekToTime(float TimeSeconds)
{
	if (PlaybackSubsystem)
	{
		PlaybackSubsystem->SeekToTime(TimeSeconds);
	}
}

void UReplayControlWidget::SetReplayPlaybackSpeed(float Speed)
{
	if (!PlaybackSubsystem)
	{
		return;
	}

	if (Speed <= 0.f)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[ReplayControl] SetPlaybackSpeed 거부됨 — 되감기(역재생)는 지원하지 않아 0 이하 값은 무시함(%.2f)"), Speed);
		return;
	}

	PlaybackSubsystem->PlaybackSpeed = Speed;
}
