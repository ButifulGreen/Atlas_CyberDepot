// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assignment/InventoryOrderSubsystem.h"
#include "EventBus/FactoryEventBusSubsystem.h"

bool UInventoryOrderSubsystem::TryPlaceOrder(EItemType ItemType, int32 Quantity)
{
	const FStockLineState* Line = StockLines.Find(ItemType);
	if (!Line || Line->bIsLineLocked || Quantity <= 0)
	{
		return false;
	}

	// 입고 트레이에 실제로 물품을 스폰하는 연결(AHorizontalTray::OnItemSpawnedAtStart)은
	// 레벨의 인바운드 트레이-품목 매핑이 정해지는 이후 단계에서 채운다.
	return true;
}

void UInventoryOrderSubsystem::OnInboundArrived(EItemType ItemType, int32 Quantity)
{
	FStockLineState* Line = StockLines.Find(ItemType);
	if (!Line)
	{
		return;
	}

	Line->CurrentStock = FMath::Min(Line->CurrentStock + Quantity, Line->MaxCapacity);

	const bool bShouldLock = Line->CurrentStock >= Line->MaxCapacity;
	if (bShouldLock == Line->bIsLineLocked)
	{
		return;
	}

	Line->bIsLineLocked = bShouldLock;
	OnLineLockChanged.Broadcast(ItemType, Line->bIsLineLocked);

	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (GI)
	{
		if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
		{
			FAnomalyEvent Event;
			Event.Timestamp = FDateTime::UtcNow();
			Event.LogID = FGuid::NewGuid();
			Event.Severity = EEventSeverity::Warning;
			Event.AnomalyCode = TEXT("Code:004");
			EventBus->PublishAnomaly(Event);
		}
	}
}

bool UInventoryOrderSubsystem::IsLineLocked(EItemType ItemType) const
{
	const FStockLineState* Line = StockLines.Find(ItemType);
	return Line && Line->bIsLineLocked;
}
