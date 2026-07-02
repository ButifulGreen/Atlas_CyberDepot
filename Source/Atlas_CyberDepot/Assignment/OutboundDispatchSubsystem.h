// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Agent/DispatchTypes.h"
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
	bool TryAssignIdleAtlas(AFactoryAtlasRobot* Atlas, FStationAssignment& OutAssignment);
	bool TryAssignIdleTransportRobot(AFactoryTransportRobot* Robot, FTransportTask& OutTask);
	void HandoffStationAssignment(const FGuid& AssignmentID, AFactoryAtlasRobot* From, AFactoryAtlasRobot* To);
	void OnHandoffAtlasArrivedAtStagingPoint(const FGuid& AssignmentID);
	void OnStationAssignmentCompleted(const FGuid& AssignmentID);

	// 문서는 TaskID만 받지만, 이벤트에 실을 ActorID/ActorType을 얻으려면 호출자(로봇)가 필요해 매개변수로 추가
	void OnTransportTaskCompleted(const FGuid& TaskID, AFactoryTransportRobot* Robot);

private:
	AStorageShelf* FindShelfForItemType(EItemType ItemType) const;
};
