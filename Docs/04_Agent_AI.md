# 4. 에이전트 / AI 제어 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §4. `AFactoryAgentBase`/`AFactoryAIController`는 구현 2단계(베이스 스켈레톤), `AFactoryAtlasRobot`/`AFactoryTransportRobot`/`AFactoryNPCHuman`의 실제 로직은 5단계(로봇 개별 동작)에서 채운다.

### `AFactoryAgentBase` (ACharacter)
- 멤버: `FGuid AgentID`, `EAgentType AgentType`, `EAgentState CurrentState`, `FVector TargetLocation`, `float BlockedTimer`, `static constexpr float BlockedThresholdSeconds = 2.f`, `bool bIsParkedInIdleZone`(`AIdleWaitingZone::TryOccupyHomeSlot`/`ReleaseSlot`이 갱신, "지금 앉아있음"), `TWeakObjectPtr<AIdleWaitingZone> HomeIdleZone` + `int32 HomeSlotIndex`(7단계 후속 — 선입선출 없이 레벨 시작 시 1회만 고정 배정되는 홈 대기실/슬롯. `UOutboundDispatchSubsystem::AssignHomeIdleZoneSlots`가 채운 뒤로는 세션 내내 유지됨), `bool bIsHeadingToIdleZone`(7단계 후속 — 대기실로 이동 중인 동안만 true, 도착하면 즉시 false. `bIsParkedInIdleZone`과 달리 "이동 중"만을 뜻해 `OnArrivedAtDestination`이 대기실행 도착 여부를 판별하는 데 쓰인다. 위 세 값 모두 `CurrentAssignment`와 동일하게 리플리케이트하지 않음)
- 함수
  - `virtual void SetState(EAgentState NewState)`
  - `FStateSnapshot ToSnapshot() const`
  - `virtual void OnArrivedAtDestination()` (6단계 신규 — `AFactoryAIController::OnMoveCompleted`가 이동 성공 시 호출. 기본 구현은 빈 함수, 아틀라스/배송로봇이 override)
  - `virtual void OnWorkingTick(float DeltaTime)` (6단계 신규 — `CurrentState==Working`인 동안 매 Tick 호출. 기본 구현은 빈 함수)
  - `virtual void Tick(float DeltaTime) override` (6단계 신규, 회피 재설계로 갱신 — 서버 권한에서만 동작. `CurrentState==Working`이면 `OnWorkingTick` 호출. `CurrentState`가 `Moving`/`Pause`(대기 중도 "이동 의도"는 유지)이고 속도가 거의 0이면 `BlockedTimer` 누적 — `BlockedThresholdSeconds` 초과 후 `BlockedRecoveryRetryIntervalSeconds`(Balance)마다 원인을 가리지 않고 `AbandonWaypointRouteAndReroute()`로 강제 재탐색(선반 같은 정적 지오메트리에 막혀 안전거리 트레이스가 못 잡는 경우의 최후의 안전망 — `08_Navigation.md` §8-B "최후의 안전망" 참고). `Waitbound`/다음 홉 재시도 대기 중에는 개입하지 않는다)
  - `void AssignHomeIdleZoneSlot(AIdleWaitingZone* Zone, int32 SlotIndex)` (7단계 후속 — `AIdleWaitingZone::AssignHomeSlots`가 레벨 시작 시 1회 호출해 `HomeIdleZone`/`HomeSlotIndex`를 채운다)
  - `bool TryHeadToIdleZone()` (7단계 신규, 7단계 후속에서 재설계 — "유휴 로봇은 항상 대기실로" 규칙의 공용 구현. 검색 없이 자신의 `HomeIdleZone`/`HomeSlotIndex`로만 이동한다. 이미 이동/파킹 중(`bIsParkedInIdleZone || bIsHeadingToIdleZone`)이면 즉시 true, 홈이 배정 안 됐으면 false. 성공 시 `Zone->TryOccupyHomeSlot`으로 점유 확정 + `bIsHeadingToIdleZone = true` + `SetState(Moving)` + `RequestMoveWithFilter`로 이동 시작)
  - `bool TryHandleIdleZoneArrival()` (7단계 신규 — `OnArrivedAtDestination` 맨 앞에서 호출. `bIsHeadingToIdleZone`이 true면 false로 내리고 `SetState(Idle)` 후 true를 반환해 호출부가 이후 작업 로직을 건너뛰게 한다)
  - `void LeaveIdleZoneIfParked()` (7단계 신규 — `AcceptStationAssignment`/`AcceptTransportTask` 맨 앞에서 호출. `HomeIdleZone`이 유효하면 `Zone->ReleaseSlot(this)` 호출(홈 배정 자체는 유지, "지금 앉아있음"만 해제), `bIsHeadingToIdleZone`도 함께 내림)
  - `bool TryRequestWaypointRoute(AFactoryNavWaypoint* TargetWaypoint, const FVector& FinalHopTarget)` / `bool TryHandleWaypointRouteArrival()` / `void AbandonWaypointRouteAndReroute()` (Docs 이탈, 승인됨 — 웨이포인트 그래프 경유 이동 상태머신. 상세는 `08_Navigation.md` §8-B 참고. 관련 멤버: `TArray<TWeakObjectPtr<AFactoryNavWaypoint>> PendingWaypointRoute`, `int32 WaypointRouteIndex`, `FVector PendingFinalHopTarget`, `EWaypointTravelPhase TravelPhase`(None/TraversingGraph/FinalHop, 순수 C++ enum) — 전부 리플리케이트 안 함)
  - `void RunSafetyTraceCheck()` (private — `SafetyTraceIntervalSeconds`(기본 1초, Balance|Safety) 타이머로 배치된 `USafetyTraceMarkerComponent`마다 라인트레이스(`SafeDistanceUnits`도 Balance|Safety). 마커 미배치 시 이동 방향 1방향으로 대체. 감지 시 `bYieldingForSafety`로 이동 일시정지 또는(고장 감지 시) 즉시 재탐색 — 상세는 `08_Navigation.md` §8-B)

