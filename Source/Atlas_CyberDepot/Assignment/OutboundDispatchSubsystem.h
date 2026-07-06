// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Agent/DispatchTypes.h"
#include "Infrastructure/HorizontalTray.h"
#include "OutboundDispatchSubsystem.generated.h"

class AFactoryAtlasRobot;
class AFactoryTransportRobot;
class AStorageShelf;
struct FDeliveryOrder;

USTRUCT()
struct FPendingHandoff
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid AssignmentID;

	UPROPERTY()
	TWeakObjectPtr<AFactoryAtlasRobot> From;

	UPROPERTY()
	TWeakObjectPtr<AFactoryAtlasRobot> To;

	UPROPERTY()
	EWorkZoneType ZoneType = EWorkZoneType::ShelfInboundZone;
};

// Docs/07_TaskAssignment.md §7 — 6단계 대상. 06_Infrastructure.md, 04_Agent_AI.md가 먼저 준비돼 있어야 한다.
UCLASS()
class UOutboundDispatchSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	TArray<FStationAssignment> ActiveStationAssignments;

	UPROPERTY(BlueprintReadOnly)
	TArray<FTransportTask> PendingTransportTasks;

	UPROPERTY()
	TMap<FGuid, FPendingHandoff> PendingHandoffs;

	void DecomposeOrder(const FDeliveryOrder& Order);
	// 8단계 — 해당 주문에서 파생된 작업이 전부 미배정 상태일 때만 제거하고 true 반환, 하나라도 배정됐으면 false
	bool TryCancelAssignmentsForOrder(const FGuid& OrderID);
	bool TryAssignIdleAtlas(AFactoryAtlasRobot* Atlas, FStationAssignment& OutAssignment);
	bool TryAssignIdleTransportRobot(AFactoryTransportRobot* Robot, FTransportTask& OutTask);
	void HandoffStationAssignment(const FGuid& AssignmentID, AFactoryAtlasRobot* From, AFactoryAtlasRobot* To);
	void OnHandoffAtlasArrivedAtStagingPoint(const FGuid& AssignmentID);
	void OnStationAssignmentCompleted(const FGuid& AssignmentID);

	// 문서는 TaskID만 받지만, 이벤트에 실을 ActorID/ActorType을 얻으려면 호출자(로봇)가 필요해 매개변수로 추가
	void OnTransportTaskCompleted(const FGuid& TaskID, AFactoryTransportRobot* Robot);

	// 6단계 신규 — 월드의 Idle 상태 아틀라스/배송로봇에게 대기 중인 배정/작업을 밀어넣는다(Push).
	// DecomposeOrder/EnqueueInboundWork가 새 작업을 만든 직후 호출.
	void TryDispatchIdleAgents();

	// 6단계 신규 — InventoryOrderSubsystem::TryPlaceOrder가 Inbound 트레이에 물품을 올린 직후 호출.
	// TrayWorkZone(Tray) + ShelfInboundZone(Shelf) 배정과 이를 잇는 FTransportTask를 생성한다.
	void EnqueueInboundWork(EItemType ItemType, AHorizontalTray* Tray, AStorageShelf* Shelf);

private:
	AStorageShelf* FindShelfForItemType(EItemType ItemType) const;
	AHorizontalTray* FindTrayForItemType(EItemType ItemType, ETrayDirection Direction) const;
};
