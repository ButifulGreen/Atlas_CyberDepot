// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/DispatchTypes.h"
#include "FactoryAtlasRobot.generated.h"

class ALogisticsItem;
class URepairProgressComponent;

USTRUCT()
struct FPendingSlotReservation
{
	GENERATED_BODY()

	UPROPERTY()
	bool bIsValid = false;

	UPROPERTY()
	int32 FloorIndex = -1;

	UPROPERTY()
	int32 SlotIndex = -1;
};

// Docs/04_Agent_AI.md §4 — 5단계 대상.
// UOutboundDispatchSubsystem 관련 호출부(07_TaskAssignment.md, 6단계)는 아직 없어
// 해당 지점만 주석으로 표시하고 인터페이스/자체 상태 관리만 구현한다.
UCLASS()
class AFactoryAtlasRobot : public AFactoryAgentBase
{
	GENERATED_BODY()

public:
	AFactoryAtlasRobot();

	static constexpr float MaxBreakdownChanceCap = 0.40f;

	UPROPERTY(BlueprintReadOnly)
	FStationAssignment CurrentAssignment;

	UPROPERTY(Replicated, BlueprintReadOnly)
	TObjectPtr<ALogisticsItem> HeldItem;

	UPROPERTY(BlueprintReadOnly)
	int32 OperationCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Maintenance")
	int32 MaintenanceThreshold = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Breakdown")
	float BreakdownChanceBase = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Breakdown")
	float BreakdownChanceOverageMultiplier = 0.02f;

	UPROPERTY(BlueprintReadOnly)
	FPendingSlotReservation PendingSlotReservation;

	UPROPERTY()
	TObjectPtr<URepairProgressComponent> RepairComponent;

	// 물품 부착에 쓰이는 스켈레탈 메시 소켓 이름 (아직 아트 에셋이 없어 EditAnywhere로 노출만 해둠)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FName HeldItemSocketName = TEXT("ItemSocket");

	virtual bool IsMaintenanceDue() const override;
	virtual float GetOperationRatio() const override;
	virtual void ApplyRestDecay(int32 Amount) override;
	bool IsEligibleForQuickCheck() const;
	void AcceptStationAssignment(const FStationAssignment& Assignment, bool bIsHandoff = false);
	void EvaluateRotationOrContinue();
	void TransferItem(AActor* Source, AActor* Destination);
	void OnAssignmentExhausted();
	void OnTaskCompleted();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	float ComputeCurrentBreakdownChance() const;
	void AttachHeldItem(ALogisticsItem* Item);
	void DetachHeldItem();
};