### `AFactoryAIController` (ADetourCrowdAIController)
- 멤버: `TSubclassOf<UNavigationQueryFilter> QueryFilterClass`
- 버그 수정 — 이동 실패 시 아무 복구가 없어 에이전트가 `Moving`에 영구히 멈추는 문제가 실기 테스트로 발견됨(정지된 로봇이 뒤따르는 로봇/작업 전체를 연쇄로 막음). `MaxMoveRetryAttempts`(기본 3), `MoveRetryDelaySeconds`(기본 1초, 둘 다 Balance 노출)로 같은 목적지에 자동 재시도한다. 단, `EPathFollowingResult::Aborted`(대개 더 최신 `RequestMoveWithFilter` 호출로 대체된 것 — 예: 정비 재배정)는 재시도 대상에서 제외한다. 재시도해봤자 그 최신 요청과 충돌만 하고, 최신 요청이 알아서 자기 결과를 다시 보고하기 때문.
- 함수
  - `virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override` (6단계 신규 — `Result.IsSuccess()`일 때 `Cast<AFactoryAgentBase>(GetPawn())->OnArrivedAtDestination()` 호출. 실패 시 위 재시도 로직 진입)
  - `void RequestMoveWithFilter(const FVector& Destination)` (목적지가 바뀌면 재시도 카운트를 리셋, 같은 목적지 재시도(`RetryLastMove`)면 유지한 뒤 `QueryFilterClass`로 이동 요청)
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
  - `int32 OperationCountPerTask = 20` (Docs에 없는 구현값 — `OnTaskCompleted` 1회당 `OperationCount` 증가량. 원래 매직넘버 1로 하드코딩돼 있어 Balance 노출으로 전환하며 20으로 조정)
  - `float BreakdownChanceBase`, `float BreakdownChanceOverageMultiplier`
  - `float MaxBreakdownChanceCap = 0.40f` (고장 확률 상한, Balance 노출. 버그 수정 — 원래 `static constexpr`로 하드코딩돼 있어 재컴파일 없이 조정 불가했음. `OperationCount >= MaintenanceThreshold` 이후 `OverageOperationsPerStep`(Balance 노출, 기본 5)회 단위로 확률이 누적되지만 이 값을 초과하지 않음)
  - `FPendingSlotReservation PendingSlotReservation` (`PopNextReservedSlot`이 `CurrentAssignment.ReservedSlots` 큐에서 채우고, 그 슬롯의 leg가 완전히 끝날 때(로컬 상호작용 + 로봇 핸드오프 양쪽 성공)만 `TransferItem`이 초기화; 고장 후 재개 시 `ConfirmInbound`/`ConfirmOutboundRemoved` 호출 지점 복원에도 사용. `FloorIndex`/`SlotIndex`(Shelf 전용) 외에 `FGuid TripTaskID`도 함께 들고 있다 — 아래 `FindWaitingTransportRobot` 참고)
  - `URepairProgressComponent* RepairComponent`
