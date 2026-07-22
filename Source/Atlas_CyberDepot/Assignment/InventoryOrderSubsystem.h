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

	// 금액 산정 시스템 신규 — 플레이어 주문 UI 전용. A/B/C 수량을 한 번에 제출해 쿨다운/자금 체크를 합산 1회만
	// 수행한다(EOrderRequestType::InboundBatch, Docs/03_InventoryOrder.md 참고). 수량이 0 이하이거나 해당
	// 재고 라인이 잠겨있는 품목은 조용히 건너뛴다 — 나머지 품목은 정상 처리된다.
	UFUNCTION(BlueprintCallable)
	bool TryPlaceBatchOrder(int32 QuantityA, int32 QuantityB, int32 QuantityC);

	// Docs에 없는 구현값 — UDeliveryOrderSubsystem::TryPlaceTestOrder와 동일한 취지의 입고 쪽 테스트용.
	// 비용/쿨다운을 건너뛰고 물리적으로 즉시 적재한다 — 여러 품목을 한 프레임에 연달아 호출해도(전역 쿨다운에
	// 안 걸리므로) 전부 성공한다. 재고 잠금(bIsLineLocked)만은 그대로 체크.
	UFUNCTION(BlueprintCallable, Category = "Debug")
	bool DebugForcePlaceOrder(EItemType ItemType, int32 Quantity);

	void OnInboundArrived(EItemType ItemType, int32 Quantity);
	bool IsLineLocked(EItemType ItemType) const;

	// UI가 재주문 가능 시점을 표시할 때 사용(초 단위, 이미 가능하면 0). 쿨다운 기준값은
	// AMSmartFactoryManager::ReorderCooldownSeconds(에디터에서 조정 가능한 자리로 이전, 아래 참고).
	float GetRemainingCooldownSeconds() const;

	// 버그 수정(대기열 신설) — Quantity>1 주문 중 입고 트레이가 점유돼 즉시 못 올린 나머지 수량.
	// 품목별로 누적되며, 트레이가 빌 때마다(OnInboundTrayCleared) 1개씩 이어서 흘려보낸다.
	UPROPERTY(BlueprintReadOnly)
	TMap<EItemType, int32> PendingInboundQuantities;

	// AFactoryAtlasRobot::TransferItem이 입고(Inbound) 트레이에서 물품을 집어가 트레이가 빈 직후 호출.
	// 이 품목의 대기열이 있으면 즉시 다음 1개를 이어서 올린다.
	void OnInboundTrayCleared(EItemType ItemType);

protected:
	// 6단계 신규 — StockLines를 채우는 코드가 없어 TryPlaceOrder가 항상 실패하던 누락 보완.
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	// TryPlaceOrder/DebugForcePlaceOrder/TryDrainInboundBacklog 공용 — 입고 트레이가 비어있으면 물품을
	// 실제로 올리고 선반행 작업을 생성한다. 점유 중이면 아무것도 안 하고 false(호출부가 대기열에 남긴다).
	bool TryPlaceItemOnInboundTray(UWorld* World, EItemType ItemType);
	// PendingInboundQuantities에 이 품목의 대기 수량이 있으면 인출을 1회 시도(성공 시 카운트 차감).
	void TryDrainInboundBacklog(UWorld* World, EItemType ItemType);

	// Docs 이탈, 승인됨 — ItemPriceTable/ReorderCooldownSeconds는 원래 여기 EditAnywhere로 있었으나,
	// UWorldSubsystem은 BP 서브클래스를 만들어도 GetSubsystem<UInventoryOrderSubsystem>()이 항상 네이티브
	// 인스턴스를 반환해(SubsystemCollection.cpp — concrete 클래스마다 별도 인스턴스 생성) 에디터에서 바꾼 값이
	// 실제 게임에 반영되지 않는 문제가 있었다. AMSmartFactoryManager(AGameStateBase, BP 서브클래스가 그대로
	// 스폰되는 단일 인스턴스)로 이전. LastOrderTimestamp(런타임 상태)만 여기 그대로 둔다.
	FDateTime LastOrderTimestamp;
};
