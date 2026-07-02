// Copyright Epic Games, Inc. All Rights Reserved.

#include "Visualization/CongestionHeatmapSubsystem.h"
#include "EventBus/FactoryEventBusSubsystem.h"

void UCongestionHeatmapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
		{
			SnapshotHandle = EventBus->SubscribeSnapshot(FOnStateSnapshot::FDelegate::CreateUObject(this, &UCongestionHeatmapSubsystem::OnAgentSnapshot));
			AnomalyHandle = EventBus->SubscribeAnomaly(FOnAnomalyEvent::FDelegate::CreateUObject(this, &UCongestionHeatmapSubsystem::OnAnomalyReceived));
		}
	}
}

void UCongestionHeatmapSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
		{
			EventBus->Unsubscribe(SnapshotHandle);
			EventBus->Unsubscribe(AnomalyHandle);
		}
	}

	Super::Deinitialize();
}

void UCongestionHeatmapSubsystem::OnAgentSnapshot(const FStateSnapshot& Snapshot)
{
	float& Score = CellCongestionScore.FindOrAdd(LocationToGridCoord(Snapshot.Location));
	Score += 1.f;
}

void UCongestionHeatmapSubsystem::TickRecompute(float DeltaTime)
{
	TimeSinceLastUpdate += DeltaTime;
	if (TimeSinceLastUpdate < UpdateIntervalSeconds)
	{
		return;
	}
	TimeSinceLastUpdate = 0.f;

	for (TPair<FIntPoint, float>& Pair : CellCongestionScore)
	{
		Pair.Value *= DecayRatePerUpdate;
	}

	CellsWithActiveAnomaly.Empty();
}

TArray<FCongestionCell> UCongestionHeatmapSubsystem::GetCurrentSnapshot() const
{
	TArray<FCongestionCell> Result;
	Result.Reserve(CellCongestionScore.Num());

	for (const TPair<FIntPoint, float>& Pair : CellCongestionScore)
	{
		FCongestionCell Cell;
		Cell.GridCoord = Pair.Key;
		Cell.Score = Pair.Value;
		Cell.bHasActiveAnomaly = CellsWithActiveAnomaly.Contains(Pair.Key);
		Result.Add(Cell);
	}

	return Result;
}

void UCongestionHeatmapSubsystem::Tick(float DeltaTime)
{
	TickRecompute(DeltaTime);
}

bool UCongestionHeatmapSubsystem::IsTickable() const
{
	return !IsTemplate();
}

TStatId UCongestionHeatmapSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCongestionHeatmapSubsystem, STATGROUP_Tickables);
}

void UCongestionHeatmapSubsystem::OnAnomalyReceived(const FAnomalyEvent& Event)
{
	CellsWithActiveAnomaly.Add(LocationToGridCoord(Event.Location));
}

FIntPoint UCongestionHeatmapSubsystem::LocationToGridCoord(const FVector& Location) const
{
	return FIntPoint(FMath::FloorToInt(Location.X / GridCellSize), FMath::FloorToInt(Location.Y / GridCellSize));
}
