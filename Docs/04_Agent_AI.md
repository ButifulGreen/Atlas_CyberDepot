# 4. 에이전트 / AI 제어 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §4. `AFactoryAgentBase`/`AFactoryAIController`는 구현 2단계(베이스 스켈레톤), `AFactoryAtlasRobot`/`AFactoryTransportRobot`/`AFactoryNPCHuman`의 실제 로직은 5단계(로봇 개별 동작)에서 채운다.

### `AFactoryAgentBase` (ACharacter)
- 멤버: `FGuid AgentID`, `EAgentType AgentType`, `EAgentState CurrentState`, `FVector TargetLocation`, `float BlockedTimer`, `static constexpr float BlockedThresholdSeconds = 2.f`
- 함수
  - `virtual void SetState(EAgentState NewState)`
  - `virtual void OnBlockedTick(float DeltaTime)`
  - `virtual void OnUnblocked()`
  - `FStateSnapshot ToSnapshot() const`

### `AFactoryAIController` (ADetourCrowdAIController)
- 멤버: `TSubclassOf<UNavigationQueryFilter> QueryFilterClass`
- 함수
  - `virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override`
  - `void RequestMoveWithFilter(const FVector& Destination)` (이동 요청 직전 `ApplyDynamicCongestionCost`를 통해 `QueryFilterClass` 인스턴스의 AreaCost를 현재 혼잡도 기준으로 갱신한 뒤 이동 요청)
  - `void ApplyDynamicCongestionCost(UNavigationQueryFilter* Filter)` (경로 주변 `ACostZoneVolume`들의 `GetCurrentCostMultiplier()`를 조회해 Filter의 AreaCost에 반영. NavMesh 지오메트리는 건드리지 않음 — `08_Navigation.md` 참고)
  - `void SetAvoidanceIgnoreActor(AActor* TargetActor, bool bIgnore)` (Crowd Avoidance 컴포넌트에서 특정 액터를 무시/재고려 대상으로 토글. NPC가 `AssignedMaintenanceTarget`에 접근할 때 사용)

### `FPendingSlotReservation` (USTRUCT)
아틀라스가 `TransferItem` 도중 고장났을 때 수리 완료 후 재개(Resume)에 필요한 슬롯 정보를 보존한다. `bIsValid == false`이면 현재 진행 중인 예약이 없음을 의미한다.
- `bool bIsValid = false`
- `int32 FloorIndex = -1`
- `int32 SlotIndex = -1`

### `AFactoryAtlasRobot` (AFactoryAgentBase)
- 멤버
  - `FStationAssignment CurrentAssignment` (거점 배정, 비어있으면 유휴 상태)
  - `ALogisticsItem* HeldItem` (동시에 1개만 보유; 로봇 스켈레탈 메시 전용 소켓에 `AttachToComponent`로 부착. 물리 홀딩 미사용)
  - `int32 OperationCount`, `int32 MaintenanceThreshold`
  - `float BreakdownChanceBase`, `float BreakdownChanceOverageMultiplier`
  - `static constexpr float MaxBreakdownChanceCap = 0.40f` (고장 확률 상한 40%; `OperationCount >= MaintenanceThreshold` 이후 5회 단위로 확률이 누적되지만 이 값을 초과하지 않음)
  - `FPendingSlotReservation PendingSlotReservation` (`TransferItem` 직전에 기록, 완료 후 초기화; 고장 후 재개 시 `ConfirmInbound`/`ConfirmOutboundRemoved` 호출 지점 복원에 사용)
  - `URepairProgressComponent* RepairComponent`
