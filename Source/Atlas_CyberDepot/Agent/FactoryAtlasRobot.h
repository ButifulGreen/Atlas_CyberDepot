// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/DispatchTypes.h"
#include "FactoryAtlasRobot.generated.h"

class ALogisticsItem;
class URepairProgressComponent;
class AFactoryTransportRobot;

USTRUCT(BlueprintType)
struct FPendingSlotReservation
{
	GENERATED_BODY()

	// 디버깅 편의 — VisibleAnywhere 없이는 디테일 패널에 안 뜬다.
	UPROPERTY(VisibleAnywhere)
	bool bIsValid = false;

	UPROPERTY(VisibleAnywhere)
	int32 FloorIndex = -1;

	UPROPERTY(VisibleAnywhere)
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

	// 디버깅 편의 — VisibleAnywhere 없이는 디테일 패널에 안 뜬다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FStationAssignment CurrentAssignment;

	UPROPERTY(VisibleAnywhere, Replicated, BlueprintReadOnly)
	TObjectPtr<ALogisticsItem> HeldItem;

	UPROPERTY(BlueprintReadOnly)
	int32 OperationCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Maintenance")
	int32 MaintenanceThreshold = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Breakdown")
	float BreakdownChanceBase = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Breakdown")
	float BreakdownChanceOverageMultiplier = 0.02f;

	// 버그 수정 — 플레이테스트로 조정될 값인데 static constexpr로 하드코딩돼 있어 재컴파일 없이 못 바꿨다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Breakdown")
	float MaxBreakdownChanceCap = 0.40f;

	// 버그 수정 — ComputeCurrentBreakdownChance의 매직넘버(5)를 노출. OperationCount가
	// MaintenanceThreshold를 이 값만큼 초과할 때마다 BreakdownChanceOverageMultiplier씩 누적된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Breakdown")
	int32 OverageOperationsPerStep = 5;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FPendingSlotReservation PendingSlotReservation;

	// 6단계 신규 — HandoffStationAssignment가 이동 요청 전 채워 넣는, 아직 도착하지 않은 핸드오프 배정 ID.
	// OnArrivedAtDestination에서 이 값이 유효하면 일반 작업 로직 대신 핸드오프 도착 처리를 우선한다.
	UPROPERTY(BlueprintReadOnly)
	FGuid PendingHandoffAssignmentID;

	UPROPERTY()
	TObjectPtr<URepairProgressComponent> RepairComponent;

	// 물품 부착에 쓰이는 스켈레탈 메시 소켓 이름 (아직 아트 에셋이 없어 EditAnywhere로 노출만 해둠)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FName HeldItemSocketName = TEXT("ItemSocket");

	// ABP의 Two-Bone IK가 읽어가는 손 타겟(월드 좌표)과 게이트. TransferItem이 선반 슬롯을 확정하는 시점에 갱신된다.
	UPROPERTY(BlueprintReadOnly, Category = "IK")
	FVector CurrentIKHandTarget = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "IK")
	bool bIsReachingForItem = false;

	// Docs에 없는 구현값 — 파트너(배송로봇) 대기 중 재시도 간격/탐색 반경.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|WorkZone")
	float ZoneRetryIntervalSeconds = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|WorkZone")
	float RendezvousSearchRadius = 300.f;

	virtual bool IsMaintenanceDue() const override;
	virtual float GetOperationRatio() const override;
	virtual void ApplyRestDecay(int32 Amount) override;
	virtual URepairProgressComponent* GetRepairComponent() const override { return RepairComponent; }
	virtual void OnArrivedAtDestination() override;
	virtual void OnWorkingTick(float DeltaTime) override;
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

	// 6단계 신규 — CurrentAssignment.ZoneType/TargetZoneOwner 기준으로 다음 슬롯을 예약해
	// PendingSlotReservation에 기록한다(이동 목적지 계산을 위해 TransferItem보다 먼저 호출).
	bool ReserveNextSlot();

	// 6단계 신규 — 새 CurrentAssignment를 받은 직후 존 예약 + 첫 이동을 킥오프한다.
	void StartCurrentAssignment();
	// TrayWorkZone/ShelfIn·OutboundZone 배정을 이어간다(도착 직후, 또는 파트너 대기 재시도 중).
	void ContinueShelfAssignment();
	void ContinueTrayAssignment();
	// 지정 위치 근방에서 아틀라스와 주고받을 준비가 된(짐 보유 여부가 bNeedsPayload와 일치하는) 배송로봇을 찾는다.
	AFactoryTransportRobot* FindWaitingTransportRobot(const FVector& Location, bool bNeedsPayload) const;

	float ZoneRetryTimer = 0.f;
};
