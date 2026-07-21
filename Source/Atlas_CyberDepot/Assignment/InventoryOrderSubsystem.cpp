// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assignment/InventoryOrderSubsystem.h"
#include "Atlas_CyberDepot.h"
#include "Assignment/OutboundDispatchSubsystem.h"
#include "Assignment/SmartFactoryManager.h"
#include "EventBus/FactoryEventBusSubsystem.h"
#include "Infrastructure/HorizontalTray.h"
#include "Infrastructure/LogisticsItem.h"
#include "Infrastructure/LogisticsItemSpawner.h"
#include "Infrastructure/StorageShelf.h"
#include "Kismet/GameplayStatics.h"

void UInventoryOrderSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.GetNetMode() == NM_Client)
	{
		return;
	}

	for (uint8 Index = 0; Index <= static_cast<uint8>(EItemType::ItemC); ++Index)
	{
		const EItemType ItemType = static_cast<EItemType>(Index);
		if (!StockLines.Contains(ItemType))
		{
			FStockLineState Line;
			Line.ItemType = ItemType;
			StockLines.Add(ItemType, Line);
		}
	}
}

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

	AMSmartFactoryManager* Manager = World->GetGameState<AMSmartFactoryManager>();
	if (!Manager)
	{
		return true;
	}

	// Docs 이탈, 승인됨 — 전역 재주문 쿨다운("차량 연계" 이전까지의 임시 제약). 품목 무관 단일 타이머.
	const FDateTime Now = FDateTime::UtcNow();
	if ((Now - LastOrderTimestamp).GetTotalSeconds() < Manager->ReorderCooldownSeconds)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Economy] 주문 실패 — 재주문 쿨다운 %.1f초 남음"), GetRemainingCooldownSeconds());
		return false;
	}

	const int32 Cost = Manager->GetUnitPrice(ItemType) * Quantity;
	if (!Manager->TryAdjustFunds(-static_cast<float>(Cost), TEXT("InboundOrder")))
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Economy] 주문 실패 — 자금 부족(필요 %d)"), Cost);
		return false;
	}

	LastOrderTimestamp = Now;
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Economy] 주문 성공 — %s x%d, 비용 -%d(잔액 %.0f)"),
		*UEnum::GetValueAsString(ItemType), Quantity, Cost, Manager->SharedFunds);

	// 버그 수정(대기열 신설) — 전체 Quantity를 대기열에 실어두고 지금 즉시 1개 인출을 시도한다.
	// 트레이가 비어있으면 바로 올라가고, 점유 중이면 남아서 OnInboundTrayCleared가 이어받는다.
	PendingInboundQuantities.FindOrAdd(ItemType) += Quantity;
	TryDrainInboundBacklog(World, ItemType);
	return true;
}

bool UInventoryOrderSubsystem::DebugForcePlaceOrder(EItemType ItemType, int32 Quantity)
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

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Economy][Debug] 강제 주문 — %s x%d(비용/쿨다운 무시)"),
		*UEnum::GetValueAsString(ItemType), Quantity);

	PendingInboundQuantities.FindOrAdd(ItemType) += Quantity;
	TryDrainInboundBacklog(World, ItemType);
	return true;
}

