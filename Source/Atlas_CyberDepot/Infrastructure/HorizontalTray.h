// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HorizontalTray.generated.h"

class ALogisticsItem;
class AFactoryAgentBase;

UENUM(BlueprintType)
enum class ETrayDirection : uint8
{
	Inbound,
	Outbound
};

// Docs/06_Infrastructure.md §6 — 4단계 대상. 입출고 공통 단일 클래스, 물품은 항상 1개씩만 이동.
UCLASS()
class AHorizontalTray : public AActor
{
	GENERATED_BODY()

public:
	AHorizontalTray();

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ETrayDirection Direction = ETrayDirection::Inbound;

	UPROPERTY(Replicated, BlueprintReadOnly)
	TWeakObjectPtr<ALogisticsItem> CurrentItem;

	UPROPERTY(Replicated, BlueprintReadOnly)
	bool bIsHaltedAtEnd = false;

	UPROPERTY(Replicated, BlueprintReadOnly)
	TWeakObjectPtr<AFactoryAgentBase> WorkZoneOccupant;

	bool TryReserveWorkZone(AFactoryAgentBase* Agent);
	void ReleaseWorkZone();

	void OnItemSpawnedAtStart(ALogisticsItem* Item);
	void OnItemPlacedByAtlas(ALogisticsItem* Item);
	void TickConveyance(float DeltaTime);
	void OnItemReachedEnd();
	void OnItemCleared();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;

private:
	// Docs에 없는 구현값: 컨베이어 이동 속도/길이
	UPROPERTY(EditAnywhere)
	float ConveySpeedUnitsPerSecond = 100.f;

	UPROPERTY(EditAnywhere)
	float TrayLength = 500.f;
};
