// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Agent/RepairTypes.h"
#include "EventBus/FactoryEventTypes.h"
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

	// 5단계 신규 — 이번 세션에서 EItemType별로 몇 번 메시 컴포넌트(0=MeshItemA/1=B/2=C)를 쓸지의 매핑.
	// 서버가 BeginPlay에서 한 번만 셔플해서 정하고, 판이 끝날 때까지 유지된다(ALogisticsItem::UpdateItemMesh가 참조).
	UPROPERTY(Replicated, BlueprintReadOnly)
	TArray<uint8> ItemTypeToMeshSlot;

	void AdjustReputation(float Delta, FName Reason);
	void RequestMaintenance(AFactoryAgentBase* Agent, ERepairType RepairType);
	void OnAgentBecameIdle(AFactoryAgentBase* Agent);
	void OnRepairCompleted(AFactoryAgentBase* Agent);
	AFactoryNPCHuman* FindNearestAvailableNPC(const FVector& Location) const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
};