bool UInventoryOrderSubsystem::TryPlaceItemOnInboundTray(UWorld* World, EItemType ItemType)
{
	UOutboundDispatchSubsystem* Dispatch = World->GetSubsystem<UOutboundDispatchSubsystem>();
	if (!Dispatch)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Economy][Queue] %s 스폰 실패 — UOutboundDispatchSubsystem을 못 찾음"),
			*UEnum::GetValueAsString(ItemType));
		return false;
	}

	// 디버그 편의 — "트레이가 없음"(구조적, 레벨 재확인 필요)과 "트레이가 점유 중"(정상, 곧 풀림)을
	// 같은 로그로 뭉뚱그리면 테스트 중 원인 파악이 늦어진다. 분리해서 각자 다른 로그를 남긴다.
	AHorizontalTray* InboundTray = Dispatch->FindTrayForItemType(ItemType, ETrayDirection::Inbound);
	if (!InboundTray)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Economy][Queue] %s 담당 Inbound 트레이가 레벨에 없음(구조적 문제) — 스폰 보류. BoundItemType=%s인 Inbound 트레이가 있는지 확인 필요"),
			*UEnum::GetValueAsString(ItemType), *UEnum::GetValueAsString(ItemType));
		return false;
	}

	if (InboundTray->CurrentItem.IsValid())
	{
		// 정상적으로 발생하는 대기 상태(누군가 아직 안 가져감) — 호출부(TryDrainInboundBacklog)가 대기 로그를 남긴다.
		return false;
	}

	// 버그 수정(재검토) — 담당 선반이 아예 없으면 물품을 올려도 아무도 안 가져가 트레이가 그 즉시
	// 영구 점유되고 대기열 전체가 막힌다. 스폰하기 전에 먼저 확인해 헛되이 트레이를 막지 않는다.
	AStorageShelf* TargetShelf = Dispatch->FindShelfForItemType(ItemType);
	if (!TargetShelf)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Economy][Queue] %s 담당 선반이 레벨에 없음(구조적 문제) — 스폰하면 트레이가 영구 점유되므로 보류. BoundItemType=%s인 선반이 있는지 확인 필요"),
			*UEnum::GetValueAsString(ItemType), *UEnum::GetValueAsString(ItemType));
		return false;
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
			Dispatch->EnqueueInboundWork(ItemType, InboundTray, TargetShelf);
			UE_LOG(LogFactoryDispatch, Log, TEXT("[Economy][Queue] %s 스폰 성공 — %s에 적재, %s행 작업 생성"),
				*UEnum::GetValueAsString(ItemType), *InboundTray->GetName(), *TargetShelf->GetName());
			return true;
		}
	}

	UE_LOG(LogFactoryDispatch, Warning, TEXT("[Economy][Queue] %s 스폰 실패 — 모든 ALogisticsItemSpawner 풀 소진(TryAcquireItem 전부 실패)"),
		*UEnum::GetValueAsString(ItemType));
	return false;
}

void UInventoryOrderSubsystem::TryDrainInboundBacklog(UWorld* World, EItemType ItemType)
{
	int32* Pending = PendingInboundQuantities.Find(ItemType);
	if (!Pending || *Pending <= 0)
	{
		return;
	}

	if (!TryPlaceItemOnInboundTray(World, ItemType))
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Economy][Queue] %s 인출 보류 — 대기 %d개(구체적 사유는 바로 위 로그 참고, 트레이가 비면 자동 재시도)"),
			*UEnum::GetValueAsString(ItemType), *Pending);
		return;
	}

	--(*Pending);
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Economy][Queue] %s 대기열에서 1개 인출 완료 — 남은 대기 %d개"),
		*UEnum::GetValueAsString(ItemType), *Pending);
}

void UInventoryOrderSubsystem::OnInboundTrayCleared(EItemType ItemType)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Economy][Queue] %s OnInboundTrayCleared 실패 — World가 없음"), *UEnum::GetValueAsString(ItemType));
		return;
	}

	TryDrainInboundBacklog(World, ItemType);
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

float UInventoryOrderSubsystem::GetRemainingCooldownSeconds() const
{
	const UWorld* World = GetWorld();
	const AMSmartFactoryManager* Manager = World ? World->GetGameState<AMSmartFactoryManager>() : nullptr;
	if (!Manager)
	{
		return 0.f;
	}

	const double Elapsed = (FDateTime::UtcNow() - LastOrderTimestamp).GetTotalSeconds();
	return static_cast<float>(FMath::Max(0.0, static_cast<double>(Manager->ReorderCooldownSeconds) - Elapsed));
}
