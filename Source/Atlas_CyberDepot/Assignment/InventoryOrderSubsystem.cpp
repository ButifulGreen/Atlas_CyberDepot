// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assignment/InventoryOrderSubsystem.h"
#include "Assignment/OutboundDispatchSubsystem.h"
#include "EventBus/FactoryEventBusSubsystem.h"
#include "Infrastructure/HorizontalTray.h"
#include "Infrastructure/LogisticsItem.h"
#include "Infrastructure/LogisticsItemSpawner.h"
#include "Infrastructure/StorageShelf.h"
#include "Kismet/GameplayStatics.h"

bool UInventoryOrderSubsystem::TryPlaceOrder(EItemType ItemType, int32 Quantity)
{
	const FStockLineState* Line = StockLines.Find(ItemType);
	if (!Line || Line->bIsLineLocked || Quantity <= 0)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	// 해당 품목을 담당하는 입고 트레이를 찾는다 — 비어있지 않으면 지금은 못 올리고 다음 기회에 재시도한다
	// (Quantity가 1보다 큰 경우 나머지 수량을 이어서 흘려보내는 대기열은 아직 없음, 후속 과제).
	AHorizontalTray* InboundTray = nullptr;
	TArray<AActor*> FoundTrays;
	UGameplayStatics::GetAllActorsOfClass(World, AHorizontalTray::StaticClass(), FoundTrays);
	for (AActor* Actor : FoundTrays)
	{
		AHorizontalTray* Tray = Cast<AHorizontalTray>(Actor);
		if (Tray && Tray->Direction == ETrayDirection::Inbound && Tray->BoundItemType == ItemType)
		{
			InboundTray = Tray;
			break;
		}
	}

	if (!InboundTray || InboundTray->CurrentItem.IsValid())
	{
		return true;
	}

	TArray<AActor*> FoundSpawners;
	UGameplayStatics::GetAllActorsOfClass(World, ALogisticsItemSpawner::StaticClass(), FoundSpawners);
	for (AActor* Actor : FoundSpawners)
	{
		ALogisticsItemSpawner* Spawner = Cast<ALogisticsItemSpawner>(Actor);
		ALogisticsItem* Item = Spawner ? Spawner->TryAcquireItem(ItemType) : nullptr;
		if (Item)
		{
			InboundTray->OnItemSpawnedAtStart(Item);

			// 6단계 신규 — 트레이에 올린 물품을 선반까지 옮길 아틀라스/배송로봇 작업을 생성한다.
			AStorageShelf* TargetShelf = nullptr;
			TArray<AActor*> FoundShelves;
			UGameplayStatics::GetAllActorsOfClass(World, AStorageShelf::StaticClass(), FoundShelves);
			for (AActor* ShelfActor : FoundShelves)
			{
				if (AStorageShelf* Shelf = Cast<AStorageShelf>(ShelfActor); Shelf && Shelf->BoundItemType == ItemType)
				{
					TargetShelf = Shelf;
					break;
				}
			}

			if (TargetShelf)
			{
				if (UOutboundDispatchSubsystem* Dispatch = World->GetSubsystem<UOutboundDispatchSubsystem>())
				{
					Dispatch->EnqueueInboundWork(ItemType, InboundTray, TargetShelf);
				}
			}
			break;
		}
	}

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
