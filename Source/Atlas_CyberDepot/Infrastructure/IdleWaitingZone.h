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

	// 7단계 후속 — 선입선출 검색을 폐기하고, 이미 정해진(Home) SlotIndex를 그대로 점유한다.
	// 대기실 배치(마커 개수)가 항상 로봇 수 이상이라고 가정하므로 실패 시 별도 폴백은 없다.
	bool TryOccupyHomeSlot(AFactoryAgentBase* Agent, int32 SlotIndex, FTransform& OutSlotTransform);
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
