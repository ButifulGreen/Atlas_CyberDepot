// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/DispatchTypes.h"
#include "FactoryTransportRobot.generated.h"

class ALogisticsItem;
class URepairProgressComponent;
class UStaticMeshComponent;
class AHorizontalTray;

// Docs/04_Agent_AI.md §4 — 5단계 대상.
// UOutboundDispatchSubsystem 호출부(07_TaskAssignment.md, 6단계)는 아직 없어 해당 지점만 주석 처리.
UCLASS()
class AFactoryTransportRobot : public AFactoryAgentBase
{
	GENERATED_BODY()

public:
	AFactoryTransportRobot();

	// 디버깅 편의 — VisibleAnywhere 없이는 디테일 패널에 안 뜬다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FTransportTask CurrentTask;

	UPROPERTY(VisibleAnywhere, Replicated, BlueprintReadOnly)
	TObjectPtr<ALogisticsItem> PayloadItem;

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

	// 버그 수정 — ComputeCurrentBreakdownChance의 매직넘버(5)를 노출.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Breakdown")
	int32 OverageOperationsPerStep = 5;

	UPROPERTY()
	TObjectPtr<URepairProgressComponent> RepairComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FName PayloadItemSocketName = TEXT("ItemSocket");

	// 버그 수정 — 트레이의 배송로봇 작업 지점은 좌표 하나뿐이라(선반 슬롯과 달리 예약이 없었음) 물량이
	// 몰리면 두 번째 로봇이 첫 번째가 서있는 지점으로 이동을 시도해 영구 차단될 수 있었다. 점유 중이면
	// 이 간격으로 재시도한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|WorkZone")
	float TrayZoneRetryIntervalSeconds = 1.f;

	virtual bool IsMaintenanceDue() const override;
	virtual float GetOperationRatio() const override;
	virtual int32 GetOperationCount() const override { return OperationCount; }
	virtual void ApplyRestDecay(int32 Amount) override;
	virtual void ResumeAfterRepair() override;
	virtual URepairProgressComponent* GetRepairComponent() const override { return RepairComponent; }
	virtual void DebugForceBreakdown() override { TriggerBreakdown(); }
	virtual void OnArrivedAtDestination() override;
	virtual void OnMoveFailedPermanently() override;
	// 안전거리/FinalHop 트레이스가 이번 트립의 짝(아틀라스)을 장애물로 오인해 접근을 멈추지 않도록 제외.
	virtual AFactoryAgentBase* GetCurrentTripPartner() const override;
	// 버그 수정(사용자 지시, Waitbound) — 선반 마커는 깊이축으로만 떨어져 있어(Lateral 오프셋 없음)
	// 배송로봇 마커가 아틀라스 마커보다 항상 선반에서 더 멀다 — 배송로봇이 먼저 도착해 정지하면 뒤따르는
	// 아틀라스가 물리적으로 가로막힌다. Waitbound에서 짝 아틀라스가 먼저 도착(Working)했는지 확인한 뒤에만
	// 마커로 진입한다.
	virtual bool CanProceedFromWaitbound() const override;
	// 짝 아틀라스가 정비 중인 NPC 때문에 다른 선반칸으로 재할당됐을 때 같이 갱신한다(같은 트립이니
	// 물리적으로 같은 칸에서 만나야 핸드오프가 성립한다). 이미 그 칸으로 이동/도착해 있었다면 새 칸으로
	// 다시 이동시킨다.
	void RetargetCurrentTaskSlot(int32 NewFloorIndex, int32 NewSlotIndex);
	bool IsEligibleForQuickCheck() const;
	void AcceptTransportTask(const FTransportTask& Task);
	void EvaluateRotationOrContinue();
	void OnTaskCompleted();

	// 6단계 신규 — 배송로봇은 트레이/선반을 직접 건드리지 않고, 아틀라스가 소켓으로 직접 주고받는다.
	// 아틀라스가 TransferItem(Destination=this)로 물품을 건널 때 호출 — 부착 후 DropoffPoint로 이동 재개.
	void OnItemGivenByAtlas(ALogisticsItem* Item);
	// 아틀라스가 TransferItem(Source=this)로 물품을 가져갈 때 호출 — 비우고 트립 완료 처리.
	void OnItemCollectedByAtlas();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	float ComputeCurrentBreakdownChance() const;
	// EvaluateRotationOrContinue의 확률 롤 성공 시 / DebugForceBreakdown 강제 호출 시 공용으로 쓰는 실제 고장 처리.
	void TriggerBreakdown();

	// 7단계 후속 — EvaluateRotationOrContinue가 자리를 비우기 전 확인하는 "교대 가능한 로봇이 있는가" 체크.
	bool HasRestedTransportRobotAvailable() const;

	// PickupPoint/DropoffPoint가 Tray면 GetTransportRobotWorkLocation(), Shelf면 방향에 맞는
	// 스테이징 트랜스폼 위치를 반환한다(선반은 슬롯 개념이 없는 로봇 대기 지점 1곳뿐).
	FVector GetTaskPointLocation(AActor* PointActor, bool bIsPickupSide) const;

	// 버그 수정 — PointActor가 Tray면 이동 전에 TransportRobotWorkZone을 먼저 예약한다. 이미 다른
	// 배송로봇이 점유 중이면 이동을 미루고 TrayZoneRetryIntervalSeconds마다 재시도한다(선반은 슬롯
	// 단위로 이미 예약되므로 대상이 아님). AcceptTransportTask/OnItemGivenByAtlas가 공용으로 호출.
	void TryStartMoveToPoint(AActor* PointActor, bool bIsPickupSide);

	// 버그 수정 — 같은 트립을 담당하는 아틀라스를 찾아 Crowd 상호 회피를 미리 꺼둔다(핸드오프 지점에서
	// 서로를 피하려다 밀고 당기며 이동 실패하는 문제, FactoryAtlasRobot의 대응 함수와 짝). 해제는
	// 아틀라스 쪽에서 TransferItem 성공 시 처리한다(SetAvoidanceIgnoreActor가 양쪽을 동시에 되돌림).
	class AFactoryAtlasRobot* FindAtlasForTrip(const FGuid& TripTaskID) const;
	void RetryMoveToPendingPoint();
	// 현재 점유 중인 트레이 예약이 있으면 반납. 다음 목적지로 넘어갈 때(TryStartMoveToPoint 진입 시)와
	// 트립이 완전히 끝날 때(OnItemCollectedByAtlas) 둘 다 호출해 항상 반납되도록 한다.
	void ReleaseReservedTrayZone();

	// 버그 수정 — 배송로봇은 스켈레탈 메시가 없는(ACharacter::GetMesh()가 항상 빈) 모델이라, 물품 소켓
	// ("ItemSocket")은 실제로 BP에 추가된 스태틱 메시 컴포넌트 쪽에 있다. BeginPlay에서 그 컴포넌트를
	// 찾아 캐싱해두고 OnItemGivenByAtlas가 GetMesh() 대신 이걸 부착 대상으로 쓴다.
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BodyMeshComponent;

	// TryStartMoveToPoint가 예약에 실패했을 때 기억해두는 재시도 대상.
	TWeakObjectPtr<AActor> PendingMovePoint;
	bool bPendingMoveIsPickupSide = false;
	FTimerHandle TrayZoneWaitTimerHandle;

	// 지금 점유 중인 트레이 배송로봇 작업 지점(없으면 무효) — ReleaseReservedTrayZone이 참조.
	TWeakObjectPtr<AHorizontalTray> ReservedTrayZone;
};
