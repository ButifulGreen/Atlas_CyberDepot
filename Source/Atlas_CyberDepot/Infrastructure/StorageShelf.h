// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "EventBus/FactoryEventTypes.h"
#include "StorageShelf.generated.h"

class ALogisticsItem;
class AFactoryAgentBase;

// Docs/07_TaskAssignment.md §7에서 정의되지만 TransferZoneOccupancy가 참조하므로 여기서 함께 정의
UENUM(BlueprintType)
enum class EWorkZoneType : uint8
{
	ShelfInboundZone,
	ShelfOutboundZone,
	TrayWorkZone
};

USTRUCT(BlueprintType)
struct FShelfSlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TWeakObjectPtr<ALogisticsItem> OccupyingItem;

	UPROPERTY(BlueprintReadOnly)
	FDateTime EnteredTimestamp;

	UPROPERTY(BlueprintReadOnly)
	bool bReservedForInbound = false;

	UPROPERTY(BlueprintReadOnly)
	bool bReservedForOutbound = false;
};

// 5단계 신규 — 27개(3층×9칸) 물품 슬롯 위치를 BP에서 각각 손으로 배치하기 위한 마커.
// 배열 순서가 아니라 이 컴포넌트 자신이 들고 있는 FloorIndex/SlotIndex로 식별해서,
// BP에서 몇 개를 어떤 순서로 추가하든 안전하게 매칭한다.
UCLASS(ClassGroup = (Infrastructure), meta = (BlueprintSpawnableComponent))
class UStorageSlotMarkerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage Slot")
	int32 FloorIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage Slot")
	int32 SlotIndex = 0;
};

// Docs/06_Infrastructure.md §6 — 4단계 대상. 기차형 좌/우 분리 구조, 3층×9칸 고정.
// 좌/우 구역 예약 함수는 문서상 AFactoryAtlasRobot*이지만 그 클래스가 5단계에 있어
// 지금은 멤버(InboundZoneOccupant 등)와 동일한 AFactoryAgentBase*로 받는다.
UCLASS()
class AStorageShelf : public AActor
{
	GENERATED_BODY()

public:
	AStorageShelf();

	static constexpr int32 NumFloors = 3;
	static constexpr int32 NumSlotsPerFloor = 9;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EItemType BoundItemType = EItemType::ItemA;

	UPROPERTY(Replicated, BlueprintReadOnly)
	TArray<FShelfSlot> Slots;

	UPROPERTY(Replicated, BlueprintReadOnly)
	TWeakObjectPtr<AFactoryAgentBase> InboundZoneOccupant;

	UPROPERTY(Replicated, BlueprintReadOnly)
	TWeakObjectPtr<AFactoryAgentBase> OutboundZoneOccupant;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform InboundStagingTransform;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform OutboundStagingTransform;

	// 5단계 신규 — 마커 기준 선반 정면축 하나만 사용, 입고는 +방향/출고는 -방향으로 이 거리만큼 이동.
	// 아틀라스/운송로봇마다 값이 다르다(운송로봇이 더 멀리). Docs에 없는 구현값 — 레벨 제작 후 실측 조정 필요.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|WorkPosition")
	float AtlasWorkDistance = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|WorkPosition")
	float TransportRobotWorkDistance = 300.f;

	// 지정한 (FloorIndex, SlotIndex) 마커의 월드 트랜스폼. 마커를 못 찾으면 선반 자신의 트랜스폼으로 대체.
	FTransform GetSlotMarkerTransform(int32 FloorIndex, int32 SlotIndex) const;

	// ZoneType(Inbound=+/Outbound=-)에 맞춰 마커에서 선반 정면축으로 각자의 작업거리만큼 뗀 월드 위치.
	FVector GetAtlasWorkLocation(int32 FloorIndex, int32 SlotIndex, EWorkZoneType ZoneType) const;
	FVector GetTransportRobotWorkLocation(int32 FloorIndex, int32 SlotIndex, EWorkZoneType ZoneType) const;

	bool TryReserveInboundZone(AFactoryAgentBase* Atlas);
	void ReleaseInboundZone();
	bool TryReserveOutboundZone(AFactoryAgentBase* Atlas);
	void ReleaseOutboundZone();

	bool TryReserveEmptySlot(int32& OutFloorIndex, int32& OutSlotIndex);
	bool TryReserveOldestOccupiedSlot(int32& OutFloorIndex, int32& OutSlotIndex, ALogisticsItem*& OutItem);
	void ConfirmInbound(int32 FloorIndex, int32 SlotIndex, ALogisticsItem* Item);
	void ConfirmOutboundRemoved(int32 FloorIndex, int32 SlotIndex);

	bool IsFull() const;
	int32 GetOccupiedCount() const;

	void TransferZoneOccupancy(EWorkZoneType ZoneType, AFactoryAgentBase* From, AFactoryAgentBase* To);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	static int32 ToSlotArrayIndex(int32 FloorIndex, int32 SlotIndex);

	FVector ComputeWorkLocation(const FVector& MarkerLocation, EWorkZoneType ZoneType, float DepthOffset) const;

	// BeginPlay에서 자식 컴포넌트 중 UStorageSlotMarkerComponent만 모아 캐싱
	UPROPERTY()
	TArray<TObjectPtr<UStorageSlotMarkerComponent>> SlotMarkers;
};
