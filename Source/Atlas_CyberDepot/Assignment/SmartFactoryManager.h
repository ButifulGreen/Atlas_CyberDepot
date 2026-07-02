// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Agent/RepairTypes.h"
#include "SmartFactoryManager.generated.h"

class AFactoryAgentBase;
class AFactoryNPCHuman;

// Docs/03_InventoryOrder.md §3 — 6단계 대상.
UCLASS()
class AMSmartFactoryManager : public AGameStateBase
{
	GENERATED_BODY()

public:
	UPROPERTY(Replicated, BlueprintReadOnly)
	float ReputationScore = 0.f;

	void AdjustReputation(float Delta, FName Reason);
	void RequestMaintenance(AFactoryAgentBase* Agent, ERepairType RepairType);
	void OnAgentBecameIdle(AFactoryAgentBase* Agent);
	void OnRepairCompleted(AFactoryAgentBase* Agent);
	AFactoryNPCHuman* FindNearestAvailableNPC(const FVector& Location) const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