- 함수
  - `bool IsMaintenanceDue() const` (`OperationCount >= MaintenanceThreshold`를 반환; 별도 bool 멤버 없음)
  - `bool IsEligibleForQuickCheck() const` (`CurrentState == Idle && bIsParkedInIdleZone && IsMaintenanceDue()`)
  - `void AcceptStationAssignment(const FStationAssignment& Assignment, const FPendingSlotReservation* InheritedSlot = nullptr)` (`UOutboundDispatchSubsystem`이 호출. 맨 앞에서 `LeaveIdleZoneIfParked()`(7단계 신규 — 파킹 중이던 로봇이 새 작업을 받으면 대기실 자리를 반납) 호출 후, 신규 배정(`InheritedSlot == nullptr`) 시 `StartCurrentAssignment()`로 존 예약+새 슬롯 팝+첫 이동을 킥오프; 즉시 동시 교대로 인수(`InheritedSlot` 유효, `07_TaskAssignment.md`의 `HandoffStationAssignment` 참고) 시에는 `PendingSlotReservation`을 그대로 물려받아 존 재예약·새 슬롯 팝 없이 곧장 그 작업 위치로 이동. 양쪽 다 마지막에 `FTaskLifecycleEvent(Assigned)` 발행)
  - `void OnArrivedAtDestination()` (override — 맨 앞에서 `TryHandleWaypointRouteArrival()`(Docs 이탈, 승인됨 — `08_Navigation.md` §8-B)을 가장 먼저 확인해 웨이포인트 경로 중간/최종 홉 이동 중이면 즉시 반환, 그 다음 `TryHandleIdleZoneArrival()`(7단계 신규)을 확인해 대기실 도착이면 즉시 반환, 아니면 기존 배정 로직 진행 — 즉시 동시 교대로 인수한 경우도 이 경로로 자연히 이어진다)
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
  - `int32 OperationCountPerTask = 20` (Docs에 없는 구현값 — `OnTaskCompleted` 1회당 `OperationCount` 증가량. 원래 매직넘버 1로 하드코딩돼 있어 Balance 노출으로 전환하며 20으로 조정)
  - `float BreakdownChanceBase`, `float BreakdownChanceOverageMultiplier`
  - `float MaxBreakdownChanceCap = 0.40f`, `int32 OverageOperationsPerStep = 5` (둘 다 Balance 노출. 버그 수정 — 원래 `MaxBreakdownChanceCap`은 `static constexpr`, 초과 단위 5는 매직넘버로 하드코딩돼 있어 재컴파일 없이 조정 불가했음)
  - `URepairProgressComponent* RepairComponent`
