// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "EventBus/FactoryEventTypes.h"
#include "FactoryAgentBase.generated.h"

// Docs/04_Agent_AI.md §4 — 아틀라스/운송로봇/NPC 공용 베이스. 2단계(스켈레톤) 대상.
UCLASS()
class AFactoryAgentBase : public ACharacter
{
	GENERATED_BODY()

public:
	AFactoryAgentBase();

	static constexpr float BlockedThresholdSeconds = 2.f;

	UPROPERTY(Replicated, BlueprintReadOnly)
	FGuid AgentID;

	// Docs/01_EventBus_DataPipeline.md의 EActorType과 04_Agent_AI.md의 EAgentType이 동일한 값 구성이라 하나로 통합
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	EActorType AgentType = EActorType::AtlasRobot;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentState, BlueprintReadOnly)
	EAgentState CurrentState = EAgentState::Idle;

	UPROPERTY(BlueprintReadOnly)
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float BlockedTimer = 0.f;

	// Docs/04_Agent_AI.md IsEligibleForQuickCheck()이 참조하지만 어디에도 멤버로 정의돼 있지 않아
	// 5단계에서 추가 — AIdleWaitingZone::TryReserveSlot/ReleaseSlot이 갱신한다.
	UPROPERTY(BlueprintReadOnly)
	bool bIsParkedInIdleZone = false;

	virtual void SetState(EAgentState NewState);
	virtual void OnBlockedTick(float DeltaTime);
	virtual void OnUnblocked();
	FStateSnapshot ToSnapshot() const;

	// AtlasRobot/TransportRobot가 각자 OperationCount/MaintenanceThreshold로 구현(override)한다.
	// AIdleWaitingZone·AMSmartFactoryManager처럼 구체 타입을 모르는 코드가 공용으로 판정할 수 있도록
	// 6단계에서 베이스에 추가.
	virtual bool IsMaintenanceDue() const { return false; }
	virtual float GetOperationRatio() const { return 0.f; }
	virtual void ApplyRestDecay(int32 Amount) {}

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	virtual void OnRep_CurrentState();
};
