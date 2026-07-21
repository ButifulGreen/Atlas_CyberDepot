// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/RepairTypes.h"
#include "FactoryNPCHuman.generated.h"

class APlayerController;

// Docs/04_Agent_AI.md에 값이 명시돼 있지 않아 관련 함수(StartPatrol/ReturnToOfficeRoom/CallToOfficeExit)에서
// 유추 가능한 최소 상태만 정의
UENUM(BlueprintType)
enum class EPatrolState : uint8
{
	InOffice,
	Patrolling,
	ReturningToOffice
};

// Docs/04_Agent_AI.md §4 — 5단계 대상.
UCLASS()
class AFactoryNPCHuman : public AFactoryAgentBase
{
	GENERATED_BODY()

public:
	AFactoryNPCHuman();

	UPROPERTY(BlueprintReadOnly)
	EPatrolState PatrolState = EPatrolState::InOffice;

	UPROPERTY(BlueprintReadOnly)
	float PatrolStartTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Patrol")
	float MaxPatrolDurationSeconds = 30.f;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<AFactoryAgentBase> AssignedMaintenanceTarget;

	// Docs에 없는 구현값: 순찰 반경, 사무실 복귀 지점 (레벨마다 다르므로 EditAnywhere로 노출)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patrol")
	float PatrolRadius = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patrol")
	FTransform OfficeRoomTransform;

	// 버그 수정(사용자 지시) — AFactoryAgentBase::OnArrivedAtDestination()이 기본 빈 함수라, 사무실로
	// 복귀한 NPC가 실제로 도착해도 아무도 감지하지 못해 CurrentState/PatrolState가 Moving/
	// ReturningToOffice에 영구히 눌러붙었다(자동으로 다시 순찰을 재개시킬 방법이 없어, 정비를 한 번이라도
	// 마친 NPC는 사무실에 영구 정지 — 결국 전원이 같은 사무실 지점에 뭉치는 현상으로 재현). 도착 시
	// 0부터 이 값까지의 랜덤 정수(초)만큼 대기했다가 StartPatrol()을 다시 호출한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Patrol")
	int32 MaxOfficeWaitSeconds = 10;

	// Docs에 없는 구현값 — 순찰 단독(애니메이션/내비메시) 테스트용으로 BlueprintCallable 노출.
	UFUNCTION(BlueprintCallable)
	void StartPatrol();
	void AssignMaintenance(AFactoryAgentBase* Target, ERepairType RepairType);
	void ReturnToOfficeRoom();
	bool TryPossessByPlayer(APlayerController* RequestingController);
	void CallToOfficeExit();

	// 버그 수정(사용자 지시) — 사무실 도착을 실제로 감지해 대기 타이머를 거는 용도(위 MaxOfficeWaitSeconds 참고).
	virtual void OnArrivedAtDestination() override;

	// 8단계(Docs/02_Multiplayer_RPC.md) — 다른 플레이어가 이미 빙의 중인지만 확인한다.
	// AI 상태(정비/순찰 등)는 가로채기 허용 정책에 따라 체크하지 않는다.
	bool CanBePossessedBy(APlayerController* RequestingController) const;

	// 8단계 — 관전자 복귀 처리. AFactoryPlayerController::Server_ReleaseNPC가 자신의 관전자 폰을
	// 다시 Possess하기 직전에 호출해, 이 NPC는 AI 제어로 복귀시킨다.
	void ReleasePossession();

private:
	// MaxOfficeWaitSeconds만큼(0부터 랜덤) 대기한 뒤 StartPatrol()을 호출하는 타이머.
	FTimerHandle OfficeWaitTimerHandle;
};
