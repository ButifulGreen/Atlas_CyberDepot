// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Agent/DispatchTypes.h"
#include "Infrastructure/HorizontalTray.h"
#include "OutboundDispatchSubsystem.generated.h"

class AFactoryAtlasRobot;
class AFactoryTransportRobot;
class AFactoryAgentBase;
class AStorageShelf;
class AIdleWaitingZone;
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

	// 버그 수정(사용자 지시, 근본 원인인 회피 국소최소 문제와 별개로 우선 반영) — OnMoveFailedPermanently는
	// 재큐잉하지 않는 게 원래 의도였지만, 회피가 막힌 지점에서 배정이 조용히 죽어 사이클 전체가 멈추는
	// 빈도가 테스트를 막을 정도로 잦아졌다. 실패한 배정을 새 AssignmentID로 다시 큐에 넣어 다른 아틀라스가
	// 이어받게 한다 — 같은 지점이 계속 막히면 다음 아틀라스도 반복해서 실패할 수 있다는 트레이드오프는 감수.
	void RequeueStationAssignment(FStationAssignment Assignment);
	// TaskID는 유지 — 이 값으로 아틀라스 쪽 ReservedSlots(TripTaskID)와 짝지어지므로 바뀌면 영구 미아가 된다.
	void RequeueTransportTask(const FTransportTask& Task);

	// 문서는 TaskID만 받지만, 이벤트에 실을 ActorID/ActorType을 얻으려면 호출자(로봇)가 필요해 매개변수로 추가
	void OnTransportTaskCompleted(const FGuid& TaskID, AFactoryTransportRobot* Robot);

	// 6단계 신규 — 월드의 Idle 상태 아틀라스/배송로봇에게 대기 중인 배정/작업을 밀어넣는다(Push).
	// DecomposeOrder/EnqueueInboundWork가 새 작업을 만든 직후 호출.
	void TryDispatchIdleAgents();

	// 6단계 신규 — InventoryOrderSubsystem::TryPlaceOrder가 Inbound 트레이에 물품을 올린 직후 호출.
	// TrayWorkZone(Tray) + ShelfInboundZone(Shelf) 배정과 이를 잇는 FTransportTask를 생성한다.
	void EnqueueInboundWork(EItemType ItemType, AHorizontalTray* Tray, AStorageShelf* Shelf);

	// 재사용을 위해 public으로 노출 — InventoryOrderSubsystem이 동일한 조회 로직을 다시 구현하지 않고 가져다 쓴다.
	AStorageShelf* FindShelfForItemType(EItemType ItemType) const;
	AHorizontalTray* FindTrayForItemType(EItemType ItemType, ETrayDirection Direction) const;

protected:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	// 7단계 후속 — "선입선출 없이 실행 시 1회만 고정 배정" 규칙. 버그 수정(사용자 지시) — 예전엔
	// AllowedAgentType(단일값)별로 로봇/대기실 풀을 완전히 분리해 이름순으로 그냥 앞에서부터 채웠다
	// (물리적 위치 무관). AllowedAgentTypes가 비트마스크로 바뀌어 한 대기실이 아틀라스/배송로봇을 동시에
	// 받을 수 있게 된 이상 타입별 분리 자체가 부적절해졌고, 이 참에 배정 기준도 "시작 시점에 마커와
	// 물리적으로 가장 가까운 로봇"으로 바꿨다 — 모든 로봇 + 모든 대기실의 모든 슬롯을 한 번에 모아,
	// 아직 안 정해진 슬롯-로봇 쌍 중 가장 가까운 조합을 하나씩 그리디하게 확정한다.
	void AssignHomeIdleZoneSlots();

	// 버그 수정 — UWorldSubsystem::OnWorldBeginPlay는 UWorld::BeginPlay() 안에서 GameMode->StartPlay()보다
	// 먼저 호출된다(엔진 World.cpp). 즉 이 시점엔 레벨의 어떤 액터도 아직 자신의 BeginPlay를 실행하지 않은
	// 상태라, AIdleWaitingZone::ParkingMarkers(자기 BeginPlay에서 캐싱)가 항상 비어있어 AssignHomeIdleZoneSlots가
	// 아무도 배정하지 못하고 조용히 실패했다. 다음 틱으로 미뤄 모든 액터의 BeginPlay가 끝난 뒤 실행한다.
	void RunDeferredWorldBeginPlaySetup();
	// 버그 수정 — 같은 품목을 요청하는 두 주문이 같은 선반/트레이에 대해 각자 별도의 FStationAssignment를
	// 만들어내는 경우, 두 유휴 아틀라스가 동시에 배정받으면 나중에 배정된 쪽은 StartCurrentAssignment의
	// TryReserve*Zone이 실패해 CurrentState==Idle인 채로 배정만 영구히 남는 문제가 있었다.
	// 배정 자체를 병합하는 대신, 배정 시점에 물리적 존이 이미 점유돼 있으면 그 배정을 건너뛰어
	// (여전히 미배정 상태로 큐에 남겨) 다른 로봇이 나중에 자연스럽게 집어가게 한다.
	bool IsZoneOccupied(EWorkZoneType ZoneType, AActor* TargetZoneOwner) const;
};
