# 4. 에이전트 / AI 제어 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §4. `AFactoryAgentBase`/`AFactoryAIController`는 구현 2단계(베이스 스켈레톤), `AFactoryAtlasRobot`/`AFactoryTransportRobot`/`AFactoryNPCHuman`의 실제 로직은 5단계(로봇 개별 동작)에서 채운다.

### `AFactoryAgentBase` (ACharacter)
- 멤버: `FGuid AgentID`, `EAgentType AgentType`, `EAgentState CurrentState`, `FVector TargetLocation`, `float BlockedTimer`, `static constexpr float BlockedThresholdSeconds = 2.f`, `bool bIsParkedInIdleZone`(`AIdleWaitingZone::TryOccupyHomeSlot`/`ReleaseSlot`이 갱신, "지금 앉아있음"), `TWeakObjectPtr<AIdleWaitingZone> HomeIdleZone` + `int32 HomeSlotIndex`(7단계 후속 — 선입선출 없이 레벨 시작 시 1회만 고정 배정되는 홈 대기실/슬롯. `UOutboundDispatchSubsystem::AssignHomeIdleZoneSlots`가 채운 뒤로는 세션 내내 유지됨), `bool bIsHeadingToIdleZone`(7단계 후속 — 대기실로 이동 중인 동안만 true, 도착하면 즉시 false. `bIsParkedInIdleZone`과 달리 "이동 중"만을 뜻해 `OnArrivedAtDestination`이 대기실행 도착 여부를 판별하는 데 쓰인다. 위 세 값 모두 `CurrentAssignment`/`PendingHandoffAssignmentID`와 동일하게 리플리케이트하지 않음)
- 함수
  - `virtual void SetState(EAgentState NewState)`
  - `virtual void OnBlockedTick(float DeltaTime)`
  - `virtual void OnUnblocked()`
  - `FStateSnapshot ToSnapshot() const`
  - `virtual void OnArrivedAtDestination()` (6단계 신규 — `AFactoryAIController::OnMoveCompleted`가 이동 성공 시 호출. 기본 구현은 빈 함수, 아틀라스/배송로봇이 override)
  - `virtual void OnWorkingTick(float DeltaTime)` (6단계 신규 — `CurrentState==Working`인 동안 매 Tick 호출. 기본 구현은 빈 함수)
  - `virtual void Tick(float DeltaTime) override` (6단계 신규 — 서버 권한에서만 동작. `CurrentState==Working`이면 `OnWorkingTick` 호출. `CurrentState==Moving`이고 속도가 거의 0이면 `BlockedTimer` 누적, `BlockedThresholdSeconds` 최초 초과 시 1회 `OnBlockedTick` 호출(그 뒤로도 Blocked인 동안 매 틱 재호출), 재개 시 `OnUnblocked()` 1회 호출 후 타이머 리셋 — `ACostZoneVolume::RegisterBlocker`/`UnregisterBlocker`와 대칭되는 엣지 트리거)
  - `void AssignHomeIdleZoneSlot(AIdleWaitingZone* Zone, int32 SlotIndex)` (7단계 후속 — `AIdleWaitingZone::AssignHomeSlots`가 레벨 시작 시 1회 호출해 `HomeIdleZone`/`HomeSlotIndex`를 채운다)
  - `bool TryHeadToIdleZone()` (7단계 신규, 7단계 후속에서 재설계 — "유휴 로봇은 항상 대기실로" 규칙의 공용 구현. 검색 없이 자신의 `HomeIdleZone`/`HomeSlotIndex`로만 이동한다. 이미 이동/파킹 중(`bIsParkedInIdleZone || bIsHeadingToIdleZone`)이면 즉시 true, 홈이 배정 안 됐으면 false. 성공 시 `Zone->TryOccupyHomeSlot`으로 점유 확정 + `bIsHeadingToIdleZone = true` + `SetState(Moving)` + `RequestMoveWithFilter`로 이동 시작)
  - `bool TryHandleIdleZoneArrival()` (7단계 신규 — `OnArrivedAtDestination` 맨 앞에서 호출. `bIsHeadingToIdleZone`이 true면 false로 내리고 `SetState(Idle)` 후 true를 반환해 호출부가 이후 작업 로직을 건너뛰게 한다)
  - `void LeaveIdleZoneIfParked()` (7단계 신규 — `AcceptStationAssignment`/`AcceptTransportTask` 맨 앞에서 호출. `HomeIdleZone`이 유효하면 `Zone->ReleaseSlot(this)` 호출(홈 배정 자체는 유지, "지금 앉아있음"만 해제), `bIsHeadingToIdleZone`도 함께 내림)

