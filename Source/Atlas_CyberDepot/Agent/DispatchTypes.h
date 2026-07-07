// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EventBus/FactoryEventTypes.h"
#include "Infrastructure/StorageShelf.h"
#include "DispatchTypes.generated.h"

class AFactoryAtlasRobot;

// 버그 수정 — 아틀라스가 배송로봇을 거리로 추정 매칭하던 방식(FindWaitingTransportRobot)이
// 반경 튜닝값에 따라 계속 어긋나는 문제가 있어, 트립 단위로 정확히 짝을 기억하는 방식으로 교체.
// FStationAssignment::ReservedSlots와 FTransportTask가 생성 시점에 같은 TripTaskID를 공유한다.
USTRUCT(BlueprintType)
struct FReservedSlotEntry
{
	GENERATED_BODY()

	// Shelf 배정: X=FloorIndex, Y=SlotIndex(1-based). Tray 배정: 슬롯 개념이 없어 항상 (-1,-1).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint SlotCoord = FIntPoint(-1, -1);

	// 이 트립을 담당할 FTransportTask::TaskID와 동일한 값 — 아틀라스와 배송로봇이 같은 트립임을 식별하는 키.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGuid TripTaskID;
};

// Docs/07_TaskAssignment.md §7에서 정의되지만 AFactoryAtlasRobot::CurrentAssignment가
// 값 멤버로 참조해서(포인터가 아니라 전방 선언으로는 컴파일 불가) 5단계에서 함께 정의
USTRUCT(BlueprintType)
struct FStationAssignment
{
	GENERATED_BODY()

	// 디버깅 편의 — VisibleAnywhere 없이 BlueprintReadOnly만으로는 디테일 패널에 안 뜬다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGuid AssignmentID;

	// 8단계 — 취소(Docs/02_Multiplayer_RPC.md) 시 원본 주문을 역추적하기 위해 추가
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGuid SourceOrderID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	EWorkZoneType ZoneType = EWorkZoneType::ShelfInboundZone;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TWeakObjectPtr<AActor> TargetZoneOwner;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 RemainingCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TWeakObjectPtr<AFactoryAtlasRobot> AssignedAtlas;

	// 버그 수정 — 슬롯을 아틀라스가 나중에 즉흥적으로 정하지 않고, 작업 생성 시점(DecomposeOrder/
	// EnqueueInboundWork)에 미리 예약해서 큐로 들고 있는다. 각 항목의 TripTaskID로 같은 트립을
	// 담당할 FTransportTask와 정확히 짝지어진다(거리 추정 매칭 폐기). Shelf 배정은 SlotCoord도 유효하고,
	// Tray 배정은 SlotCoord 없이 TripTaskID만 쓰인다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FReservedSlotEntry> ReservedSlots;

	bool IsValid() const { return RemainingCount > 0 || AssignmentID.IsValid(); }
};

// Docs/07_TaskAssignment.md §7에서 정의되지만 AFactoryTransportRobot::CurrentTask가
// 값 멤버로 참조해서 5단계에서 함께 정의
USTRUCT(BlueprintType)
struct FTransportTask
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGuid TaskID;

	// 8단계 — 취소(Docs/02_Multiplayer_RPC.md) 시 원본 주문을 역추적하기 위해 추가
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGuid SourceOrderID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TWeakObjectPtr<AActor> PickupPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TWeakObjectPtr<AActor> DropoffPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	EItemType ItemType = EItemType::ItemA;

	// 버그 수정 — 배송로봇도 층수와 무관하게 슬롯의 (X, Y) 위치로 직접 이동해야 해서 추가.
	// -1이면 선반이 아닌 트레이 쪽 지점이라 슬롯 개념이 없다는 뜻.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 FloorIndex = -1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 SlotIndex = -1;

	bool IsValid() const { return TaskID.IsValid(); }
};
