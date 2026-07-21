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

			// 버그 수정 — 원래 여기서 콜리전을 켰다(활성 아이템은 물리적으로 존재해야 한다는 의도). 그런데
			// 아이템은 컨베이어에 서 있는 동안이나 선반 슬롯에 최종적으로 놓인 뒤에도 계속 이 콜리전을
			// 들고 있고, 정작 그걸 끄는 코드(AttachHeldItem/OnItemGivenByAtlas)는 "아틀라스/로봇이 손에
			// 들고 있는 동안"만 커버해서 컨베이어 대기 중·선반 안착 후에는 다시 켜진 채로 남았다. 아틀라스가
			// 인접 슬롯에 이미 놓인 물품과 물리적으로 겹쳐 목적지에 도달하지 못하고(Blocked) 배정을
			// 통째로 포기하는 문제로 실제 재현됨. 이 프로젝트에서 아이템 콜리전이 필요한 지점이 없어
			// (물리 시뮬레이션으로 안착시키지 않고 항상 SetActorLocation으로 직접 스냅) 활성 상태에서도
			// 계속 꺼둔다 — 풀 상태(SpawnPool/ReturnItem)와 동일하게.
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
