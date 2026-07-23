// Copyright Epic Games, Inc. All Rights Reserved.

#include "EventBus/FactoryEventBusSubsystem.h"

void UFactoryEventBusSubsystem::PublishAnomaly(const FAnomalyEvent& Event)
{
	OnAnomalyPublished.Broadcast(Event);
}

void UFactoryEventBusSubsystem::PublishSnapshot(const FStateSnapshot& Snapshot)
{
	OnSnapshotPublished.Broadcast(Snapshot);
}

void UFactoryEventBusSubsystem::PublishTaskLifecycle(const FTaskLifecycleEvent& Event)
{
	OnTaskLifecyclePublished.Broadcast(Event);
}

void UFactoryEventBusSubsystem::PublishTrainingLogEntry(const FTrainingLogEntry& Entry)
{
	OnTrainingLogPublished.Broadcast(Entry);
}

FDelegateHandle UFactoryEventBusSubsystem::SubscribeAnomaly(const FOnAnomalyEvent::FDelegate& Callback)
{
	return OnAnomalyPublished.Add(Callback);
}

FDelegateHandle UFactoryEventBusSubsystem::SubscribeSnapshot(const FOnStateSnapshot::FDelegate& Callback)
{
	return OnSnapshotPublished.Add(Callback);
}

FDelegateHandle UFactoryEventBusSubsystem::SubscribeTaskLifecycle(const FOnTaskLifecycleEvent::FDelegate& Callback)
{
	return OnTaskLifecyclePublished.Add(Callback);
}

FDelegateHandle UFactoryEventBusSubsystem::SubscribeTrainingLogEntry(const FOnTrainingLogEntry::FDelegate& Callback)
{
	return OnTrainingLogPublished.Add(Callback);
}

void UFactoryEventBusSubsystem::Unsubscribe(FDelegateHandle Handle)
{
	OnAnomalyPublished.Remove(Handle);
	OnSnapshotPublished.Remove(Handle);
	OnTaskLifecyclePublished.Remove(Handle);
	OnTrainingLogPublished.Remove(Handle);
}
