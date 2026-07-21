// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assignment/DeliveryOrderSubsystem.h"
#include "Atlas_CyberDepot.h"
#include "Assignment/OutboundDispatchSubsystem.h"
#include "Assignment/SmartFactoryManager.h"
#include "TimerManager.h"

void UDeliveryOrderSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.GetNetMode() == NM_Client)
	{
		return;
	}

	const AMSmartFactoryManager* Manager = InWorld.GetGameState<AMSmartFactoryManager>();
	if (!Manager)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Economy] 외부업체 초기화 실패 — AMSmartFactoryManager(GameState)를 아직 못 찾음"));
		return;
	}

	VendorTimers.SetNum(Manager->VendorNames.Num());
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Economy] 외부업체 %d개 초기화 — 각자 독립 랜덤 타이머 예약"), Manager->VendorNames.Num());
	for (int32 Index = 0; Index < Manager->VendorNames.Num(); ++Index)
	{
		ScheduleNextVendorOrder(Index);
	}
}

void UDeliveryOrderSubsystem::ScheduleNextVendorOrder(int32 VendorIndex)
{
	UWorld* World = GetWorld();
	const AMSmartFactoryManager* Manager = World ? World->GetGameState<AMSmartFactoryManager>() : nullptr;
	if (!Manager || !VendorTimers.IsValidIndex(VendorIndex))
	{
		return;
	}

	const float Delay = FMath::RandRange(Manager->MinOrderIntervalSeconds, Manager->MaxOrderIntervalSeconds);
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Economy] %s 다음 주문까지 %.1f초"),
		Manager->VendorNames.IsValidIndex(VendorIndex) ? *Manager->VendorNames[VendorIndex].ToString() : TEXT("Unknown"), Delay);
	FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &UDeliveryOrderSubsystem::GenerateRandomVendorOrder, VendorIndex);
	World->GetTimerManager().SetTimer(VendorTimers[VendorIndex], Delegate, Delay, false);
}

void UDeliveryOrderSubsystem::GenerateRandomVendorOrder(int32 VendorIndex)
{
	UWorld* World = GetWorld();
	AMSmartFactoryManager* Manager = World ? World->GetGameState<AMSmartFactoryManager>() : nullptr;
	if (!Manager || !Manager->VendorNames.IsValidIndex(VendorIndex))
	{
		return;
	}

	const FName VendorName = Manager->VendorNames[VendorIndex];

	FDeliveryOrder NewOrder;
	NewOrder.OrderID = FGuid::NewGuid();
	NewOrder.VendorName = VendorName;
	NewOrder.Deadline = FDateTime::UtcNow() + FTimespan::FromSeconds(Manager->MaxOrderIntervalSeconds);
	NewOrder.Status = EOrderStatus::Available;
	NewOrder.RequestedQuantities.Add(EItemType::ItemA, FMath::RandRange(Manager->MinQuantityPerItem, Manager->MaxQuantityPerItem));
	NewOrder.RequestedQuantities.Add(EItemType::ItemB, FMath::RandRange(Manager->MinQuantityPerItem, Manager->MaxQuantityPerItem));
	NewOrder.RequestedQuantities.Add(EItemType::ItemC, FMath::RandRange(Manager->MinQuantityPerItem, Manager->MaxQuantityPerItem));

	// 같은 업체의 기존 항목이 있으면 교체 — 간단한 구조 유지(이전 미수락 주문은 덮어씀, 사용자 확정 사항).
	FDeliveryOrder* Existing = ActiveOrders.FindByPredicate([VendorName](const FDeliveryOrder& O)
	{
		return O.VendorName == VendorName;
	});

	if (Existing)
	{
		*Existing = NewOrder;
	}
	else
	{
		ActiveOrders.Add(NewOrder);
	}

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Economy] %s 신규 주문 생성(%s) — A:%d B:%d C:%d"),
		*VendorName.ToString(), *NewOrder.OrderID.ToString(),
		NewOrder.RequestedQuantities[EItemType::ItemA], NewOrder.RequestedQuantities[EItemType::ItemB], NewOrder.RequestedQuantities[EItemType::ItemC]);

	BroadcastVendorOrderDisplays();
	ScheduleNextVendorOrder(VendorIndex);
}

