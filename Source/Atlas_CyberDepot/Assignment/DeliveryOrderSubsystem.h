// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EventBus/FactoryEventTypes.h"
#include "DeliveryOrderSubsystem.generated.h"

// Docs/03_InventoryOrder.md에 값이 명시돼 있지 않아 TryAcceptOrder/OnOrderExpired에서 유추 가능한 최소 상태만 정의
UENUM(BlueprintType)
enum class EOrderStatus : uint8
{
	Available,
	Accepted,
	Completed,
	Expired
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDeliveryResult, const FGuid&, bool);

USTRUCT(BlueprintType)
struct FDeliveryOrder
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid OrderID;

	UPROPERTY(BlueprintReadOnly)
	TMap<EItemType, int32> RequestedQuantities;

	UPROPERTY(BlueprintReadOnly)
	FDateTime Deadline;

	UPROPERTY(BlueprintReadOnly)
	EOrderStatus Status = EOrderStatus::Available;
};

// Docs/03_InventoryOrder.md §3 — 6단계 대상.
UCLASS()
class UDeliveryOrderSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	TArray<FDeliveryOrder> ActiveOrders;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Order")
	float OrderRefreshIntervalSeconds = 60.f;

	FOnDeliveryResult OnDeliveryResult;

	void RefreshOrderList();
	bool TryAcceptOrder(const FGuid& OrderID);
	void OnOrderExpired(const FGuid& OrderID);
};
