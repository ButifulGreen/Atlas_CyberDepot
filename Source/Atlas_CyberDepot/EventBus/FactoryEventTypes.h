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

	// 사용자 지시, 신규 — 리플레이 재생 시 고스트 액터가 어느 실제 로봇을 대신하는지 이름표로 보여주는 데
	// 쓴다(AFactoryAgentBase::DisplayName 그대로). ActorID(GUID)만으로는 사람이 못 알아보고, 재생 시점엔
	// 원본 라이브 액터를 다시 찾아 조회하는 것도 불안정해(세션이 바뀌었거나 이미 사라졌을 수 있음) 스냅샷
	// 자체에 실어 보낸다.
	UPROPERTY(BlueprintReadOnly)
	FString DisplayName;
};

// Docs/10_Benchmark_Replay.md — AI 학습용 데이터 전용 로그. FStateSnapshot(리플레이 재생용 transform 위주,
// 0.1초 주기)과 다른 소비자·다른 보존 정책(블랙박스로 폐기되지 않고 계속 누적)을 가져 별도 구조체로 분리했다
// (§0 "게임플레이 로직과 데이터 출력 분리" 원칙과 동일하게, 의미가 다른 데이터를 하나의 스키마에 억지로
// 우겨넣지 않기 위함). 상태 변화 시점, 그리고 이동 목적지가 결정되는 시점에만 이벤트 기반으로 발행한다
// (주기 폴링 아님 — 이산적 결정에 시간 기준 샘플링을 적용하면 짧게 있다 사라진 상태를 놓칠 수 있어서).
USTRUCT(BlueprintType)
struct FTrainingLogEntry
{
	GENERATED_BODY()

	// 2026-07-23 — 아이템/정비/배정/조작 주체 필드 추가로 2로 상향(아래 참고). 이전 버전 기록 파일은 이
	// 필드들이 전부 기본값으로 채워지지 않고 아예 없는 상태였다.
	UPROPERTY(BlueprintReadOnly)
	uint8 SchemaVersion = 2;

	UPROPERTY(BlueprintReadOnly)
	FDateTime Timestamp;

	// 이 액터의 직전 학습 로그 기록 이후 경과 시간(초) — UTrainingDataRecorderSubsystem이 기록 시점에 채운다.
	// 최초 기록이면 -1.
	UPROPERTY(BlueprintReadOnly)
	float ElapsedSinceLastEntrySeconds = -1.f;

	UPROPERTY(BlueprintReadOnly)
	FGuid ActorID;

	UPROPERTY(BlueprintReadOnly)
	EActorType ActorType = EActorType::AtlasRobot;

	UPROPERTY(BlueprintReadOnly)
	EAgentState CurrentState = EAgentState::Idle;

	UPROPERTY(BlueprintReadOnly)
	FVector Location = FVector::ZeroVector;

	// 이동 결정 기록 전용 — 상태 변화만으로 발행된 항목은 ZeroVector로 남는다.
	UPROPERTY(BlueprintReadOnly)
	FVector MoveDestination = FVector::ZeroVector;

	// 웨이포인트 그래프를 경유한 이동이면 그 웨이포인트 액터 이름, 마커 좌표로 직행(FinalHop)한 경우거나
	// 이동이 아닌 항목이면 NAME_None.
	UPROPERTY(BlueprintReadOnly)
	FName SelectedWaypointName = NAME_None;

	// 이하 사용자 지시(2026-07-23, "아주 자세한 정보")로 추가된 필드 — SchemaVersion 1→2.

	// 소지 아이템 — 아틀라스(HeldItem)/배송로봇(PayloadItem)만 해당. 빈 손이거나 NPC면 false(그때
	// CarriedItemType은 의미 없음 — EItemType엔 "없음" 값이 없어 별도 bool로 구분).
	UPROPERTY(BlueprintReadOnly)
	bool bIsCarryingItem = false;

	UPROPERTY(BlueprintReadOnly)
	EItemType CarriedItemType = EItemType::ItemA;

	// 정비 진행도(0~해당 정비 유형의 목표 시간) — GetRepairComponent()가 있는(Atlas/TransportRobot)
	// 에이전트만, 그마저도 지금 수리 중이 아니면 0.
	UPROPERTY(BlueprintReadOnly)
	float RepairProgress = 0.f;

	// 현재 배정/트립 ID — 아틀라스는 FStationAssignment::AssignmentID, 배송로봇은 FTransportTask::TaskID.
	// 배정이 없으면(NPC, 또는 아직 배정을 못 받은 Idle) 기본 FGuid(Invalid).
	// 주의 — 원본 필드(CurrentAssignment/CurrentTask)가 리플리케이트되지 않아, 클라이언트 인스턴스가 직접
	// 발행하는 로그에서는 이 값이 정확하지 않을 수 있다(서버 인스턴스가 발행한 로그만 신뢰할 것).
	UPROPERTY(BlueprintReadOnly)
	FGuid CurrentAssignmentID;

	// 지금까지 완료한 작업 수 — AFactoryAgentBase::GetOperationCount(). 위 CurrentAssignmentID와 동일한
	// 이유로 원본 필드가 리플리케이트되지 않아, 서버 인스턴스가 발행한 로그만 신뢰할 것.
	UPROPERTY(BlueprintReadOnly)
	int32 OperationCount = 0;

	// 사용자(플레이어)가 지금 이 에이전트를 직접 조작 중인지(NPC 빙의). Enhanced Input 직접 이동은
	// SetState를 거치지 않아 CurrentState만으로는 "AI 판단인지 사람 조작인지"를 구분할 수 없다는 문제를
	// 이번 세션 초반에 리플레이 스냅샷 발행 조건에서 실제로 겪었다(Docs/10_Benchmark_Replay.md 구현 비고
	// 참고) — 학습 데이터 소비자가 같은 문제를 겪지 않도록 명시적으로 싣는다.
	UPROPERTY(BlueprintReadOnly)
	bool bIsPlayerControlled = false;
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