### `AFactoryAIController` (ADetourCrowdAIController)
- 멤버: `TSubclassOf<UNavigationQueryFilter> QueryFilterClass`
- 함수
  - `virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override` (6단계 신규 — `Result.IsSuccess()`일 때 `Cast<AFactoryAgentBase>(GetPawn())->OnArrivedAtDestination()` 호출)
  - `void RequestMoveWithFilter(const FVector& Destination)` (이동 요청 직전 `ApplyDynamicCongestionCost`를 통해 `QueryFilterClass` 인스턴스의 AreaCost를 현재 혼잡도 기준으로 갱신한 뒤 이동 요청)
  - `void ApplyDynamicCongestionCost(UNavigationQueryFilter* Filter)` (경로 주변 `ACostZoneVolume`들의 `GetCurrentCostMultiplier()`를 조회해 Filter의 AreaCost에 반영. NavMesh 지오메트리는 건드리지 않음 — `08_Navigation.md` 참고)
  - `void SetAvoidanceIgnoreActor(AActor* TargetActor, bool bIgnore)` (Crowd Avoidance 컴포넌트에서 특정 액터를 무시/재고려 대상으로 토글. NPC가 `AssignedMaintenanceTarget`에 접근할 때 사용. 버그 수정 — 기존엔 `My->GroupsToAvoid`와 `Target->AvoidanceGroup`만 바꿔 My만 Target을 무시하게 됐고 Target은 계속 My를 피하려 했다. 이제 My/Target 양쪽 모두 `GroupsToAvoid`/`AvoidanceGroup` 두 필드를 동일하게 토글해 진짜 상호 무시가 되도록 수정)

### `FPendingSlotReservation` (USTRUCT)
아틀라스가 `TransferItem` 도중 고장났을 때 수리 완료 후 재개(Resume)에 필요한 슬롯 정보를 보존한다. `bIsValid == false`이면 현재 진행 중인 예약이 없음을 의미한다.
- `bool bIsValid = false`
- `int32 FloorIndex = -1`
- `int32 SlotIndex = -1`

### `AFactoryAtlasRobot` (AFactoryAgentBase)
- 멤버
  - `FStationAssignment CurrentAssignment` (거점 배정, 비어있으면 유휴 상태)
  - `ALogisticsItem* HeldItem` (동시에 1개만 보유; 로봇 스켈레탈 메시 전용 소켓에 `AttachToComponent`로 부착. 물리 홀딩 미사용. 버그 수정 — 부착 직전 `SetActorEnableCollision(false)` 호출 추가. `ALogisticsItemSpawner::TryAcquireItem`이 풀에서 꺼내며 콜리전을 켜두는데, 그 상태로 그대로 부착하면 아틀라스 콜리전과 겹쳐 물리 디페네트레이션으로 아틀라스가 멀리 튕겨나가는 문제가 있었다)
  - `int32 OperationCount`, `int32 MaintenanceThreshold`
  - `float BreakdownChanceBase`, `float BreakdownChanceOverageMultiplier`
  - `float MaxBreakdownChanceCap = 0.40f` (고장 확률 상한, Balance 노출. 버그 수정 — 원래 `static constexpr`로 하드코딩돼 있어 재컴파일 없이 조정 불가했음. `OperationCount >= MaintenanceThreshold` 이후 `OverageOperationsPerStep`(Balance 노출, 기본 5)회 단위로 확률이 누적되지만 이 값을 초과하지 않음)
  - `FPendingSlotReservation PendingSlotReservation` (`PopNextReservedSlot`이 `CurrentAssignment.ReservedSlots` 큐에서 채우고, 그 슬롯의 leg가 완전히 끝날 때(로컬 상호작용 + 로봇 핸드오프 양쪽 성공)만 `TransferItem`이 초기화; 고장 후 재개 시 `ConfirmInbound`/`ConfirmOutboundRemoved` 호출 지점 복원에도 사용. `FloorIndex`/`SlotIndex`(Shelf 전용) 외에 `FGuid TripTaskID`도 함께 들고 있다 — 아래 `FindWaitingTransportRobot` 참고)
  - `FGuid PendingHandoffAssignmentID` (6단계 신규 — `HandoffStationAssignment`가 이동 요청 전 채워 넣는, 아직 도착하지 않은 핸드오프 배정 ID. `OnArrivedAtDestination`에서 이 값이 유효하면 일반 작업 로직 대신 `OnHandoffAtlasArrivedAtStagingPoint` 호출을 우선한다)
  - `URepairProgressComponent* RepairComponent`
