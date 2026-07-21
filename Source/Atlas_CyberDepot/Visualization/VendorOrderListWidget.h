// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Assignment/DeliveryOrderSubsystem.h"
#include "VendorOrderListWidget.generated.h"

class AMSmartFactoryManager;
class AFactoryKioskTerminal;

// Docs/09_Visualization.md §9 — 외부업체 랜덤 주문 표시 + 수락. Docs/03_InventoryOrder.md의
// UDeliveryOrderSubsystem::ActiveOrders는 UWorldSubsystem이라 클라이언트에 직접 안 보이므로,
// AMSmartFactoryManager::VendorOrderDisplays(리플리케이트 표시 전용 사본)를 구독한다.
UCLASS()
class UVendorOrderListWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	TArray<FVendorOrderDisplay> DisplayedOrders;

	// AMSmartFactoryManager::OnVendorOrdersUpdated 구독 시작 + 현재 값으로 즉시 1회 갱신(BP_OnOrdersUpdated
	// 콜백이 이 호출 안에서 바로 1회 실행됨 — 별도로 다시 부를 필요 없음).
	UFUNCTION(BlueprintCallable, Category = "VendorOrder")
	void BindToManager(AMSmartFactoryManager* Manager);

	// Server_SubmitKioskOrder의 거리 체크(KioskInteractRadius)를 통과하려면 로컬 플레이어가 실제로 근접해
	// 있어야 하는 키오스크 액터를 지정한다(관제실 키오스크 등, 이 위젯을 배치한 곳 근처의 것).
	UFUNCTION(BlueprintCallable, Category = "VendorOrder")
	void BindToKiosk(AFactoryKioskTerminal* Kiosk);

	// 수락 버튼(BP)이 호출 — EOrderRequestType::OutboundApproval로 기존 RPC 경로를 그대로 태운다.
	// BoundKiosk가 없거나 로컬 플레이어가 근처에 없으면 서버에서 조용히 실패한다(Server_SubmitKioskOrder 그대로).
	UFUNCTION(BlueprintCallable, Category = "VendorOrder")
	void AcceptOrder(FGuid OrderID);

protected:
	virtual void NativeDestruct() override;

	// 실제 UI 갱신(행 다시 그리기 등)은 BP 서브클래스가 구현 — UAgentStatusIndicatorWidget과 동일한 패턴.
	UFUNCTION(BlueprintImplementableEvent, Category = "VendorOrder")
	void BP_OnOrdersUpdated();

private:
	void RefreshFromManager();

	UPROPERTY()
	TWeakObjectPtr<AMSmartFactoryManager> BoundManager;

	UPROPERTY()
	TWeakObjectPtr<AFactoryKioskTerminal> BoundKiosk;

	// FDelegateHandle은 GC 추적이 필요 없는 값 타입이라 UPROPERTY 없이 둔다.
	FDelegateHandle OrdersUpdatedHandle;
};
