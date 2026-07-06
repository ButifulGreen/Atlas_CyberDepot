// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EventBus/FactoryEventTypes.h"
#include "LogisticsItemSpawner.generated.h"

class ALogisticsItem;

// 5단계 신규 — 레벨 시작 시 물품 풀을 미리 스폰해두고(대기 상태: 숨김+콜리전 끔),
// 이후 실제 사용 시점에 위치만 옮겨(텔레포트) 재사용하는 방식(Docs에 없는 구현값, 실측 후 조정).
UCLASS()
class ALogisticsItemSpawner : public AActor
{
	GENERATED_BODY()

public:
	ALogisticsItemSpawner();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool")
	TSubclassOf<ALogisticsItem> ItemClass;

	// 타입(ItemA/B/C)당 스폰할 개수 — 선반 1개 용량(27칸)보다 여유 있게 30으로 시작, 밸런스 값.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Pool")
	int32 ItemsPerType = 30;

	// 대기 중(숨김+콜리전 꺼짐) 물품 중 해당 타입 하나를 꺼내 활성화한다. 없으면 nullptr.
	ALogisticsItem* TryAcquireItem(EItemType Type);

	// 다 쓴 물품을 다시 숨기고 콜리전을 끈 채 대기 위치로 되돌린다.
	void ReturnItem(ALogisticsItem* Item);

protected:
	virtual void BeginPlay() override;

private:
	void SpawnPool();

	UPROPERTY()
	TArray<TObjectPtr<ALogisticsItem>> PooledItems;
};
