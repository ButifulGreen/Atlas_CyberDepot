// Copyright Epic Games, Inc. All Rights Reserved.

#include "Infrastructure/LogisticsItemSpawner.h"
#include "Infrastructure/LogisticsItem.h"

ALogisticsItemSpawner::ALogisticsItemSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ALogisticsItemSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		SpawnPool();
	}
}

void ALogisticsItemSpawner::SpawnPool()
{
	UWorld* World = GetWorld();
	if (!World || !ItemClass)
	{
		return;
	}

	constexpr EItemType AllTypes[] = { EItemType::ItemA, EItemType::ItemB, EItemType::ItemC };
	const FTransform PoolTransform(GetActorRotation(), GetActorLocation());

	for (EItemType Type : AllTypes)
	{
		for (int32 Index = 0; Index < ItemsPerType; ++Index)
		{
			// Deferred 스폰 — ItemType을 BeginPlay(메시 갱신) 이전에 확정해야 처음부터 올바른 메시가 보인다.
			ALogisticsItem* NewItem = World->SpawnActorDeferred<ALogisticsItem>(ItemClass, PoolTransform);
			if (!NewItem)
			{
				continue;
			}

			NewItem->ItemType = Type;
			NewItem->FinishSpawning(PoolTransform);

			NewItem->SetActorHiddenInGame(true);
			NewItem->SetActorEnableCollision(false);

			PooledItems.Add(NewItem);
		}
	}
}

ALogisticsItem* ALogisticsItemSpawner::TryAcquireItem(EItemType Type)
{
	for (ALogisticsItem* Item : PooledItems)
	{
		if (Item && Item->ItemType == Type && Item->IsHidden())
		{
			Item->SetActorHiddenInGame(false);
			Item->SetActorEnableCollision(true);
			return Item;
		}
	}

	return nullptr;
}

void ALogisticsItemSpawner::ReturnItem(ALogisticsItem* Item)
{
	if (!Item)
	{
		return;
	}

	Item->SetActorHiddenInGame(true);
	Item->SetActorEnableCollision(false);
	Item->SetActorLocation(GetActorLocation());
}
