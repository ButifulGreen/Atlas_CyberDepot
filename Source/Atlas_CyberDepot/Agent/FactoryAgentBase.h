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

	virtual void SetState(EAgentState NewState);
	virtual void OnBlockedTick(float DeltaTime);
	virtual void OnUnblocked();
	FStateSnapshot ToSnapshot() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	virtual void OnRep_CurrentState();
};
