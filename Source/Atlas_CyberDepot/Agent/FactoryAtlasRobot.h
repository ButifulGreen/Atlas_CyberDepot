// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/DispatchTypes.h"
#include "TimerManager.h"
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

	// 버그 수정 — 배송로봇 매칭을 거리 추정 대신 이 값(FTransportTask::TaskID와 동일)으로 정확히 짚는다.
	UPROPERTY(VisibleAnywhere)
	FGuid TripTaskID;
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

	// Docs에 없는 구현값 — OnTaskCompleted 1회당 OperationCount 증가량(원래 매직넘버 1로 하드코딩돼 있었음).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Maintenance")
	int32 OperationCountPerTask = 20;

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

	// 버그 수정 — bIsReachingForItem이 TransferItem 한 호출 안에서 true/false로 동기 토글되면
	// 매 프레임 한 번만 값을 읽는 ABP가 true를 절대 관측하지 못한다. 최소 이 시간만큼은
	// true를 유지해 ABP의 FInterpTo가 IK Alpha를 실제로 올릴 시간을 준다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|IK")
	float IKReachHoldSeconds = 0.5f;

	// Docs에 없는 구현값 — 파트너(배송로봇) 대기 중 재시도 간격.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|WorkZone")
	float ZoneRetryIntervalSeconds = 1.f;

	virtual bool IsMaintenanceDue() const override;
	virtual float GetOperationRatio() const override;
	virtual int32 GetOperationCount() const override { return OperationCount; }
	virtual void ApplyRestDecay(int32 Amount) override;
	virtual void ResumeAfterRepair() override;
	virtual URepairProgressComponent* GetRepairComponent() const override { return RepairComponent; }
	virtual void DebugForceBreakdown() override { TriggerBreakdown(); }
	virtual void OnArrivedAtDestination() override;
	virtual void OnMoveFailedPermanently() override;
	virtual void OnWorkingTick(float DeltaTime) override;
	// 안전거리/FinalHop 트레이스가 이번 트립의 짝(배송로봇)을 장애물로 오인해 접근을 멈추지 않도록 제외.
	virtual AFactoryAgentBase* GetCurrentTripPartner() const override;
	// 정비 중인 NPC(Broken)가 선반 접근을 막을 때, 대안 칸이 있으면 그리로 재할당(성공 시 true).
	virtual bool TryHandleFinalHopBrokenBlock(AFactoryAgentBase* BrokenAgent) override;
	bool IsEligibleForQuickCheck() const;
	void AcceptStationAssignment(const FStationAssignment& Assignment, bool bIsHandoff = false);
	void EvaluateRotationOrContinue();
	bool TransferItem(AActor* Source, AActor* Destination);
	void OnAssignmentExhausted();
	void OnTaskCompleted();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	float ComputeCurrentBreakdownChance() const;
	// EvaluateRotationOrContinue의 확률 롤 성공 시 / DebugForceBreakdown 강제 호출 시 공용으로 쓰는 실제 고장 처리.
	void TriggerBreakdown();
	void AttachHeldItem(ALogisticsItem* Item);
	void DetachHeldItem();

	// 버그 수정 — 슬롯은 더 이상 아틀라스가 즉흥적으로 정하지 않는다. 작업 생성 시점에 이미
	// CurrentAssignment.ReservedSlots에 예약돼 있는 큐에서 다음 슬롯을 꺼내 PendingSlotReservation에 기록한다.
	bool PopNextReservedSlot();

	// 6단계 신규 — 새 CurrentAssignment를 받은 직후 존 예약 + 첫 이동을 킥오프한다.
	void StartCurrentAssignment();
	// TrayWorkZone/ShelfIn·OutboundZone 배정을 이어간다(도착 직후, 또는 파트너 대기 재시도 중).
	void ContinueShelfAssignment();
	void ContinueTrayAssignment();
	// 버그 수정 — 거리로 근처 로봇을 추정하지 않고, 이 트립(TripTaskID)을 맡은 그 배송로봇을 정확히 찾는다.
	// 도착(Working) + 짐 보유 여부(bNeedsPayload)까지 일치해야 반환한다.
	AFactoryTransportRobot* FindWaitingTransportRobot(const FGuid& TripTaskID, bool bNeedsPayload) const;

	// 버그 수정 — 트레이/선반 핸드오프 지점에서 아틀라스와 배송로봇의 Crowd 회피가 서로를 장애물로 보고
	// 계속 피하려다 제자리에서 밀고 당겨(이동 실패 Code=1 Blocked) 목적지에 못 붙는 경우가 있었다.
	// FindWaitingTransportRobot과 달리 도착/짐 보유 여부와 무관하게 같은 트립이면 바로 찾는다(이동 중에도
	// 미리 상호 무시를 걸어둬야 하므로).
	AFactoryTransportRobot* FindTransportRobotForTrip(const FGuid& TripTaskID) const;
	// PendingSlotReservation.TripTaskID 기준으로 위 탐색 + AIController->SetAvoidanceIgnoreActor(true) 호출.
	void IgnoreTransportRobotForCurrentTrip(class AFactoryAIController* AIController);
	// 핸드오프가 끝난 뒤 상호 회피를 원복.
	void ClearTransportRobotIgnore(AFactoryTransportRobot* Robot);

	// IKReachHoldSeconds 경과 후 bIsReachingForItem을 내린다.
	void ClearIKReachFlag();

	float ZoneRetryTimer = 0.f;
	FTimerHandle IKReachTimerHandle;
	// 버그 수정 — StartCurrentAssignment의 존 예약 실패(다른 아틀라스가 점유 중)를 재시도하는 타이머.
	FTimerHandle StartAssignmentRetryTimerHandle;
};
