// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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
};