- 함수
  - `bool IsMaintenanceDue() const` (`OperationCount >= MaintenanceThreshold`를 반환; 별도 bool 멤버 없음)
  - `bool IsEligibleForQuickCheck() const` (`CurrentState == Idle && bIsParkedInIdleZone && IsMaintenanceDue()`)
  - `void AcceptStationAssignment(const FStationAssignment& Assignment, bool bIsHandoff = false)` (`UOutboundDispatchSubsystem`이 호출. 맨 앞에서 `LeaveIdleZoneIfParked()`(7단계 신규 — 파킹 중이던 로봇이 새 작업을 받으면 대기실 자리를 반납) 호출 후, 신규 배정 시 `StartCurrentAssignment()`로 존 예약+첫 이동을 킥오프하고 `FTaskLifecycleEvent(Assigned)` 발행; 핸드오프 인수(`bIsHandoff=true`) 시에는 이미 스테이징 지점에 도착해 있는 상태라 킥오프 없이 `CurrentAssignment`만 갱신)
  - `void OnArrivedAtDestination()` (override — 맨 앞에서 `TryHandleIdleZoneArrival()`(7단계 신규)을 확인해 대기실 도착이면 즉시 반환, 아니면 `PendingHandoffAssignmentID` 확인 후 기존 배정 로직 진행)
  - `void EvaluateRotationOrContinue()` (이동/작업 재시도 진입점(`OnArrivedAtDestination`, `OnWorkingTick`)에서 호출. ① 고장 확률 롤 — 실패 시 `SetState(Broken)`. ② 통과 시 `IsMaintenanceDue()` 확인 — true이고 대기실에 초기화 로봇이 있으면 `UOutboundDispatchSubsystem::HandoffStationAssignment` 요청 후 대기실로 이동; 교대 불가 시 계속 진행)
  - `bool TransferItem(AActor* Source, AActor* Destination)` (선반 슬롯/트레이/배송로봇 사이의 단일 전달 동작. `Source`/`Destination`이 `AFactoryTransportRobot*`이면 소켓 대 소켓으로 직접 주고받고 로봇 쪽 `OnItemCollectedByAtlas`/`OnItemGivenByAtlas`를 호출한다. 선반 분기는 `PendingSlotReservation`이 이미 유효하다고 가정하고 재예약하지 않으며, 한 leg의 중간 단계(예: 인출 후 배송로봇에게 넘기기 전)에서는 `PendingSlotReservation`을 지우지 않는다 — leg의 마지막 단계(선반에 놓기)에서만 지운다. 버그 수정 — `bIsReachingForItem`을 `false`로 되돌리는 시점을 즉시가 아니라 `IKReachHoldSeconds`(기본 0.5초) 뒤 `ClearIKReachFlag`가 타이머로 처리하도록 변경 — 한 함수 호출 안에서 true/false가 동기적으로 토글되면 매 프레임 한 번만 값을 읽는 ABP가 true를 절대 관측 못해 IK 리치 애니메이션이 실제 게임플레이에서 전혀 트리거되지 않는 문제가 있었다. 버그 수정 — 반환형을 `bool`로 바꾸고, `RemainingCount` 감소를 호출부의 별도 문장이 아니라 Destination 분기(선반/트레이/배송로봇에 실제로 넘겨준 시점)에서 직접 처리하도록 이동 — 호출부가 성공 여부를 확인 안 하고 무조건 감소시키던 방식은 슬롯 재예약 실패 등으로 실제 전달이 안 됐는데도 카운트가 줄어 물건이 손에 남은 채 배정이 거짓 완료되는 문제가 있었다)
  - `bool PopNextReservedSlot()` (버그 수정 — 기존 `ReserveNextSlot`을 대체. 슬롯은 더 이상 아틀라스가 그때그때 정하지 않고 작업 생성 시점(`UOutboundDispatchSubsystem::DecomposeOrder`/`EnqueueInboundWork`)에 이미 예약돼 있다. `CurrentAssignment.ReservedSlots` 큐 맨 앞(`FReservedSlotEntry`)을 꺼내 `PendingSlotReservation`에 `FloorIndex`/`SlotIndex`/`TripTaskID`를 기록할 뿐, 선반에 새로 예약을 걸지 않는다. Shelf뿐 아니라 Tray 배정도 트립마다 이 큐를 채워 `TripTaskID`만 소비한다 — `SlotCoord`는 (-1,-1) 고정)
  - `void StartCurrentAssignment()` (6단계 신규, private — 신규 `CurrentAssignment`에 대해 존 예약 + `PopNextReservedSlot()`으로 첫 트립을 꺼내 그 위치로 첫 이동 요청. Shelf 계열은 Inbound/Outbound 모두 동일하게 슬롯 위치로 직접 이동하며 별도 스테이징 단계가 없다. Tray도 물리적 이동 목적지는 하나뿐이지만 `TripTaskID` 확보를 위해 동일하게 `PopNextReservedSlot()`을 호출한다)
  - `void ContinueShelfAssignment()` / `void ContinueTrayAssignment()` (6단계 신규, private — `OnArrivedAtDestination`/`OnWorkingTick`이 호출. emit/receive 통일 처리는 `07_TaskAssignment.md` 참고)
  - `AFactoryTransportRobot* FindWaitingTransportRobot(const FGuid& TripTaskID, bool bNeedsPayload) const` (6단계 신규, private. 버그 수정 — 원래 목적지 좌표 반경(`RendezvousSearchRadius`) 안에서 짐 보유 여부만 일치하면 아무 배송로봇이나 매칭했는데, 반경 튜닝값에 따라 실제로 도착한 로봇과도 어긋나는 문제가 반복됐다(300→100 조정 후에도 121 거리로 재발). 거리 추정을 완전히 폐기하고, 월드의 배송로봇 중 `CurrentTask.TaskID == TripTaskID`인 로봇 하나를 정확히 찾아 `CurrentState == Working`(도착 완료) + 짐 보유 상태 일치까지 확인한 뒤에만 반환한다. `RendezvousSearchRadius` 프로퍼티는 제거됨)
  - `void OnAssignmentExhausted()` (RemainingCount==0, 거점 이탈 후 유휴 상태 전환 → `OnTaskCompleted()` 호출 → `UOutboundDispatchSubsystem::OnStationAssignmentCompleted` 경유로 `FTaskLifecycleEvent(Completed)` 발행 → 끝에서 `Dispatch->TryDispatchIdleAgents()`(월드 전체 유휴 로봇 스윕)를 호출해 자기 자신을 포함해 다음 배정을 잇거나, 줄 배정이 없으면 대기실로 향하게 한다(7단계 신규, `TryDispatchIdleAgents` 참고))
  - `void OnTaskCompleted()` (`OperationCount` 증가; 누적 고장 확률은 `OperationCount >= MaintenanceThreshold` 이후 `BreakdownChanceBase + (초과 5회 단위 × BreakdownChanceOverageMultiplier)`로 계산하되 `MaxBreakdownChanceCap`으로 상한 고정. 임계치 초과가 지속되면 `Code:003` 발행 시 현재 확률값을 `FAnomalyEvent::RiskValue`에 기록)

