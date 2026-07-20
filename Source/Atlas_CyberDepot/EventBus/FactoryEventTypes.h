// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "FactoryEventTypes.generated.h"

// Docs/01_EventBus_DataPipeline.md §1
UENUM(BlueprintType)
enum class EEventSeverity : uint8
{
	Info,
	Warning,
	Critical
};

// Docs/01_EventBus_DataPipeline.md §1 (FAnomalyEvent/FStateSnapshot/FTaskLifecycleEvent 공용)
UENUM(BlueprintType)
enum class EActorType : uint8
{
	AtlasRobot,
	TransportRobot,
	NPCHuman
};

// Docs/01_EventBus_DataPipeline.md §1 (FAnomalyEvent 전용, 경로를 막는 주체 분류)
UENUM(BlueprintType)
enum class EInterrupterType : uint8
{
	NPC,
	PossessedPlayer,
	AtlasRobot,
	TransportRobot
};

// Docs/04_Agent_AI.md §4 AFactoryAgentBase::CurrentState, Docs/01 FStateSnapshot::CurrentState 공용
UENUM(BlueprintType)
enum class EAgentState : uint8
{
	Patrolling,
	Moving,
	Idle,
	Working,
	UnderRepair,
	Broken,
	// 버그 수정(사용자 지시, 회피 재설계) — 안전거리/FinalHop 트레이스가 정지시킨 "대기+재확인" 상태.
	// 기존엔 bYieldingForSafety라는 bool을 Moving 위에 얹어 표현했는데, 그러면 Moving이 "이동 의도"와
	// "실제 이동 중"을 동시에 뜻해 데이터 가치가 떨어졌다. 끝에 추가 — 리플리케이트되는 값이라 기존
	// 값 순서를 바꾸지 않는다.
	Pause
};

// Docs/01_EventBus_DataPipeline.md §1
UENUM(BlueprintType)
enum class ETaskLifecycleEventType : uint8
{
	Assigned,
	PickedUp,
	Completed
};

// Docs/06_Infrastructure.md §6에서 정의되지만 FTaskLifecycleEvent가 참조하므로 여기서 함께 정의
UENUM(BlueprintType)
enum class EItemType : uint8
{
	ItemA,
	ItemB,
	ItemC
};

USTRUCT(BlueprintType)
struct FAnomalyEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	uint8 SchemaVersion = 1;

	UPROPERTY(BlueprintReadOnly)
	FDateTime Timestamp;

	UPROPERTY(BlueprintReadOnly)
	FGuid LogID;

	UPROPERTY(BlueprintReadOnly)
	EEventSeverity Severity = EEventSeverity::Info;

	UPROPERTY(BlueprintReadOnly)
	FGuid ActorID;

	UPROPERTY(BlueprintReadOnly)
	EActorType ActorType = EActorType::AtlasRobot;

	// Code:001 교착상태, Code:002 세이프티존 침범, Code:003 예방정비 미실시 누적,
	// Code:004 선반 포화로 입고 정지, Code:005 로봇 고장 발생
	UPROPERTY(BlueprintReadOnly)
	FName AnomalyCode;

	UPROPERTY(BlueprintReadOnly)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float NearestObstacleDistance = 0.f;

	UPROPERTY(BlueprintReadOnly)
	bool bSafetyZoneStatus = false;

	UPROPERTY(BlueprintReadOnly)
	EInterrupterType InterrupterType = EInterrupterType::NPC;

	// Code:003 발행 시 해당 로봇의 현재 누적 BreakdownChance 기록. 그 외 코드는 0.
	UPROPERTY(BlueprintReadOnly)
	float RiskValue = 0.f;
};

USTRUCT(BlueprintType)
struct FTaskLifecycleEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	uint8 SchemaVersion = 1;

	UPROPERTY(BlueprintReadOnly)
	FDateTime Timestamp;

	UPROPERTY(BlueprintReadOnly)
	FGuid EventID;

	// FTransportTask::TaskID 또는 FStationAssignment::AssignmentID
	UPROPERTY(BlueprintReadOnly)
	FGuid TaskOrAssignmentID;

	UPROPERTY(BlueprintReadOnly)
	ETaskLifecycleEventType EventType = ETaskLifecycleEventType::Assigned;

	UPROPERTY(BlueprintReadOnly)
	FGuid ActorID;

	UPROPERTY(BlueprintReadOnly)
	EActorType ActorType = EActorType::AtlasRobot;

	UPROPERTY(BlueprintReadOnly)
	EItemType ItemType = EItemType::ItemA;
};

USTRUCT(BlueprintType)
struct FStateSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	uint8 SchemaVersion = 1;

	UPROPERTY(BlueprintReadOnly)
	FDateTime Timestamp;

	UPROPERTY(BlueprintReadOnly)
	FGuid ActorID;

	UPROPERTY(BlueprintReadOnly)
	EActorType ActorType = EActorType::AtlasRobot;

	UPROPERTY(BlueprintReadOnly)
	EAgentState CurrentState = EAgentState::Idle;

	UPROPERTY(BlueprintReadOnly)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly)
	FVector Velocity = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FAnomalyCodeDefinition : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Code;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText ManualDescription;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EEventSeverity DefaultSeverity = EEventSeverity::Info;
};
