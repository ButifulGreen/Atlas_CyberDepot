# 1. Core / 이벤트 버스 / 데이터 파이프라인

> Atlas_CyberDepot 아키텍처 설계안 v5 — §1. 구현 1단계(Core) 대상. 다른 모든 시스템이 이 이벤트 버스에 구독하므로 제일 먼저 뼈대가 서 있어야 한다.

### `UFactoryEventBusSubsystem` (UGameInstanceSubsystem)
- 멤버: `FOnAnomalyEvent OnAnomalyPublished`, `FOnStateSnapshot OnSnapshotPublished`, `FOnTaskLifecycleEvent OnTaskLifecyclePublished`
- 함수
  - `void PublishAnomaly(const FAnomalyEvent& Event)`
  - `void PublishSnapshot(const FStateSnapshot& Snapshot)`
  - `void PublishTaskLifecycle(const FTaskLifecycleEvent& Event)`
  - `FDelegateHandle SubscribeAnomaly(const FOnAnomalyEvent::FDelegate& Callback)`
  - `FDelegateHandle SubscribeSnapshot(const FOnStateSnapshot::FDelegate& Callback)`
  - `FDelegateHandle SubscribeTaskLifecycle(const FOnTaskLifecycleEvent::FDelegate& Callback)`
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

### `FAnomalyCodeDefinition` (FTableRowBase, DataTable 행)
- `FName Code`
- `FText DisplayName`
- `FText ManualDescription`
- `EEventSeverity DefaultSeverity`