- 함수
  - `bool IsMaintenanceDue() const` (`OperationCount >= MaintenanceThreshold`)
  - `bool IsEligibleForQuickCheck() const`
  - `void AcceptTransportTask(const FTransportTask& Task)` (`UOutboundDispatchSubsystem`이 호출. 맨 앞에서 `LeaveIdleZoneIfParked()`(7단계 신규) 호출 후 `CurrentTask` 저장과 동시에 `TryStartMoveToPoint(PickupPoint, true)` 호출; `FTaskLifecycleEvent(Assigned)` 발행)
  - `void OnArrivedAtDestination()` (6단계 신규, override — 맨 앞에서 `TryHandleWaypointRouteArrival()`(Docs 이탈, 승인됨)을 가장 먼저 확인해 웨이포인트 경로 이동 중이면 즉시 반환, 그 다음 `TryHandleIdleZoneArrival()`(7단계 신규)을 확인해 대기실 도착이면 즉시 반환. 아니면 트레이/선반을 직접 건드리지 않고 `SetState(Working)`으로 파킹만 하고 `EvaluateRotationOrContinue()` 호출. 다음 단계는 아틀라스의 `OnItemGivenByAtlas`/`OnItemCollectedByAtlas` 호출을 기다린다)
  - `void OnItemGivenByAtlas(ALogisticsItem* Item)` (6단계 신규 — 아틀라스의 `TransferItem(Destination=this)`가 호출. `PayloadItem` 소켓 부착, `FTaskLifecycleEvent(PickedUp)` 발행, `TryStartMoveToPoint(DropoffPoint, false)`로 이동 재개. 기존 `OnItemPickedUp()`을 대체)
  - `void OnItemCollectedByAtlas()` (6단계 신규 — 아틀라스의 `TransferItem(Source=this)`가 호출. `PayloadItem` 비우고 `ReleaseReservedTrayZone()`(버그 수정 신규) 호출 후 `OnTaskCompleted()` 호출)
  - `FVector GetTaskPointLocation(AActor* PointActor, bool bIsPickupSide) const` (6단계 신규, private — Pickup/Dropoff 지점을 트레이/선반 타입과 방향에 맞는 실제 좌표로 변환)
  - `void TryStartMoveToPoint(AActor* PointActor, bool bIsPickupSide)` / `void RetryMoveToPendingPoint()` / `void ReleaseReservedTrayZone()` (버그 수정 신규, private — `AcceptTransportTask`/`OnItemGivenByAtlas`가 직접 `RequestMoveWithFilter`를 호출하던 것을 대체. `PointActor`가 `AHorizontalTray`면 이동 전에 `TryReserveTransportRobotWorkZone`을 먼저 시도하고, 실패(이미 다른 배송로봇이 점유 중)하면 이동을 미루고 `TrayZoneRetryIntervalSeconds`(Balance, 기본 1초)마다 재시도한다. 원래 트레이의 배송로봇 작업 지점(`GetTransportRobotWorkLocation()`)은 좌표 하나뿐인데 예약이 없어서, 물량이 몰려 트립이 연달아 들어오면 두 번째 로봇이 첫 번째가 서있는 지점으로 이동을 시도해 길찾기가 영구 차단되는 문제가 실기 테스트로 발견됨. 버그 수정(설계 변경) — 트레이면 `TryReserveTransportRobotWorkZone` 확보 후, 사람이 미리 지정한 고정 도킹 웨이포인트 대신 목표 마커 좌표(`GetTaskPointLocation`)에 가장 가깝고 실제로 도달 가능한 웨이포인트를 `TryRequestWaypointRoute`가 매번 동적으로 찾는다(선반도 별도 도킹 개념 없이 동일하게 슬롯 좌표 기준으로 탐색) — 상세는 `08_Navigation.md` §8-B)
  - `void EvaluateRotationOrContinue()` (`OnArrivedAtDestination`에서 호출. ① 고장 확률 롤 — 실패 시 `SetState(Broken)`. ② 통과 시 `IsMaintenanceDue()` 확인 — true이고 `PayloadItem`이 비어있으며 `HasRestedTransportRobotAvailable()`(대기실에 교대 가능한 초기화 로봇이 있는가)이 true면 `TryHeadToIdleZone()`으로 자신의 홈 슬롯으로 이동(7단계 후속 — 대기실 검색이 아니라 일반 유휴 파킹과 동일한 홈 슬롯 경로 재사용); 교대 불가 시 계속 진행)
  - `bool HasRestedTransportRobotAvailable() const` (7단계 후속, private — `EvaluateRotationOrContinue`가 자리를 비우기 전 확인하는 "교대 가능한 로봇이 있는가" 체크. `IsUsableBy(TransportRobot)`인 `AIdleWaitingZone`(버그 수정(사용자 지시) — `AllowedAgentType` 단일값 비교에서 `AllowedAgentTypes` 비트마스크 조회로 전환) 중 `FindRestedOccupant()`가 있는 곳이 하나라도 있으면 true)
  - `void OnTaskCompleted()` (`SetState(Idle)`; `OperationCount` 증가; `MaxBreakdownChanceCap` 적용; `UOutboundDispatchSubsystem::OnTransportTaskCompleted` 경유로 `FTaskLifecycleEvent(Completed)` 발행; 유휴 전환 즉시 `Dispatch->TryAssignIdleTransportRobot(this, ...)`로 다음 트립을 스스로 이어받되, 이어받을 트립이 없으면 `TryHeadToIdleZone()`으로 대기실로 향한다(7단계 신규))