void UDeliveryOrderSubsystem::BroadcastVendorOrderDisplays()
{
	UWorld* World = GetWorld();
	if (AMSmartFactoryManager* Manager = World ? World->GetGameState<AMSmartFactoryManager>() : nullptr)
	{
		Manager->UpdateVendorOrderDisplays(ActiveOrders);
	}
}

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

bool UDeliveryOrderSubsystem::TryPlaceTestOrder(EItemType ItemType, int32 Quantity)
{
	if (Quantity <= 0)
	{
		return false;
	}

	FDeliveryOrder NewOrder;
	NewOrder.OrderID = FGuid::NewGuid();
	NewOrder.RequestedQuantities.Add(ItemType, Quantity);
	NewOrder.Deadline = FDateTime::UtcNow() + FTimespan::FromSeconds(OrderRefreshIntervalSeconds);
	NewOrder.Status = EOrderStatus::Available;

	ActiveOrders.Add(NewOrder);
	return TryAcceptOrder(NewOrder.OrderID);
}

bool UDeliveryOrderSubsystem::TryAcceptOrder(const FGuid& OrderID)
{
	FDeliveryOrder* Order = ActiveOrders.FindByPredicate([&OrderID](const FDeliveryOrder& O)
	{
		return O.OrderID == OrderID;
	});

	if (!Order)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Economy] 주문 수락 실패 — OrderID(%s)를 찾을 수 없음"), *OrderID.ToString());
		return false;
	}

	if (Order->Status != EOrderStatus::Available)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Economy] %s 주문(%s) 수락 실패 — 이미 처리됨(상태=%s)"),
			*Order->VendorName.ToString(), *OrderID.ToString(), *UEnum::GetValueAsString(Order->Status));
		return false;
	}

	Order->Status = EOrderStatus::Accepted;

	UWorld* World = GetWorld();
	if (UOutboundDispatchSubsystem* Dispatch = World ? World->GetSubsystem<UOutboundDispatchSubsystem>() : nullptr)
	{
		Dispatch->DecomposeOrder(*Order);
	}

	// Docs 이탈, 승인됨 — 외부업체 출고 주문 수락 시 품목별 판매가 합산 수익을 공용 자금에 반영.
	AMSmartFactoryManager* Manager = World ? World->GetGameState<AMSmartFactoryManager>() : nullptr;
	int32 Total = 0;
	if (Manager)
	{
		for (const TPair<EItemType, int32>& Pair : Order->RequestedQuantities)
		{
			Total += Manager->GetSellPrice(Pair.Key) * Pair.Value;
		}
		Manager->TryAdjustFunds(static_cast<float>(Total), TEXT("OutboundOrderAccepted"));
	}

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Economy] %s 주문(%s) 수락 — 수익 +%d, 로봇 배차 시작"),
		*Order->VendorName.ToString(), *OrderID.ToString(), Total);

	BroadcastVendorOrderDisplays();
	return true;
}

bool UDeliveryOrderSubsystem::TryCancelOrder(const FGuid& OrderID)
{
	FDeliveryOrder* Order = ActiveOrders.FindByPredicate([&OrderID](const FDeliveryOrder& O)
	{
		return O.OrderID == OrderID;
	});

	if (!Order || Order->Status != EOrderStatus::Accepted)
	{
		return false;
	}

	UOutboundDispatchSubsystem* Dispatch = GetWorld() ? GetWorld()->GetSubsystem<UOutboundDispatchSubsystem>() : nullptr;
	if (!Dispatch || !Dispatch->TryCancelAssignmentsForOrder(OrderID))
	{
		return false;
	}

	Order->Status = EOrderStatus::Cancelled;
	BroadcastVendorOrderDisplays();
	return true;
}

void UDeliveryOrderSubsystem::OnOrderExpired(const FGuid& OrderID)
{
	OnDeliveryResult.Broadcast(OrderID, false);
	BroadcastVendorOrderDisplays();
}
