// Copyright Epic Games, Inc. All Rights Reserved.

#include "Visualization/VendorOrderListWidget.h"
#include "Atlas_CyberDepot.h"
#include "Assignment/SmartFactoryManager.h"
#include "Infrastructure/FactoryKioskTerminal.h"
#include "Player/FactoryPlayerController.h"

void UVendorOrderListWidget::BindToManager(AMSmartFactoryManager* Manager)
{
	if (AMSmartFactoryManager* Previous = BoundManager.Get())
	{
		if (OrdersUpdatedHandle.IsValid())
		{
			Previous->OnVendorOrdersUpdated.Remove(OrdersUpdatedHandle);
		}
	}

	BoundManager = Manager;

	if (Manager)
	{
		OrdersUpdatedHandle = Manager->OnVendorOrdersUpdated.AddUObject(this, &UVendorOrderListWidget::RefreshFromManager);
	}

	RefreshFromManager();
}

void UVendorOrderListWidget::BindToKiosk(AFactoryKioskTerminal* Kiosk)
{
	BoundKiosk = Kiosk;
}

void UVendorOrderListWidget::AcceptOrder(FGuid OrderID)
{
	AFactoryKioskTerminal* Kiosk = BoundKiosk.Get();
	AFactoryPlayerController* PC = GetOwningPlayer<AFactoryPlayerController>();
	if (!Kiosk || !PC)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[VendorOrder] AcceptOrder(%s) 무시 — Kiosk=%s, PC=%s"),
			*OrderID.ToString(), Kiosk ? TEXT("있음") : TEXT("없음"), PC ? TEXT("있음") : TEXT("없음"));
		return;
	}

	FKioskOrderRequest Request;
	Request.RequestType = EOrderRequestType::OutboundApproval;
	Request.TargetOrderID = OrderID;
	PC->Server_SubmitKioskOrder(Kiosk, Request);
}

void UVendorOrderListWidget::SubmitInboundOrder(int32 QuantityA, int32 QuantityB, int32 QuantityC)
{
	AFactoryKioskTerminal* Kiosk = BoundKiosk.Get();
	AFactoryPlayerController* PC = GetOwningPlayer<AFactoryPlayerController>();
	if (!Kiosk || !PC)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[VendorOrder] SubmitInboundOrder(A:%d B:%d C:%d) 무시 — Kiosk=%s, PC=%s"),
			QuantityA, QuantityB, QuantityC, Kiosk ? TEXT("있음") : TEXT("없음"), PC ? TEXT("있음") : TEXT("없음"));
		return;
	}

	FKioskOrderRequest Request;
	Request.RequestType = EOrderRequestType::InboundBatch;
	Request.QuantityA = QuantityA;
	Request.QuantityB = QuantityB;
	Request.QuantityC = QuantityC;
	PC->Server_SubmitKioskOrder(Kiosk, Request);
}

void UVendorOrderListWidget::NativeDestruct()
{
	if (AMSmartFactoryManager* Manager = BoundManager.Get())
	{
		if (OrdersUpdatedHandle.IsValid())
		{
			Manager->OnVendorOrdersUpdated.Remove(OrdersUpdatedHandle);
		}
	}

	Super::NativeDestruct();
}

void UVendorOrderListWidget::RefreshFromManager()
{
	AMSmartFactoryManager* Manager = BoundManager.Get();
	DisplayedOrders = Manager ? Manager->VendorOrderDisplays : TArray<FVendorOrderDisplay>();
	BP_OnOrdersUpdated();
}