### `AFactoryTransportRobot` (AFactoryAgentBase)
- 멤버
  - `FTransportTask CurrentTask` (트립 작업, 비어있으면 유휴 상태)
  - `ALogisticsItem* PayloadItem` (항상 최대 1개; 로봇 전용 소켓에 `AttachToComponent`로 부착. 버그 수정 — `AFactoryAtlasRobot::HeldItem`과 동일하게 부착 직전 콜리전을 꺼서 물리 디페네트레이션 튕김을 방지)
  - `int32 OperationCount`, `int32 MaintenanceThreshold`
  - `float BreakdownChanceBase`, `float BreakdownChanceOverageMultiplier`
  - `float MaxBreakdownChanceCap = 0.40f`, `int32 OverageOperationsPerStep = 5` (둘 다 Balance 노출. 버그 수정 — 원래 `MaxBreakdownChanceCap`은 `static constexpr`, 초과 단위 5는 매직넘버로 하드코딩돼 있어 재컴파일 없이 조정 불가했음)
  - `URepairProgressComponent* RepairComponent`
- 함수
  - `bool IsMaintenanceDue() const` (`OperationCount >= MaintenanceThreshold`)
  - `bool IsEligibleForQuickCheck() const`
  - `void AcceptTransportTask(const FTransportTask& Task)` (`UOutboundDispatchSubsystem`이 호출. 맨 앞에서 `LeaveIdleZoneIfParked()`(7단계 신규) 호출 후 `CurrentTask` 저장과 동시에 `PickupPoint` 방향(트레이면 `GetTransportRobotWorkLocation()`, 선반이면 `OutboundStagingTransform`)으로 이동 시작; `FTaskLifecycleEvent(Assigned)` 발행)
  - `void OnArrivedAtDestination()` (6단계 신규, override — 맨 앞에서 `TryHandleIdleZoneArrival()`(7단계 신규)을 확인해 대기실 도착이면 즉시 반환. 아니면 트레이/선반을 직접 건드리지 않고 `SetState(Working)`으로 파킹만 하고 `EvaluateRotationOrContinue()` 호출. 다음 단계는 아틀라스의 `OnItemGivenByAtlas`/`OnItemCollectedByAtlas` 호출을 기다린다)
  - `void OnItemGivenByAtlas(ALogisticsItem* Item)` (6단계 신규 — 아틀라스의 `TransferItem(Destination=this)`가 호출. `PayloadItem` 소켓 부착, `FTaskLifecycleEvent(PickedUp)` 발행, `DropoffPoint` 방향(선반이면 `InboundStagingTransform`)으로 이동 재개. 기존 `OnItemPickedUp()`을 대체)
  - `void OnItemCollectedByAtlas()` (6단계 신규 — 아틀라스의 `TransferItem(Source=this)`가 호출. `PayloadItem` 비우고 `OnTaskCompleted()` 호출)
  - `FVector GetTaskPointLocation(AActor* PointActor, bool bIsPickupSide) const` (6단계 신규, private — Pickup/Dropoff 지점을 트레이/선반 타입과 방향에 맞는 실제 좌표로 변환)
  - `void EvaluateRotationOrContinue()` (`OnArrivedAtDestination`에서 호출. ① 고장 확률 롤 — 실패 시 `SetState(Broken)`. ② 통과 시 `IsMaintenanceDue()` 확인 — true이고 `PayloadItem`이 비어있으며 `HasRestedTransportRobotAvailable()`(대기실에 교대 가능한 초기화 로봇이 있는가)이 true면 `TryHeadToIdleZone()`으로 자신의 홈 슬롯으로 이동(7단계 후속 — 대기실 검색이 아니라 일반 유휴 파킹과 동일한 홈 슬롯 경로 재사용); 교대 불가 시 계속 진행)
  - `bool HasRestedTransportRobotAvailable() const` (7단계 후속, private — `EvaluateRotationOrContinue`가 자리를 비우기 전 확인하는 "교대 가능한 로봇이 있는가" 체크. `AllowedAgentType == TransportRobot`인 `AIdleWaitingZone` 중 `FindRestedOccupant()`가 있는 곳이 하나라도 있으면 true)
  - `void OnEnterBlockedState()` / `virtual void OnBlockedTick(float DeltaTime) override` / `virtual void OnUnblocked() override` (6단계 신규 — `OnBlockedTick` 진입 엣지 1회에서 `OnEnterBlockedState()`가 근접 `ACostZoneVolume`들에 `RegisterBlocker` 등록 + 목록 기억, `OnUnblocked()`에서 같은 목록에 `UnregisterBlocker` 호출 후 초기화)
  - `void OnTaskCompleted()` (`SetState(Idle)`; `OperationCount` 증가; `MaxBreakdownChanceCap` 적용; `UOutboundDispatchSubsystem::OnTransportTaskCompleted` 경유로 `FTaskLifecycleEvent(Completed)` 발행; 유휴 전환 즉시 `Dispatch->TryAssignIdleTransportRobot(this, ...)`로 다음 트립을 스스로 이어받되, 이어받을 트립이 없으면 `TryHeadToIdleZone()`으로 대기실로 향한다(7단계 신규))

### `AFactoryNPCHuman` (AFactoryAgentBase)
- 멤버: `EPatrolState PatrolState`, `float PatrolStartTime`, `float MaxPatrolDurationSeconds`, `AFactoryAgentBase* AssignedMaintenanceTarget`
- 함수
  - `void StartPatrol()`
  - `void AssignMaintenance(AFactoryAgentBase* Target, ERepairType RepairType)` (`AssignedMaintenanceTarget` 설정과 동시에 `AFactoryAIController::SetAvoidanceIgnoreActor(Target, true)` 호출로 좁은 구간에서도 접근 가능하도록 회피 예외 등록; 정비 종료 시 `false`로 해제)
  - `void ReturnToOfficeRoom()`
  - `bool TryPossessByPlayer(APlayerController* Controller)` (FieldWorker Role 검증은 호출 측 `Server_RequestPossessNPC`에서 수행)
  - `void CallToOfficeExit()`
