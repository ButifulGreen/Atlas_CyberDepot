# 4. 에이전트 / AI 제어 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §4. `AFactoryAgentBase`/`AFactoryAIController`는 구현 2단계(베이스 스켈레톤), `AFactoryAtlasRobot`/`AFactoryTransportRobot`/`AFactoryNPCHuman`의 실제 로직은 5단계(로봇 개별 동작)에서 채운다.

### `AFactoryAgentBase` (ACharacter)
- 멤버: `FGuid AgentID`, `EAgentType AgentType`, `EAgentState CurrentState`, `FVector TargetLocation`, `float BlockedTimer`, `static constexpr float BlockedThresholdSeconds = 2.f`
- 함수
  - `virtual void SetState(EAgentState NewState)`
  - `virtual void OnBlockedTick(float DeltaTime)`
  - `virtual void OnUnblocked()`
  - `FStateSnapshot ToSnapshot() const`
  - `virtual void OnArrivedAtDestination()` (6단계 신규 — `AFactoryAIController::OnMoveCompleted`가 이동 성공 시 호출. 기본 구현은 빈 함수, 아틀라스/배송로봇이 override)
  - `virtual void OnWorkingTick(float DeltaTime)` (6단계 신규 — `CurrentState==Working`인 동안 매 Tick 호출. 기본 구현은 빈 함수)
  - `virtual void Tick(float DeltaTime) override` (6단계 신규 — 서버 권한에서만 동작. `CurrentState==Working`이면 `OnWorkingTick` 호출. `CurrentState==Moving`이고 속도가 거의 0이면 `BlockedTimer` 누적, `BlockedThresholdSeconds` 최초 초과 시 1회 `OnBlockedTick` 호출(그 뒤로도 Blocked인 동안 매 틱 재호출), 재개 시 `OnUnblocked()` 1회 호출 후 타이머 리셋 — `ACostZoneVolume::RegisterBlocker`/`UnregisterBlocker`와 대칭되는 엣지 트리거)

### `AFactoryAIController` (ADetourCrowdAIController)
- 멤버: `TSubclassOf<UNavigationQueryFilter> QueryFilterClass`
- 함수
  - `virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override` (6단계 신규 — `Result.IsSuccess()`일 때 `Cast<AFactoryAgentBase>(GetPawn())->OnArrivedAtDestination()` 호출)
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
  - `FPendingSlotReservation PendingSlotReservation` (`ReserveNextSlot`이 채우고 `TransferItem`이 사용 후 초기화; 고장 후 재개 시 `ConfirmInbound`/`ConfirmOutboundRemoved` 호출 지점 복원에 사용)
  - `FGuid PendingHandoffAssignmentID` (6단계 신규 — `HandoffStationAssignment`가 이동 요청 전 채워 넣는, 아직 도착하지 않은 핸드오프 배정 ID. `OnArrivedAtDestination`에서 이 값이 유효하면 일반 작업 로직 대신 `OnHandoffAtlasArrivedAtStagingPoint` 호출을 우선한다)
  - `URepairProgressComponent* RepairComponent`
