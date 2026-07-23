# 1. Core / 이벤트 버스 / 데이터 파이프라인

> Atlas_CyberDepot 아키텍처 설계안 v5 — §1. 구현 1단계(Core) 대상. 다른 모든 시스템이 이 이벤트 버스에 구독하므로 제일 먼저 뼈대가 서 있어야 한다.

### `UFactoryEventBusSubsystem` (UGameInstanceSubsystem)
- 멤버: `FOnAnomalyEvent OnAnomalyPublished`, `FOnStateSnapshot OnSnapshotPublished`, `FOnTaskLifecycleEvent OnTaskLifecyclePublished`, `FOnTrainingLogEntry OnTrainingLogPublished`
- 함수
  - `void PublishAnomaly(const FAnomalyEvent& Event)`
  - `void PublishSnapshot(const FStateSnapshot& Snapshot)`
  - `void PublishTaskLifecycle(const FTaskLifecycleEvent& Event)`
  - `void PublishTrainingLogEntry(const FTrainingLogEntry& Entry)`
  - `FDelegateHandle SubscribeAnomaly(const FOnAnomalyEvent::FDelegate& Callback)`
  - `FDelegateHandle SubscribeSnapshot(const FOnStateSnapshot::FDelegate& Callback)`
  - `FDelegateHandle SubscribeTaskLifecycle(const FOnTaskLifecycleEvent::FDelegate& Callback)`
  - `FDelegateHandle SubscribeTrainingLogEntry(const FOnTrainingLogEntry::FDelegate& Callback)`
  - `void Unsubscribe(FDelegateHandle Handle)`

### `FAnomalyEvent` (USTRUCT)
- `uint8 SchemaVersion = 1`
- `FDateTime Timestamp`
- `FGuid LogID`
- `EEventSeverity Severity`
- `FGuid ActorID`
- `EActorType ActorType`
- `FName AnomalyCode` (`Code:001` 교착상태, `Code:002` 세이프티존 침범, `Code:003` 예방정비 미실시 누적, `Code:004` 선반 포화로 입고 정지, `Code:005` 로봇 고장 발생, `Code:006` 정비 완료)
  - `Code:002`는 `AFactoryAgentBase::RunSafetyTraceCheck`(`08_Navigation.md` §8-B, Docs 이탈 승인됨)가 정면 라인트레이스로 다른 에이전트를 감지할 때마다 발행한다. `TargetLocation`=감지 대상 위치, `NearestObstacleDistance`=트레이스 거리, `bSafetyZoneStatus=true`, `InterrupterType`=감지 대상 타입.
- `FVector Location`
- `FVector Velocity`
- `FVector TargetLocation`
- `float NearestObstacleDistance`
- `bool bSafetyZoneStatus`
- `EInterrupterType InterrupterType`
- `float RiskValue = 0.f` (`Code:003` 발행 시 해당 로봇의 현재 누적 `BreakdownChance` 수치를 기록. 다른 코드에서는 0으로 둔다. 로봇 배치 대수별 위험도 추이를 사후 분석하기 위한 필드)

### `FTaskLifecycleEvent` (USTRUCT)
태스크/거점 배정 단위의 시작~종료 흐름을 추적하기 위한 경량 이벤트. `FAnomalyEvent`와 별도 구조체로 둔다(의미가 다른 데이터를 하나의 스키마에 억지로 우겨넣지 않기 위함 — §0 "게임플레이 로직과 데이터 출력 분리" 원칙과 정합).
- `uint8 SchemaVersion = 1`
- `FDateTime Timestamp`
- `FGuid EventID`
- `FGuid TaskOrAssignmentID` (`FTransportTask::TaskID` 또는 `FStationAssignment::AssignmentID`)
- `ETaskLifecycleEventType EventType`
- `FGuid ActorID`
- `EActorType ActorType`
- `EItemType ItemType`

### `ETaskLifecycleEventType` (enum)
- `Assigned` (운송로봇의 `AcceptTransportTask` 또는 아틀라스의 `AcceptStationAssignment` 시점)
- `PickedUp` (운송로봇이 `PickupPoint`에서 물품을 소켓에 부착 완료한 시점; 아틀라스의 거점 배정에는 해당 없음)
- `Completed` (운송로봇의 `OnTransportTaskCompleted` 또는 아틀라스의 `OnStationAssignmentCompleted` 시점)

### `FStateSnapshot` (USTRUCT)
- `uint8 SchemaVersion = 1`
- `FDateTime Timestamp`
- `FGuid ActorID`
- `EActorType ActorType`
- `EAgentState CurrentState`
- `FVector Location`
- `FRotator Rotation`
- `FVector Velocity`
- `FString DisplayName`(사용자 지시, 신규 — 리플레이 고스트 이름표용, 아래 §10 참고)
- 리플레이 재생 전용(`Docs/10_Benchmark_Replay.md`). `AFactoryAgentBase::PublishReplaySnapshot`이 발행 — 이동
  중(`CurrentState==Moving`)엔 `ReplaySnapshotIntervalSeconds`(기본 0.1초) 주기로, 그 외 상태 변화는
  `OnRep_CurrentState`에서 즉시 발행한다. 서버/클라이언트 각자 권한과 무관하게 자기가 보는 값을 독립적으로
  발행한다(리슨 서버 멀티플레이에서 각 플레이어가 리플레이를 각자 로컬로 볼 수 있어야 하므로 — RPC 없이
  EventBus가 원래 `UGameInstanceSubsystem` 기반이라 인스턴스별로 이미 분리되어 있는 점을 그대로 활용).
