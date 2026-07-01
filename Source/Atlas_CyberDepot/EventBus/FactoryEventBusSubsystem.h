// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EventBus/FactoryEventTypes.h"
#include "FactoryEventBusSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnAnomalyEvent, const FAnomalyEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnStateSnapshot, const FStateSnapshot&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnTaskLifecycleEvent, const FTaskLifecycleEvent&);

// Docs/01_EventBus_DataPipeline.md §1 — 다른 모든 시스템이 이 이벤트 버스를 구독한다.
UCLASS()
class UFactoryEventBusSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	FOnAnomalyEvent OnAnomalyPublished;
	FOnStateSnapshot OnSnapshotPublished;
	FOnTaskLifecycleEvent OnTaskLifecyclePublished;

	void PublishAnomaly(const FAnomalyEvent& Event);
	void PublishSnapshot(const FStateSnapshot& Snapshot);
	void PublishTaskLifecycle(const FTaskLifecycleEvent& Event);

	FDelegateHandle SubscribeAnomaly(const FOnAnomalyEvent::FDelegate& Callback);
	FDelegateHandle SubscribeSnapshot(const FOnStateSnapshot::FDelegate& Callback);
	FDelegateHandle SubscribeTaskLifecycle(const FOnTaskLifecycleEvent::FDelegate& Callback);

	// 세 델리게이트 중 Handle이 등록된 쪽에서만 제거되며, 나머지는 안전하게 무시된다.
	void Unsubscribe(FDelegateHandle Handle);
};
