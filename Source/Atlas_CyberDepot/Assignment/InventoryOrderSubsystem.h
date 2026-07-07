// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EventBus/FactoryEventTypes.h"
#include "InventoryOrderSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLineLockChanged, EItemType, bool);

USTRUCT(BlueprintType)
struct FStockLineState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EItemType ItemType = EItemType::ItemA;

	UPROPERTY(BlueprintReadOnly)
	int32 CurrentStock = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 MaxCapacity = 27;

	UPROPERTY(BlueprintReadOnly)
	bool bIsLineLocked = false;
};

// Docs/03_InventoryOrder.md §3 — 6단계 대상.
UCLASS()
class UInventoryOrderSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	TMap<EItemType, FStockLineState> StockLines;

	FOnLineLockChanged OnLineLockChanged;

	UFUNCTION(BlueprintCallable)
	bool TryPlaceOrder(EItemType ItemType, int32 Quantity);
	void OnInboundArrived(EItemType ItemType, int32 Quantity);
	bool IsLineLocked(EItemType ItemType) const;

protected:
	// 6단계 신규 — StockLines를 채우는 코드가 없어 TryPlaceOrder가 항상 실패하던 누락 보완.
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
