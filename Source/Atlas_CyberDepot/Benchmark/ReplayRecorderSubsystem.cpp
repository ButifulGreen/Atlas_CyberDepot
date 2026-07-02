// Copyright Epic Games, Inc. All Rights Reserved.

#include "Benchmark/ReplayRecorderSubsystem.h"
#include "EventBus/FactoryEventBusSubsystem.h"
#include "Misc/FileHelper.h"
#include "JsonObjectConverter.h"

void UReplayRecorderSubsystem::StartRecording(const FString& FilePath)
{
	if (bIsRecording)
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UFactoryEventBusSubsystem* EventBus = GI ? GI->GetSubsystem<UFactoryEventBusSubsystem>() : nullptr;
	if (!EventBus)
	{
		return;
	}

	CurrentRecordingFilePath = FilePath;
	PendingLines.Empty();
	bIsRecording = true;

	SnapshotHandle = EventBus->SubscribeSnapshot(FOnStateSnapshot::FDelegate::CreateUObject(this, &UReplayRecorderSubsystem::OnSnapshotReceived));
}

void UReplayRecorderSubsystem::StopRecording()
{
	if (!bIsRecording)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
			{
				EventBus->Unsubscribe(SnapshotHandle);
			}
		}
	}

	const FString Content = FString::Join(PendingLines, TEXT("\n"));
	FFileHelper::SaveStringToFile(Content, *CurrentRecordingFilePath);

	PendingLines.Empty();
	bIsRecording = false;
}

void UReplayRecorderSubsystem::OnSnapshotReceived(const FStateSnapshot& Snapshot)
{
	if (!bIsRecording)
	{
		return;
	}

	FString Line;
	if (FJsonObjectConverter::UStructToJsonObjectString(Snapshot, Line))
	{
		PendingLines.Add(Line);
	}
}

void UReplayRecorderSubsystem::Deinitialize()
{
	if (bIsRecording)
	{
		StopRecording();
	}

	Super::Deinitialize();
}
