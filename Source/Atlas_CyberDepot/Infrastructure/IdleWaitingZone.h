// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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

// Docs/06_Infrastructure.md §6 — 4단계 대상(정비 판정 연동은 6단계에서 완성). 아틀라스/운송로봇 전용 대기 공간 + 배치 정비.
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
	TArray<FTransform> ParkingSlots;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float RestDecayIntervalSeconds = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 RestDecayAmountPerInterval = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float FullyRestedThresholdRatio = 0.2f;

	UPROPERTY(Replicated, BlueprintReadOnly)
	EZoneMaintenanceState MaintenanceState = EZoneMaintenanceState::Idle;

	bool TryReserveSlot(AFactoryAgentBase* Agent, FTransform& OutSlotTransform);
	void ReleaseSlot(AFactoryAgentBase* Agent);
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
	// TMap은 표준 프로퍼티 리플리케이션을 지원하지 않아 서버 전용 상태로만 둔다.
	UPROPERTY()
	TMap<int32, TWeakObjectPtr<AFactoryAgentBase>> SlotOccupancy;

	UPROPERTY()
	TArray<TWeakObjectPtr<AFactoryAgentBase>> BatchMaintenanceTargetSet;

	UPROPERTY()
	TWeakObjectPtr<AFactoryNPCHuman> BatchMaintenanceNPC;

	FTimerHandle RestDecayTimerHandle;
};
