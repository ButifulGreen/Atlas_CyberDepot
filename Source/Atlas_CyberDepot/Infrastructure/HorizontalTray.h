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

	// 버그 수정 — GetTransportRobotWorkLocation()은 트레이당 좌표 하나뿐인데(슬롯처럼 여러 개가 아님)
	// 배송로봇 쪽엔 이 지점에 대한 점유 예약이 없었다. 물량이 몰려 같은 트레이로 트립이 연달아 들어오면
	// 두 번째 배송로봇이 첫 번째가 서있는 지점으로 그대로 이동을 시도해 길찾기가 영구 차단될 수 있었다.
	UPROPERTY(Replicated, BlueprintReadOnly)
	TWeakObjectPtr<AFactoryAgentBase> TransportRobotWorkZoneOccupant;

	// 버그 수정 — 아틀라스/배송로봇의 작업 위치는 물품이 실제로 멈춰서 상호작용 가능한 지점인
	// ItemEndMarker를 기준으로 계산해야 한다. 별도의 WorkMarker는 ItemEndMarker와 물리적으로
	// 동기화해야 하는 중복 컴포넌트였고, 레벨에서 둘을 다르게 배치하면 엉뚱한 위치로 이동하는
	// 버그가 생겨 제거했다.
	// 5단계 신규 — 물품이 처음 텔레포트되는 지점과, 컨베이어를 타고 이동해서 멈추는 지점은 서로 다른 지점이라 분리.
	UPROPERTY(VisibleAnywhere, Category = "Work Position")
	TObjectPtr<USceneComponent> ItemStartMarker;

	UPROPERTY(VisibleAnywhere, Category = "Work Position")
	TObjectPtr<USceneComponent> ItemEndMarker;

	// 마커 기준 작업 위치 = 정면축(Forward) 오프셋(Distance) + 좌우축(Right) 오프셋(LateralOffset).
	// 버그 수정 — 원래 정면축 거리 하나뿐이라 아틀라스/배송로봇이 좌우로 벌어질 방법이 없었다. 둘이
	// 같은 축 선상에 너무 가깝게 위치하면 내비게이션 충돌 회피가 끼어들어 로봇이 정확한 목표 지점에
	// 도달하지 못하고(Blocked 재시도) 도착 판정 자체가 간헐적으로 안 나는 문제가 있었다 — 좌우 오프셋으로
	// 서로 자리를 벌려 물리적 간섭을 줄인다. Docs에 없는 구현값 — 레벨 제작 후 실측 조정 필요.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|WorkPosition")
	float AtlasWorkDistance = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|WorkPosition")
	float AtlasWorkLateralOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|WorkPosition")
	float TransportRobotWorkDistance = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|WorkPosition")
	float TransportRobotWorkLateralOffset = 0.f;

	FVector GetAtlasWorkLocation() const;
	FVector GetTransportRobotWorkLocation() const;

	bool TryReserveWorkZone(AFactoryAgentBase* Agent);
	void ReleaseWorkZone();

	bool TryReserveTransportRobotWorkZone(AFactoryAgentBase* Agent);
	void ReleaseTransportRobotWorkZone();

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
	FVector ComputeWorkLocation(float DepthOffset, float LateralOffset) const;

	// Docs에 없는 구현값: 컨베이어 이동 속도
	UPROPERTY(EditAnywhere)
	float ConveySpeedUnitsPerSecond = 100.f;
};
