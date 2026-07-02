// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/DispatchTypes.h"
#include "FactoryTransportRobot.generated.h"

class ALogisticsItem;
class URepairProgressComponent;

// Docs/04_Agent_AI.md §4 — 5단계 대상.
// UOutboundDispatchSubsystem 호출부(07_TaskAssignment.md, 6단계)는 아직 없어 해당 지점만 주석 처리.
UCLASS()
class AFactoryTransportRobot : public AFactoryAgentBase
{
	GENERATED_BODY()

public:
	AFactoryTransportRobot();

	static constexpr float MaxBreakdownChanceCap = 0.40f;

	// 정지 상태가 이 반경 안의 ACostZoneVolume에 혼잡도로 반영되는 거리 (Docs에 없는 구현값)
	static constexpr float BlockedZoneRegisterRadius = 300.f;

	UPROPERTY(BlueprintReadOnly)
	FTransportTask CurrentTask;

	UPROPERTY(Replicated, BlueprintReadOnly)
	TObjectPtr<ALogisticsItem> PayloadItem;

	UPROPERTY(BlueprintReadOnly)
	int32 OperationCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Maintenance")
	int32 MaintenanceThreshold = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Breakdown")
	float BreakdownChanceBase = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Breakdown")
	float BreakdownChanceOverageMultiplier = 0.02f;

	UPROPERTY()
	TObjectPtr<URepairProgressComponent> RepairComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FName PayloadItemSocketName = TEXT("ItemSocket");

	bool IsMaintenanceDue() const;
	bool IsEligibleForQuickCheck() const;
	void AcceptTransportTask(const FTransportTask& Task);
	void OnItemPickedUp();
	void EvaluateRotationOrContinue();
	void OnEnterBlockedState();
	void OnTaskCompleted();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	float ComputeCurrentBreakdownChance() const;
};
