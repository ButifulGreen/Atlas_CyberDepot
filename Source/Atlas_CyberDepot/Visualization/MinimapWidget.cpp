// Copyright Epic Games, Inc. All Rights Reserved.

#include "Visualization/MinimapWidget.h"
#include "Visualization/CongestionHeatmapSubsystem.h"
#include "EventBus/FactoryEventBusSubsystem.h"

void UMinimapWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	HeatmapSource = World->GetSubsystem<UCongestionHeatmapSubsystem>();

	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
		{
			AnomalyHandle = EventBus->SubscribeAnomaly(FOnAnomalyEvent::FDelegate::CreateUObject(this, &UMinimapWidget::OnAnomalyMarkerAdded));
		}
	}

	if (HeatmapSource)
	{
		World->GetTimerManager().SetTimer(RefreshTimerHandle, this, &UMinimapWidget::RefreshOverlay, HeatmapSource->UpdateIntervalSeconds, true);
	}
}

void UMinimapWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimerHandle);

		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
			{
				EventBus->Unsubscribe(AnomalyHandle);
			}
		}
	}

	Super::NativeDestruct();
}

void UMinimapWidget::RefreshOverlay()
{
	if (!HeatmapSource)
	{
		return;
	}

	BP_OnOverlayRefreshed(HeatmapSource->GetCurrentSnapshot());
}

void UMinimapWidget::OnAnomalyMarkerAdded(const FAnomalyEvent& Event)
{
	BP_OnAnomalyMarkerAdded(Event);
}
