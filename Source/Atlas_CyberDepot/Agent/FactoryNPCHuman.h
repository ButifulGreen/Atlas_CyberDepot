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

	void StartPatrol();
	void AssignMaintenance(AFactoryAgentBase* Target, ERepairType RepairType);
	void ReturnToOfficeRoom();
	bool TryPossessByPlayer(APlayerController* RequestingController);
	void CallToOfficeExit();

	// 8단계(Docs/02_Multiplayer_RPC.md) — 다른 플레이어가 이미 빙의 중인지만 확인한다.
	// AI 상태(정비/순찰 등)는 가로채기 허용 정책에 따라 체크하지 않는다.
	bool CanBePossessedBy(APlayerController* RequestingController) const;

	// 8단계 — 관전자 복귀 처리. AFactoryPlayerController::Server_ReleaseNPC가 자신의 관전자 폰을
	// 다시 Possess하기 직전에 호출해, 이 NPC는 AI 제어로 복귀시킨다.
	void ReleasePossession();
};
