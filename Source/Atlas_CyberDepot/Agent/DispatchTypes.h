// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EventBus/FactoryEventTypes.h"
#include "Infrastructure/StorageShelf.h"
#include "DispatchTypes.generated.h"

class AFactoryAtlasRobot;

// Docs/07_TaskAssignment.md §7에서 정의되지만 AFactoryAtlasRobot::CurrentAssignment가
// 값 멤버로 참조해서(포인터가 아니라 전방 선언으로는 컴파일 불가) 5단계에서 함께 정의
USTRUCT(BlueprintType)
struct FStationAssignment
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid AssignmentID;

	// 8단계 — 취소(Docs/02_Multiplayer_RPC.md) 시 원본 주문을 역추적하기 위해 추가
	UPROPERTY(BlueprintReadOnly)
	FGuid SourceOrderID;

	UPROPERTY(BlueprintReadOnly)
	EWorkZoneType ZoneType = EWorkZoneType::ShelfInboundZone;

	UPROPERTY(BlueprintReadOnly)
	TWeakObjectPtr<AActor> TargetZoneOwner;

	UPROPERTY(BlueprintReadOnly)
	int32 RemainingCount = 0;

	UPROPERTY(BlueprintReadOnly)
	TWeakObjectPtr<AFactoryAtlasRobot> AssignedAtlas;

	bool IsValid() const { return RemainingCount > 0 || AssignmentID.IsValid(); }
};

// Docs/07_TaskAssignment.md §7에서 정의되지만 AFactoryTransportRobot::CurrentTask가
// 값 멤버로 참조해서 5단계에서 함께 정의
USTRUCT(BlueprintType)
struct FTransportTask
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid TaskID;

	// 8단계 — 취소(Docs/02_Multiplayer_RPC.md) 시 원본 주문을 역추적하기 위해 추가
	UPROPERTY(BlueprintReadOnly)
	FGuid SourceOrderID;

	UPROPERTY(BlueprintReadOnly)
	TWeakObjectPtr<AActor> PickupPoint;

	UPROPERTY(BlueprintReadOnly)
	TWeakObjectPtr<AActor> DropoffPoint;

	UPROPERTY(BlueprintReadOnly)
	EItemType ItemType = EItemType::ItemA;

	bool IsValid() const { return TaskID.IsValid(); }
};
