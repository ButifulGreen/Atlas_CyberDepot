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

	// Docs에 없는 구현값 — 정비 사이클(NPC 접근+수리) 단독 테스트용. 레벨의 Idle 상태 Atlas/TransportRobot
	// 중 아무거나 하나를 골라 실제 고장 처리(TriggerBreakdown)를 강제 호출한다. Level BP에서 키 바인딩으로 호출.
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void DebugForceRandomBreakdown();

	// 버그 수정 — RequestMaintenance 호출 시점에 가용 NPC가 없으면 PendingMaintenanceQueue에 쌓아두고
	// 조용히 리턴한다(재시도가 없어 배정자 없이 영구히 Broken으로 방치되던 문제). URepairProgressComponent::
	// OnRepairCompleted가 NPC를 사무실로 돌려보내기 전에 이 함수로 먼저 대기열을 확인한다.
	// 큐에서 아직 유효한(여전히 Broken이고 정비자가 없는) 항목을 찾으면 NPC를 배정하고 true를 반환한다.
	bool TryAssignNextPendingMaintenance(AFactoryNPCHuman* NPC);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TArray<TWeakObjectPtr<AFactoryAgentBase>> PendingMaintenanceQueue;
};
