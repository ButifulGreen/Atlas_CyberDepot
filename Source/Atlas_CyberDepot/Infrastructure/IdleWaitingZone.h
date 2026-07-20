// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "EventBus/FactoryEventTypes.h"
#include "IdleWaitingZone.generated.h"

class AFactoryAgentBase;
class AFactoryNPCHuman;

UENUM(BlueprintType)
enum class EZoneMaintenanceState : uint8
{
	Idle,
	Active
};

// 7단계 신규 — 대기실 도착 지점을 뷰포트에서 드래그로 배치하기 위한 마커. UStorageSlotMarkerComponent와
// 동일한 패턴이지만 대기실은 층 개념 없는 1열 구조라 SlotIndex 하나로 식별한다.
UCLASS(ClassGroup = (Infrastructure), meta = (BlueprintSpawnableComponent))
class UParkingSlotMarkerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parking Slot")
	int32 SlotIndex = 0;
};

// Docs/06_Infrastructure.md §6 — 4단계 대상(정비 판정 연동은 6단계에서 완성, 실사용 연결은 7단계). 아틀라스/운송로봇 전용 대기 공간 + 배치 정비.
UCLASS()
class AIdleWaitingZone : public AActor
{
	GENERATED_BODY()

public:
	AIdleWaitingZone();

	// Docs의 EAgentType은 2단계에서 EActorType으로 통합했다
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EActorType AllowedAgentType = EActorType::AtlasRobot;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float RestDecayIntervalSeconds = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 RestDecayAmountPerInterval = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float FullyRestedThresholdRatio = 0.2f;

	UPROPERTY(Replicated, BlueprintReadOnly)
	EZoneMaintenanceState MaintenanceState = EZoneMaintenanceState::Idle;

	// 버그 수정(회피 재설계) — 고정 AccessWaypoint 하나로 진입/이탈을 다 처리하면 두 방향 트래픽이
	// 같은 노드로 몰린다. 선반/트레이와 동일한 원칙으로 전환 — 이 대기실 전용 고정 참조를 없애고,
	// TryHeadToIdleZone(진입)/AcceptStationAssignment 등(이탈) 둘 다 목표 마커 기준 최근접 도달가능
	// 웨이포인트를 매번 동적으로 찾는다. 레벨에 대기실 진입 측/이탈 측에 각각 가까운 전용 웨이포인트를
	// 배치해두면(EWaypointAccess::Inbound/Outbound, AllowedAgentTypes) 자연히 분리돼 쓰인다 —
	// 코드가 방향을 강제하지 않고 배치된 거리로 저절로 갈린다.

	// 7단계 후속 — 선입선출 검색을 폐기하고, 이미 정해진(Home) SlotIndex의 마커 위치만 조회한다(순수 조회,
	// 부작용 없음). 이동 목적지 계산용 — TryHeadToIdleZone(출발 시점)이 호출한다.
	bool GetHomeSlotTransform(int32 SlotIndex, FTransform& OutSlotTransform) const;
	// 버그 수정 — 실제로 파킹 등록(SlotOccupancy/bIsParkedInIdleZone/OnAgentBecameIdle)을 하는 부분을
	// TryOccupyHomeSlot에서 분리. 도착 시점(TryHandleIdleZoneArrival)에서만 호출해야 이동 중에는
	// RestDecay/QuickCheck 대상이 되지 않는다.
	void MarkSlotOccupied(AFactoryAgentBase* Agent, int32 SlotIndex);
	void ReleaseSlot(AFactoryAgentBase* Agent);
	// 7단계 후속 — InOutRemainingAgents 맨 앞부터 자신의 마커 개수만큼 소비해 각 로봇에게 고정 홈 슬롯을
	// 배정한다(선입선출 아님, 실행 시 1회만 호출). UOutboundDispatchSubsystem::AssignHomeIdleZoneSlots가 호출.
	void AssignHomeSlots(TArray<AFactoryAgentBase*>& InOutRemainingAgents);
	bool IsAgentParked(const AFactoryAgentBase* Agent) const;
	AFactoryAgentBase* FindRestedOccupant() const;
	void OnRestDecayInterval();
	bool ShouldDispatchNPCForMaintenance() const;
	void BeginBatchMaintenance(AFactoryNPCHuman* NPC);
	void OnBatchTargetLeft(AFactoryAgentBase* Agent);
	void OnBatchMaintenanceProgress();
	void EndBatchMaintenance();
	void OnBatchMaintenanceInterrupted();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	// BeginPlay에서 자식 컴포넌트 중 UParkingSlotMarkerComponent만 모아 캐싱(StorageShelf::SlotMarkers와 동일 패턴).
	UPROPERTY()
	TArray<TObjectPtr<UParkingSlotMarkerComponent>> ParkingMarkers;

	// TMap은 표준 프로퍼티 리플리케이션을 지원하지 않아 서버 전용 상태로만 둔다. 키는 배열 인덱스가 아니라
	// UParkingSlotMarkerComponent::SlotIndex — BP에서 마커를 어떤 순서로 추가·삭제해도 안전하게 매칭된다.
	UPROPERTY()
	TMap<int32, TWeakObjectPtr<AFactoryAgentBase>> SlotOccupancy;

	UPROPERTY()
	TArray<TWeakObjectPtr<AFactoryAgentBase>> BatchMaintenanceTargetSet;

	UPROPERTY()
	TWeakObjectPtr<AFactoryNPCHuman> BatchMaintenanceNPC;

	FTimerHandle RestDecayTimerHandle;
};
