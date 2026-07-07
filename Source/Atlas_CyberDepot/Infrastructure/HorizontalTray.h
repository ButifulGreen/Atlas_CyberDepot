// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "EventBus/FactoryEventTypes.h"
#include "HorizontalTray.generated.h"

class ALogisticsItem;
class AFactoryAgentBase;

UENUM(BlueprintType)
enum class ETrayDirection : uint8
{
	Inbound,
	Outbound
};

// Docs/06_Infrastructure.md §6 — 4단계 대상. 입출고 공통 단일 클래스, 물품은 항상 1개씩만 이동.
UCLASS()
class AHorizontalTray : public AActor
{
	GENERATED_BODY()

public:
	AHorizontalTray();

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ETrayDirection Direction = ETrayDirection::Inbound;

	// 5단계 신규 — AStorageShelf::BoundItemType과 동일한 패턴. 이 트레이가 담당하는 물품 종류.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EItemType BoundItemType = EItemType::ItemA;

	UPROPERTY(Replicated, BlueprintReadOnly)
	TWeakObjectPtr<ALogisticsItem> CurrentItem;

	UPROPERTY(Replicated, BlueprintReadOnly)
	bool bIsHaltedAtEnd = false;

	UPROPERTY(Replicated, BlueprintReadOnly)
	TWeakObjectPtr<AFactoryAgentBase> WorkZoneOccupant;

	// 버그 수정 — 아틀라스/배송로봇의 작업 위치는 물품이 실제로 멈춰서 상호작용 가능한 지점인
	// ItemEndMarker를 기준으로 계산해야 한다. 별도의 WorkMarker는 ItemEndMarker와 물리적으로
	// 동기화해야 하는 중복 컴포넌트였고, 레벨에서 둘을 다르게 배치하면 엉뚱한 위치로 이동하는
	// 버그가 생겨 제거했다.
	// 5단계 신규 — 물품이 처음 텔레포트되는 지점과, 컨베이어를 타고 이동해서 멈추는 지점은 서로 다른 지점이라 분리.
	UPROPERTY(VisibleAnywhere, Category = "Work Position")
	TObjectPtr<USceneComponent> ItemStartMarker;

	UPROPERTY(VisibleAnywhere, Category = "Work Position")
	TObjectPtr<USceneComponent> ItemEndMarker;

	// 마커에서 트레이 정면축 한 방향으로 각자의 거리만큼 뗀 위치. 선반과 달리 좌우 구분 없음(부호 반전 없음).
	// Docs에 없는 구현값 — 레벨 제작 후 실측 조정 필요.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|WorkPosition")
	float AtlasWorkDistance = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|WorkPosition")
	float TransportRobotWorkDistance = 300.f;

	FVector GetAtlasWorkLocation() const;
	FVector GetTransportRobotWorkLocation() const;

	bool TryReserveWorkZone(AFactoryAgentBase* Agent);
	void ReleaseWorkZone();

	void OnItemSpawnedAtStart(ALogisticsItem* Item);
	void OnItemPlacedByAtlas(ALogisticsItem* Item);
	void TickConveyance(float DeltaTime);
	void OnItemReachedEnd();
	void OnItemCleared();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;

private:
	FVector ComputeWorkLocation(float DepthOffset) const;

	// Docs에 없는 구현값: 컨베이어 이동 속도
	UPROPERTY(EditAnywhere)
	float ConveySpeedUnitsPerSecond = 100.f;
};