- `FString DisplayName`(사용자 지시, 신규 — `AFactoryAgentBase::DisplayName`을 그대로 실어 보낸다). 재생
  중 고스트 액터(`AReplayGhostActor`)가 이름표로 띄우는 데 쓴다 — `ActorID`(GUID)만으로는 사람이 못
  알아보고, 재생 시점엔 원본 라이브 액터를 다시 찾아 조회하는 것도 불안정해(세션이 바뀌었거나 이미
  사라졌을 수 있음) 스냅샷 자체에 실어 보내는 쪽을 택했다.

### `FTrainingLogEntry` (USTRUCT)
AI 학습용 데이터 전용 — `FStateSnapshot`과 소비자·보존 정책이 달라(전자는 리플레이 재생용으로 블랙박스
폐기 대상, 후자는 계속 누적 보존) 별도 구조체로 분리했다(§0 "게임플레이 로직과 데이터 출력 분리" 원칙).
상태 변화 시점과 이동 목적지가 결정되는 시점에만 이벤트 기반으로 발행하며(시간 기준 주기 폴링이 아님 —
이산적 결정에 폴링을 적용하면 짧게 있다 사라진 상태를 놓칠 수 있음), `UTrainingDataRecorderSubsystem`이
구독해 매 항목을 즉시 파일에 실시간 기록한다.
- `uint8 SchemaVersion = 2` (2026-07-23 — 아래 6개 필드 추가로 1→2 상향. 이전 버전 기록 파일엔 이 필드들이
  아예 없다)
- `FDateTime Timestamp`
- `float ElapsedSinceLastEntrySeconds = -1.f` (이 액터의 직전 학습 로그 기록 이후 경과 시간. 기록기가
  기록 시점에 채운다. 최초 기록이면 -1)
- `FGuid ActorID`
- `EActorType ActorType`
- `EAgentState CurrentState`
- `FVector Location`
- `FVector MoveDestination = ZeroVector` (이동 결정 기록 전용 — 상태 변화만으로 발행된 항목은 ZeroVector)
- `FName SelectedWaypointName = NAME_None` (웨이포인트 그래프를 경유한 이동이면 그 웨이포인트 액터 이름,
  마커 좌표로 직행(FinalHop)했거나 이동이 아닌 항목이면 NAME_None)
- `bool bIsCarryingItem` + `EItemType CarriedItemType`(신규, 2026-07-23 — 사용자 지시 "아주 자세한 정보")
  아틀라스(`HeldItem`)/배송로봇(`PayloadItem`)의 소지 아이템. `EItemType`엔 "없음" 값이 없어 별도 bool로
  구분(빈 손/NPC면 `bIsCarryingItem=false`, 이때 `CarriedItemType`은 의미 없음).
- `float RepairProgress`(신규) — `AFactoryAgentBase::GetRepairComponent()`가 있는(Atlas/TransportRobot)
  에이전트만, 그마저도 지금 수리 중이 아니면 0.
- `FGuid CurrentAssignmentID`(신규) — 아틀라스는 `FStationAssignment::AssignmentID`, 배송로봇은
  `FTransportTask::TaskID`. 배정 없음(NPC, 또는 아직 못 받은 Idle)이면 Invalid Guid.
- `int32 OperationCount`(신규) — `AFactoryAgentBase::GetOperationCount()`.
- `bool bIsPlayerControlled`(신규) — `APawn::IsPlayerControlled()`. NPC 빙의 중인지 여부. Enhanced Input
  직접 이동은 `SetState`를 거치지 않아 `CurrentState`만으로는 "AI 판단인지 사람 조작인지" 구분이 안
  된다는 문제를 이번 세션 초반 리플레이 스냅샷 발행 조건에서 실제로 겪었다(§10 구현 비고 참고) — 학습
  데이터 소비자가 같은 함정을 피하도록 명시적으로 싣는다.
- **리플리케이션 주의** — `CurrentAssignmentID`/`OperationCount`의 원본 필드(`CurrentAssignment`/
  `CurrentTask`/`OperationCount`)는 리플리케이트되지 않는 서버 전용 부기 값이다. 두 기록기 모두 서버/
  클라이언트 각자 독립적으로 발행하는 구조라(위 참고), **클라이언트 인스턴스가 발행한 로그의 이 두
  필드는 정확하지 않을 수 있다** — 서버 인스턴스가 발행한 학습 로그 파일을 신뢰할 것. 나머지 신규
  필드(`bIsCarryingItem`/`CarriedItemType`/`RepairProgress`/`bIsPlayerControlled`)의 원본 필드는 이미
  리플리케이트돼 있어 클라이언트에서도 정확하다.
- `AFactoryAgentBase::EmitTrainingLogEntry`/`EmitMovementTrainingLog`가 발행 — `OnRep_CurrentState`(상태
  변화 즉시)와 `AIController->RequestMoveWithFilter`를 호출하는 6개 지점(`TryRequestWaypointRoute`,
  `TryHandleWaypointRouteArrival`, `RetryNextHopReservation`, `RecheckWaitboundClearance`) 각각에서 호출한다.

### `FAnomalyCodeDefinition` (FTableRowBase, DataTable 행)
- `FName Code`
- `FText DisplayName`
- `FText ManualDescription`
- `EEventSeverity DefaultSeverity`