### `AFactoryNPCHuman` (AFactoryAgentBase)
- 멤버: `EPatrolState PatrolState`, `float PatrolStartTime`, `float MaxPatrolDurationSeconds`, `AFactoryAgentBase* AssignedMaintenanceTarget`, `int32 MaxOfficeWaitSeconds = 10`(UPROPERTY EditAnywhere, Balance|Patrol — 버그 수정(사용자 지시) 아래 `OnArrivedAtDestination` 참고)
- 함수
  - `void StartPatrol()` (7단계 후속 — 순찰 단독(애니메이션/내비메시) 테스트용으로 `UFUNCTION(BlueprintCallable)` 노출. 레벨 블루프린트에서 키 바인딩으로 직접 호출 가능. 버그 수정(사용자 지시) — 이제 `OnArrivedAtDestination`이 사무실 대기 종료 시 자동으로도 호출한다)
  - `void AssignMaintenance(AFactoryAgentBase* Target, ERepairType RepairType)` (`AssignedMaintenanceTarget` 설정과 동시에 `AFactoryAIController::SetAvoidanceIgnoreActor(Target, true)` 호출로 좁은 구간에서도 접근 가능하도록 회피 예외 등록; 정비 종료 시 `false`로 해제. 버그 수정(사용자 지시) — 사무실 랜덤 대기 타이머(`OfficeWaitTimerHandle`)가 걸려 있으면 여기서 먼저 취소한다 — 안 그러면 대기 중 새 정비가 배정돼도 나중에 타이머가 만료되며 진행 중인 정비를 무시하고 `StartPatrol()`로 튈 수 있다)
  - `void ReturnToOfficeRoom()` (7단계 후속 — `URepairProgressComponent::OnRepairCompleted()`가 정비를 마친 뒤 AI 제어 중인 NPC 정비자에게 자동 호출한다. 빙의 중인 플레이어는 대상에서 제외 — 대신 `OnJoinedRepairCompleted()`가 처리한다, 8단계)
  - `virtual void OnArrivedAtDestination() override` (버그 수정(사용자 지시) — 기본 구현(`AFactoryAgentBase::OnArrivedAtDestination`, 빈 함수)이라 사무실 복귀 도착이 전혀 감지되지 않아 `CurrentState`/`PatrolState`가 `Moving`/`ReturningToOffice`에 영구히 눌러붙었고, 자동으로 순찰을 재개시킬 방법이 없어 정비를 한 번이라도 마친 NPC가 사무실에 영구 정지 — 결국 레벨의 모든 NPC가 같은 사무실 지점에 뭉치는 현상으로 재현됐다. `PatrolState == ReturningToOffice`(사무실 복귀 도착)일 때만 처리하고, 순찰 중 도착이나 정비 대상 도착(`AssignMaintenance`가 이동 시작 시점에 이미 `UnderRepair`로 전환해둠)은 대상이 아니다. `PatrolState = InOffice`, `SetState(Idle)` 후 `FMath::RandRange(0, MaxOfficeWaitSeconds)`로 뽑은 정수 초만큼(0이면 즉시) 대기했다가 `StartPatrol()`을 다시 호출한다)
  - `bool TryPossessByPlayer(APlayerController* Controller)` (FieldWorker Role 검증은 호출 측 `Server_RequestPossessNPC`에서 수행)
  - **8단계 신규 — 빙의 중 입력(Docs 이탈, 승인됨).** `PossessedBy`/`UnPossessed`(override)가 서드퍼슨 템플릿 추가 시 생성된 `IMC_Default`(`IA_Move`/`IA_Look`/`IA_Jump`)·`IMC_MouseLook`(`IA_MouseLook`)을 빙의 시점에 추가하고 해제 시점에 제거한다(레벨 시작 `BeginPlay`가 아님 — AI 제어 중엔 아무도 조작하지 않으므로). `SetupPlayerInputComponent`가 이동/시점을 표준 서드퍼슨 패턴(컨트롤러 Yaw 기준)으로, `IA_Jump`를 `ACharacter::Jump`/`StopJumping`에 바인딩. `IA_Interact`는 `AFactorySpectatorPawn`과 같은 에셋을 공유한다.
  - `void JoinRepairAsPlayer(URepairProgressComponent*)` / `void LeaveRepairAsPlayer()` (8단계 신규 — `AssignMaintenance`의 플레이어 대응. `IA_Interact` 트리거 시 시야 정면 트레이스(`FindRepairableInFrontOfCamera`, `AFactorySpectatorPawn::FindInteractableInFrontOfCamera`와 동일 패턴)로 `Broken` + `GetRepairComponent()` 보유 대상만 후보로 삼아 참여/이탈을 토글한다(`AFactoryPlayerController::Server_JoinRepair`/`Server_LeaveRepair`가 위임). 항상 `ERepairType::FullRepair` — `QuickCheck`는 AI의 사전 정비 전용이라 대상에서 제외. 참여 여부는 `JoinedRepairComponent`(Replicated — 원격 클라이언트도 서버 권위 값을 받아야 상호작용 토글 방향을 올바르게 판단할 수 있음)로 추적)
  - `void OnJoinedRepairCompleted()` (8단계 신규 — `URepairProgressComponent::OnRepairCompleted()`가 빙의 중이던 참여자에게 호출. `JoinedRepairComponent` 정리 + `CurrentState`를 `Idle`로 복귀)
  - `void CallToOfficeExit()`
