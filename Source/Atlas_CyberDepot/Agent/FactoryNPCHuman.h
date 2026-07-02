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
	bool TryPossessByPlayer(APlayerController* Controller);
	void CallToOfficeExit();
};
