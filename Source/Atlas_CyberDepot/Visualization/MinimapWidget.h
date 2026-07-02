// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MinimapWidget.generated.h"

class UCongestionHeatmapSubsystem;
struct FCongestionCell;
struct FAnomalyEvent;

// Docs/09_Visualization.md §9 — 9단계 MVP. 실제 드로잉은 UMG 디자이너(에디터 작업)에서 BlueprintImplementableEvent를 구현해 채운다.
UCLASS()
class UMinimapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UCongestionHeatmapSubsystem> HeatmapSource;

	void RefreshOverlay();
	void OnAnomalyMarkerAdded(const FAnomalyEvent& Event);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintImplementableEvent, Category = "Minimap")
	void BP_OnOverlayRefreshed(const TArray<FCongestionCell>& Cells);

	UFUNCTION(BlueprintImplementableEvent, Category = "Minimap")
	void BP_OnAnomalyMarkerAdded(const FAnomalyEvent& Event);

private:
	FDelegateHandle AnomalyHandle;
	FTimerHandle RefreshTimerHandle;
};
