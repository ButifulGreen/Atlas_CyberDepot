// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assignment/DeliveryOrderSubsystem.h"
#include "Assignment/OutboundDispatchSubsystem.h"

void UDeliveryOrderSubsystem::RefreshOrderList()
{
	const FDateTime Now = FDateTime::UtcNow();

	for (FDeliveryOrder& Order : ActiveOrders)
	{
		if (Order.Status == EOrderStatus::Available && Order.Deadline <= Now)
		{
			Order.Status = EOrderStatus::Expired;
			OnOrderExpired(Order.OrderID);
		}
	}

	// 신규 주문 생성 규칙(품목/수량 랜덤화 등)은 밸런싱과 함께 정해지는 이후 단계에서 채운다.
}

bool UDeliveryOrderSubsystem::TryAcceptOrder(const FGuid& OrderID)
{
	FDeliveryOrder* Order = ActiveOrders.FindByPredicate([&OrderID](const FDeliveryOrder& O)
	{
		return O.OrderID == OrderID;
	});

	if (!Order || Order->Status != EOrderStatus::Available)
	{
		return false;
	}

	Order->Status = EOrderStatus::Accepted;

	if (UOutboundDispatchSubsystem* Dispatch = GetWorld() ? GetWorld()->GetSubsystem<UOutboundDispatchSubsystem>() : nullptr)
	{
		Dispatch->DecomposeOrder(*Order);
	}

	return true;
}

void UDeliveryOrderSubsystem::OnOrderExpired(const FGuid& OrderID)
{
	OnDeliveryResult.Broadcast(OrderID, false);
}