- 함수
  - `bool IsMaintenanceDue() const` (`OperationCount >= MaintenanceThreshold`를 반환; 별도 bool 멤버 없음)
  - `bool IsEligibleForQuickCheck() const` (`CurrentState == Idle && bIsParkedInIdleZone && IsMaintenanceDue()`)
  - `void AcceptStationAssignment(const FStationAssignment& Assignment)` (`UOutboundDispatchSubsystem`이 호출; 신규 배정과 교대 핸드오프 인수 양쪽에 동일하게 사용. 신규 배정 시 `FTaskLifecycleEvent(Assigned)` 발행; 핸드오프 인수 시에는 원 배정의 `TaskOrAssignmentID`를 그대로 유지하므로 별도 `Assigned` 이벤트를 재발행하지 않는다)
  - `void EvaluateRotationOrContinue()` (이동 직전 또는 `TransferItem` 직전에 호출. ① 고장 확률 롤 — 실패 시 `SetState(Broken)`. ② 통과 시 `IsMaintenanceDue()` 확인 — true이고 대기실에 초기화 로봇이 있으면 `UOutboundDispatchSubsystem::HandoffStationAssignment` 요청 후 대기실로 이동; 교대 불가 시 계속 진행)
  - `void TransferItem(AActor* Source, AActor* Destination)` (선반 슬롯 ↔ 운송로봇 ↔ 트레이 사이의 단일 전달 동작; 호출 직전 `PendingSlotReservation` 기록, 완료 시 초기화)
  - `void OnAssignmentExhausted()` (RemainingCount==0, 거점 이탈 후 유휴 상태 전환 → `OnTaskCompleted()` 호출 → `UOutboundDispatchSubsystem::OnStationAssignmentCompleted` 경유로 `FTaskLifecycleEvent(Completed)` 발행)
  - `void OnTaskCompleted()` (`OperationCount` 증가; 누적 고장 확률은 `OperationCount >= MaintenanceThreshold` 이후 `BreakdownChanceBase + (초과 5회 단위 × BreakdownChanceOverageMultiplier)`로 계산하되 `MaxBreakdownChanceCap`으로 상한 고정. 임계치 초과가 지속되면 `Code:003` 발행 시 현재 확률값을 `FAnomalyEvent::RiskValue`에 기록)

### `AFactoryTransportRobot` (AFactoryAgentBase)
- 멤버
  - `FTransportTask CurrentTask` (트립 작업, 비어있으면 유휴 상태)
  - `ALogisticsItem* PayloadItem` (항상 최대 1개; 로봇 전용 소켓에 `AttachToComponent`로 부착)
  - `int32 OperationCount`, `int32 MaintenanceThreshold`
  - `float BreakdownChanceBase`, `float BreakdownChanceOverageMultiplier`
  - `static constexpr float MaxBreakdownChanceCap = 0.40f`
  - `URepairProgressComponent* RepairComponent`
- 함수
  - `bool IsMaintenanceDue() const` (`OperationCount >= MaintenanceThreshold`)
  - `bool IsEligibleForQuickCheck() const`
  - `void AcceptTransportTask(const FTransportTask& Task)` (`UOutboundDispatchSubsystem`이 호출; `FTaskLifecycleEvent(Assigned)` 발행)
  - `void OnItemPickedUp()` (`PickupPoint`에서 `PayloadItem` 소켓 부착 완료 시 호출; `FTaskLifecycleEvent(PickedUp)` 발행)
  - `void EvaluateRotationOrContinue()` (이동 직전에 호출. ① 고장 확률 롤 — 실패 시 `SetState(Broken)`. ② 통과 시 `IsMaintenanceDue()` 확인 — true이고 `PayloadItem`이 비어있으며 대기실에 초기화 로봇이 있으면 대기실로 이동; 교대 불가 시 `TryAssignIdleTransportRobot` 경유로 다음 트립 수령)
  - `void OnEnterBlockedState()` (2초 경과 시 호출, 동적 코스트 주입)
  - `void OnTaskCompleted()` (`OperationCount` 증가; `MaxBreakdownChanceCap` 적용; `UOutboundDispatchSubsystem::OnTransportTaskCompleted` 경유로 `FTaskLifecycleEvent(Completed)` 발행)

### `AFactoryNPCHuman` (AFactoryAgentBase)
- 멤버: `EPatrolState PatrolState`, `float PatrolStartTime`, `float MaxPatrolDurationSeconds`, `AFactoryAgentBase* AssignedMaintenanceTarget`
- 함수
  - `void StartPatrol()`
  - `void AssignMaintenance(AFactoryAgentBase* Target, ERepairType RepairType)` (`AssignedMaintenanceTarget` 설정과 동시에 `AFactoryAIController::SetAvoidanceIgnoreActor(Target, true)` 호출로 좁은 구간에서도 접근 가능하도록 회피 예외 등록; 정비 종료 시 `false`로 해제)
  - `void ReturnToOfficeRoom()`
  - `bool TryPossessByPlayer(APlayerController* Controller)` (FieldWorker Role 검증은 호출 측 `Server_RequestPossessNPC`에서 수행)
  - `void CallToOfficeExit()`
