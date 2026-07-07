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
	Expired,
	// 8단계 — 예약됐지만 아직 로봇이 배정되지 않은 주문의 취소 결과(Docs/02_Multiplayer_RPC.md)
	Cancelled
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

	// Docs에 없는 구현값 — 6단계 사이클 테스트용. 실제 주문 목록 갱신(RefreshOrderList)은 아직 신규 주문을
	// 생성하지 않아(품목/수량 랜덤화 규칙 미정, 후속 밸런싱 단계), 지정한 품목/수량으로 주문을 즉석 생성해
	// ActiveOrders에 추가한 뒤 바로 TryAcceptOrder까지 호출한다.
	UFUNCTION(BlueprintCallable)
	bool TryPlaceTestOrder(EItemType ItemType, int32 Quantity);

	bool TryAcceptOrder(const FGuid& OrderID);
	// 8단계 — Accepted 상태이면서 아직 로봇 배정 전인 주문만 취소 가능
	bool TryCancelOrder(const FGuid& OrderID);
	void OnOrderExpired(const FGuid& OrderID);
};
