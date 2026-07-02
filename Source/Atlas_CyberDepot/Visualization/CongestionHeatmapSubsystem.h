// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "EventBus/FactoryEventTypes.h"
#include "CongestionHeatmapSubsystem.generated.h"

USTRUCT(BlueprintType)
struct FCongestionCell
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FIntPoint GridCoord = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly)
	float Score = 0.f;

	UPROPERTY(BlueprintReadOnly)
	bool bHasActiveAnomaly = false;
};

// Docs/09_Visualization.md §9 — 9단계. 이벤트 버스를 구독해 이동 경로 혼잡도를 그리드 단위로 누적/감쇠한다.
UCLASS()
class UCongestionHeatmapSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	TMap<FIntPoint, float> CellCongestionScore;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Heatmap")
	float UpdateIntervalSeconds = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Heatmap")
	float DecayRatePerUpdate = 0.85f;

	// Docs에 명시되지 않은 구현값: FVector Location(cm)을 그리드 셀로 변환하는 셀 한 변의 크기
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Heatmap")
	float GridCellSize = 200.f;

	void OnAgentSnapshot(const FStateSnapshot& Snapshot);
	void TickRecompute(float DeltaTime);
	TArray<FCongestionCell> GetCurrentSnapshot() const;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

private:
	void OnAnomalyReceived(const FAnomalyEvent& Event);
	FIntPoint LocationToGridCoord(const FVector& Location) const;

	float TimeSinceLastUpdate = 0.f;

	// 이상 징후가 발생한 셀 — Docs에 소멸 시점이 명시돼 있지 않아, 혼잡도와 같은 주기(UpdateIntervalSeconds)로 함께 정리한다.
	UPROPERTY()
	TSet<FIntPoint> CellsWithActiveAnomaly;

	FDelegateHandle SnapshotHandle;
	FDelegateHandle AnomalyHandle;
};
