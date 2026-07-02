// Copyright Epic Games, Inc. All Rights Reserved.

#include "Infrastructure/FactoryKioskTerminal.h"
#include "Components/BoxComponent.h"
#include "Assignment/InventoryOrderSubsystem.h"
#include "Assignment/DeliveryOrderSubsystem.h"

AFactoryKioskTerminal::AFactoryKioskTerminal()
{
	InteractCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractCollision"));
	InteractCollision->InitBoxExtent(FVector(50.f, 50.f, 100.f));
	SetRootComponent(InteractCollision);
}

bool ApplyKioskOrderRequest(UWorld* World, const FKioskOrderRequest& Request)
{
	if (!World)
	{
		return false;
	}

	switch (Request.RequestType)
	{
	case EOrderRequestType::Inbound:
		if (UInventoryOrderSubsystem* Inventory = World->GetSubsystem<UInventoryOrderSubsystem>())
		{
			return Inventory->TryPlaceOrder(Request.ItemType, Request.Quantity);
		}
		return false;

	case EOrderRequestType::OutboundApproval:
		if (UDeliveryOrderSubsystem* Delivery = World->GetSubsystem<UDeliveryOrderSubsystem>())
		{
			return Delivery->TryAcceptOrder(Request.TargetOrderID);
		}
		return false;

	case EOrderRequestType::Cancel:
		if (UDeliveryOrderSubsystem* Delivery = World->GetSubsystem<UDeliveryOrderSubsystem>())
		{
			return Delivery->TryCancelOrder(Request.TargetOrderID);
		}
		return false;
	}

	return false;
}