- 함수
  - `bool IsMaintenanceDue() const` (`OperationCount >= MaintenanceThreshold`를 반환; 별도 bool 멤버 없음)
  - `bool IsEligibleForQuickCheck() const` (`CurrentState == Idle && bIsParkedInIdleZone && IsMaintenanceDue()`)
  - `void AcceptStationAssignment(const FStationAssignment& Assignment, bool bIsHandoff = false)` (`UOutboundDispatchSubsystem`이 호출. 신규 배정 시 `StartCurrentAssignment()`로 존 예약+첫 이동을 킥오프하고 `FTaskLifecycleEvent(Assigned)` 발행; 핸드오프 인수(`bIsHandoff=true`) 시에는 이미 스테이징 지점에 도착해 있는 상태라 킥오프 없이 `CurrentAssignment`만 갱신)
  - `void EvaluateRotationOrContinue()` (이동/작업 재시도 진입점(`OnArrivedAtDestination`, `OnWorkingTick`)에서 호출. ① 고장 확률 롤 — 실패 시 `SetState(Broken)`. ② 통과 시 `IsMaintenanceDue()` 확인 — true이고 대기실에 초기화 로봇이 있으면 `UOutboundDispatchSubsystem::HandoffStationAssignment` 요청 후 대기실로 이동; 교대 불가 시 계속 진행)
  - `void TransferItem(AActor* Source, AActor* Destination)` (선반 슬롯/트레이/배송로봇 사이의 단일 전달 동작. `Source`/`Destination`이 `AFactoryTransportRobot*`이면 소켓 대 소켓으로 직접 주고받고 로봇 쪽 `OnItemCollectedByAtlas`/`OnItemGivenByAtlas`를 호출한다. 선반 분기는 `PendingSlotReservation`이 이미 유효하다고 가정하고 재예약하지 않는다)
  - `bool ReserveNextSlot()` (6단계 신규, private — `CurrentAssignment` 기준으로 `TryReserveEmptySlot`/`TryReserveOldestOccupiedSlot`을 호출해 `PendingSlotReservation`을 채운다. 이동 목적지 계산이 `TransferItem`보다 먼저 필요해서 분리)
  - `void StartCurrentAssignment()` (6단계 신규, private — 신규 `CurrentAssignment`에 대해 존 예약 + 첫 이동 요청)
  - `void ContinueShelfAssignment()` / `void ContinueTrayAssignment()` (6단계 신규, private — `OnArrivedAtDestination`/`OnWorkingTick`이 호출. emit/receive 통일 처리는 `07_TaskAssignment.md` 참고)
  - `AFactoryTransportRobot* FindWaitingTransportRobot(const FVector& Location, bool bNeedsPayload) const` (6단계 신규, private — `RendezvousSearchRadius` 안에서 짐 보유 여부가 일치하는 배송로봇 탐색)
  - `void OnAssignmentExhausted()` (RemainingCount==0, 거점 이탈 후 유휴 상태 전환 → `OnTaskCompleted()` 호출 → `UOutboundDispatchSubsystem::OnStationAssignmentCompleted` 경유로 `FTaskLifecycleEvent(Completed)` 발행 → 유휴 전환 즉시 `Dispatch->TryAssignIdleAtlas(this, ...)`로 다음 배정을 스스로 이어받는다)
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
  - `void AcceptTransportTask(const FTransportTask& Task)` (`UOutboundDispatchSubsystem`이 호출. `CurrentTask` 저장과 동시에 `PickupPoint` 방향(트레이면 `GetTransportRobotWorkLocation()`, 선반이면 `OutboundStagingTransform`)으로 이동 시작; `FTaskLifecycleEvent(Assigned)` 발행)
  - `void OnArrivedAtDestination()` (6단계 신규, override — 트레이/선반을 직접 건드리지 않고 `SetState(Working)`으로 파킹만 하고 `EvaluateRotationOrContinue()` 호출. 다음 단계는 아틀라스의 `OnItemGivenByAtlas`/`OnItemCollectedByAtlas` 호출을 기다린다)
  - `void OnItemGivenByAtlas(ALogisticsItem* Item)` (6단계 신규 — 아틀라스의 `TransferItem(Destination=this)`가 호출. `PayloadItem` 소켓 부착, `FTaskLifecycleEvent(PickedUp)` 발행, `DropoffPoint` 방향(선반이면 `InboundStagingTransform`)으로 이동 재개. 기존 `OnItemPickedUp()`을 대체)
  - `void OnItemCollectedByAtlas()` (6단계 신규 — 아틀라스의 `TransferItem(Source=this)`가 호출. `PayloadItem` 비우고 `OnTaskCompleted()` 호출)
  - `FVector GetTaskPointLocation(AActor* PointActor, bool bIsPickupSide) const` (6단계 신규, private — Pickup/Dropoff 지점을 트레이/선반 타입과 방향에 맞는 실제 좌표로 변환)
  - `void EvaluateRotationOrContinue()` (`OnArrivedAtDestination`에서 호출. ① 고장 확률 롤 — 실패 시 `SetState(Broken)`. ② 통과 시 `IsMaintenanceDue()` 확인 — true이고 `PayloadItem`이 비어있으며 대기실에 초기화 로봇이 있으면 대기실로 이동; 교대 불가 시 계속 진행)
  - `void OnEnterBlockedState()` / `virtual void OnBlockedTick(float DeltaTime) override` / `virtual void OnUnblocked() override` (6단계 신규 — `OnBlockedTick` 진입 엣지 1회에서 `OnEnterBlockedState()`가 근접 `ACostZoneVolume`들에 `RegisterBlocker` 등록 + 목록 기억, `OnUnblocked()`에서 같은 목록에 `UnregisterBlocker` 호출 후 초기화)
  - `void OnTaskCompleted()` (`SetState(Idle)`; `OperationCount` 증가; `MaxBreakdownChanceCap` 적용; `UOutboundDispatchSubsystem::OnTransportTaskCompleted` 경유로 `FTaskLifecycleEvent(Completed)` 발행; 유휴 전환 즉시 `Dispatch->TryAssignIdleTransportRobot(this, ...)`로 다음 트립을 스스로 이어받는다)

### `AFactoryNPCHuman` (AFactoryAgentBase)
- 멤버: `EPatrolState PatrolState`, `float PatrolStartTime`, `float MaxPatrolDurationSeconds`, `AFactoryAgentBase* AssignedMaintenanceTarget`
- 함수
  - `void StartPatrol()`
  - `void AssignMaintenance(AFactoryAgentBase* Target, ERepairType RepairType)` (`AssignedMaintenanceTarget` 설정과 동시에 `AFactoryAIController::SetAvoidanceIgnoreActor(Target, true)` 호출로 좁은 구간에서도 접근 가능하도록 회피 예외 등록; 정비 종료 시 `false`로 해제)
  - `void ReturnToOfficeRoom()`
  - `bool TryPossessByPlayer(APlayerController* Controller)` (FieldWorker Role 검증은 호출 측 `Server_RequestPossessNPC`에서 수행)
  - `void CallToOfficeExit()`
