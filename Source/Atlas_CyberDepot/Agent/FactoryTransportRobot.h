// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/DispatchTypes.h"
#include "FactoryTransportRobot.generated.h"

class ALogisticsItem;
class URepairProgressComponent;
class ACostZoneVolume;
class UStaticMeshComponent;

// Docs/04_Agent_AI.md §4 — 5단계 대상.
// UOutboundDispatchSubsystem 호출부(07_TaskAssignment.md, 6단계)는 아직 없어 해당 지점만 주석 처리.
UCLASS()
class AFactoryTransportRobot : public AFactoryAgentBase
{
	GENERATED_BODY()

public:
	AFactoryTransportRobot();

	// 정지 상태가 이 반경 안의 ACostZoneVolume에 혼잡도로 반영되는 거리 (Docs에 없는 구현값)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Congestion")
	float BlockedZoneRegisterRadius = 300.f;

	// 디버깅 편의 — VisibleAnywhere 없이는 디테일 패널에 안 뜬다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FTransportTask CurrentTask;

	UPROPERTY(VisibleAnywhere, Replicated, BlueprintReadOnly)
	TObjectPtr<ALogisticsItem> PayloadItem;

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

	// 버그 수정 — ComputeCurrentBreakdownChance의 매직넘버(5)를 노출.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Breakdown")
	int32 OverageOperationsPerStep = 5;

	UPROPERTY()
	TObjectPtr<URepairProgressComponent> RepairComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FName PayloadItemSocketName = TEXT("ItemSocket");

	virtual bool IsMaintenanceDue() const override;
	virtual float GetOperationRatio() const override;
	virtual void ApplyRestDecay(int32 Amount) override;
	virtual URepairProgressComponent* GetRepairComponent() const override { return RepairComponent; }
	virtual void OnBlockedTick(float DeltaTime) override;
	virtual void OnUnblocked() override;
	virtual void OnArrivedAtDestination() override;
	bool IsEligibleForQuickCheck() const;
	void AcceptTransportTask(const FTransportTask& Task);
	void EvaluateRotationOrContinue();
	void OnEnterBlockedState();
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

	// 7단계 후속 — EvaluateRotationOrContinue가 자리를 비우기 전 확인하는 "교대 가능한 로봇이 있는가" 체크.
	bool HasRestedTransportRobotAvailable() const;

	// PickupPoint/DropoffPoint가 Tray면 GetTransportRobotWorkLocation(), Shelf면 방향에 맞는
	// 스테이징 트랜스폼 위치를 반환한다(선반은 슬롯 개념이 없는 로봇 대기 지점 1곳뿐).
	FVector GetTaskPointLocation(AActor* PointActor, bool bIsPickupSide) const;

	// OnBlockedTick 진입 엣지(1회)에서만 OnEnterBlockedState()를 호출하기 위한 플래그.
	bool bHasRegisteredBlocker = false;

	// OnEnterBlockedState에서 등록한 존들을 기억해뒀다가 OnUnblocked에서 그대로 해제한다.
	TArray<TWeakObjectPtr<ACostZoneVolume>> RegisteredBlockedZones;

	// 버그 수정 — 배송로봇은 스켈레탈 메시가 없는(ACharacter::GetMesh()가 항상 빈) 모델이라, 물품 소켓
	// ("ItemSocket")은 실제로 BP에 추가된 스태틱 메시 컴포넌트 쪽에 있다. BeginPlay에서 그 컴포넌트를
	// 찾아 캐싱해두고 OnItemGivenByAtlas가 GetMesh() 대신 이걸 부착 대상으로 쓴다.
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BodyMeshComponent;
};
