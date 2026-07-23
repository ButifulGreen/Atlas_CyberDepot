// Copyright Epic Games, Inc. All Rights Reserved.

#include "Benchmark/ReplayVisualizationSubsystem.h"
#include "Atlas_CyberDepot.h"
#include "Benchmark/ReplayGhostActor.h"
#include "Benchmark/ReplayPlaybackSubsystem.h"
#include "Benchmark/ReplaySettings.h"

void UReplayVisualizationSubsystem::BeginVisualizing()
{
	UWorld* World = GetWorld();
	UReplayPlaybackSubsystem* Playback = World ? World->GetSubsystem<UReplayPlaybackSubsystem>() : nullptr;
	if (!Playback)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[ReplayViz] BeginVisualizing 실패 — ReplayPlaybackSubsystem을 못 찾음"));
		return;
	}

	PlaybackFrameHandle = Playback->OnPlaybackFrame.AddUObject(this, &UReplayVisualizationSubsystem::OnPlaybackFrameReceived);
}

void UReplayVisualizationSubsystem::StopVisualizing()
{
	if (UWorld* World = GetWorld())
	{
		if (UReplayPlaybackSubsystem* Playback = World->GetSubsystem<UReplayPlaybackSubsystem>())
		{
			Playback->OnPlaybackFrame.Remove(PlaybackFrameHandle);
		}
	}

	for (const TPair<FGuid, TObjectPtr<AReplayGhostActor>>& Pair : GhostActors)
	{
		if (AReplayGhostActor* Ghost = Pair.Value)
		{
			Ghost->Destroy();
		}
	}
	GhostActors.Empty();
}

void UReplayVisualizationSubsystem::OnPlaybackFrameReceived(const FStateSnapshot& Snapshot)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TObjectPtr<AReplayGhostActor>* Existing = GhostActors.Find(Snapshot.ActorID);
	AReplayGhostActor* Ghost = Existing ? Existing->Get() : nullptr;

	if (!Ghost)
	{
		TSubclassOf<AReplayGhostActor> GhostClass = ResolveGhostClass(Snapshot.ActorType);
		if (!GhostClass)
		{
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[ReplayViz] ActorType=%d 고스트 클래스가 비어있음 — 서브시스템 기본값에 BP 할당 필요"), static_cast<int32>(Snapshot.ActorType));
			return;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Ghost = World->SpawnActor<AReplayGhostActor>(GhostClass, Snapshot.Location, Snapshot.Rotation, SpawnParams);
		if (!Ghost)
		{
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[ReplayViz] 고스트 액터 스폰 실패(ActorID=%s)"), *Snapshot.ActorID.ToString());
			return;
		}
		GhostActors.Add(Snapshot.ActorID, Ghost);
	}

	Ghost->UpdateFromSnapshot(Snapshot);
}

TSubclassOf<AReplayGhostActor> UReplayVisualizationSubsystem::ResolveGhostClass(EActorType ActorType) const
{
	const UReplaySettings* Settings = GetDefault<UReplaySettings>();
	switch (ActorType)
	{
	case EActorType::AtlasRobot:
		return Settings->AtlasRobotGhostClass.LoadSynchronous();
	case EActorType::TransportRobot:
		return Settings->TransportRobotGhostClass.LoadSynchronous();
	case EActorType::NPCHuman:
		return Settings->NPCHumanGhostClass.LoadSynchronous();
	default:
		return nullptr;
	}
}
