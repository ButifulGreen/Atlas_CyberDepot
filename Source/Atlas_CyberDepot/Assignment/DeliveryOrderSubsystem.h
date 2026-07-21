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

	// Docs 이탈, 승인됨 — 외부업체 랜덤 주문 시스템(Docs/03_InventoryOrder.md)이 어느 업체의 요청인지 식별.
	UPROPERTY(BlueprintReadOnly)
	FName VendorName;
};

// Docs 이탈, 승인됨 — UWorldSubsystem(UDeliveryOrderSubsystem)은 DOREPLIFETIME을 못 써 클라이언트에
// ActiveOrders가 안 보인다. AMSmartFactoryManager(AGameStateBase)가 들고 있는 리플리케이트 표시 전용 사본.
// 실제 게임 로직(배차 등)은 계속 ActiveOrders를 참조하고, 이건 UI 렌더링에만 쓰인다.
USTRUCT(BlueprintType)
struct FVendorOrderDisplay
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName VendorName;

	UPROPERTY(BlueprintReadOnly)
	FGuid OrderID;

	UPROPERTY(BlueprintReadOnly)
	int32 QtyA = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 QtyB = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 QtyC = 0;

	// false면(Status != Available) UI가 전부 0으로 그린다 — RequestedQuantities 원본은 안 건드림.
	UPROPERTY(BlueprintReadOnly)
	bool bAvailable = false;
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

protected:
	// Docs 이탈, 승인됨 — 업체마다 독립적인 랜덤 타이머를 최초 예약. 다른 액터의 BeginPlay 결과물에
	// 의존하지 않으므로(UInventoryOrderSubsystem::OnWorldBeginPlay와 달리) 지연 없이 즉시 실행해도 안전하다.
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	// A/B/C 랜덤 수량으로 그 업체의 FDeliveryOrder를 새로 만들어(기존 항목 있으면 교체) ActiveOrders에 반영.
	void GenerateRandomVendorOrder(int32 VendorIndex);
	// 다음 실행을 MinOrderIntervalSeconds~MaxOrderIntervalSeconds 중 랜덤 간격으로 재예약.
	void ScheduleNextVendorOrder(int32 VendorIndex);
	// AMSmartFactoryManager::VendorOrderDisplays(클라이언트에 보이는 표시 전용 사본)를 최신 ActiveOrders로 갱신.
	void BroadcastVendorOrderDisplays();

	// FTimerHandle은 GC 추적이 필요 없는 순수 값 타입 — 다른 곳(FactoryAgentBase::DebugOperationCountTimerHandle)과
	// 동일하게 UPROPERTY 없이 둔다.
	TArray<FTimerHandle> VendorTimers;
};
