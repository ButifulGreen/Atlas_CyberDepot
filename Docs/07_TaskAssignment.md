# 7. 작업 배정 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §7. 구현 6단계(배정/디스패치) 대상. `06_Infrastructure.md`, `04_Agent_AI.md`가 먼저 준비되어 있어야 한다.

출고 작업을 아틀라스용 "거점 배정"과 운송로봇용 "트립 작업"으로 이원화한다. 교대(로테이션) 시 아틀라스의 거점 배정은 `HandoffStationAssignment`를 통해 다른 아틀라스에게 중도 이전 가능하다.

### `EWorkZoneType` (enum)
- `ShelfInboundZone`, `ShelfOutboundZone`, `TrayWorkZone`

### `FStationAssignment` (USTRUCT)
- `FGuid AssignmentID`
- `FGuid SourceOrderID` (8단계 — 어느 `FDeliveryOrder`에서 파생됐는지 추적, 주문 취소 시 역추적용으로 추가)
- `EWorkZoneType ZoneType`
- `TWeakObjectPtr<AActor> TargetZoneOwner` (`AStorageShelf` 또는 `AHorizontalTray`)
- `int32 RemainingCount`
- `TWeakObjectPtr<AFactoryAtlasRobot> AssignedAtlas`

### `FTransportTask` (USTRUCT)
- `FGuid TaskID`
- `FGuid SourceOrderID` (8단계 — 같은 목적으로 추가. `DecomposeOrder`가 아직 `PendingTransportTasks`를 채우지 않아 실제 값이 채워지지 않는 상태이며, `Docs/14_OpenIssues.md`에 미결로 남겨둔다)
- `TWeakObjectPtr<AActor> PickupPoint` (선반 또는 트레이)
- `TWeakObjectPtr<AActor> DropoffPoint`
- `EItemType ItemType`

### `FPendingHandoff` (USTRUCT)
소프트 핸드오프 진행 중인 교대 건을 추적한다. To가 스테이징 지점에 도착하기 전까지는 From이 계속 실제 거점을 점유한다.
- `FGuid AssignmentID`
- `TWeakObjectPtr<AFactoryAtlasRobot> From`
- `TWeakObjectPtr<AFactoryAtlasRobot> To`
- `EWorkZoneType ZoneType`

### `UOutboundDispatchSubsystem` (UWorldSubsystem)
주문을 거점 배정과 트립 작업으로 분해하고, 유휴 아틀라스/운송로봇에게 배정한다. 교대 시 거점 배정 소유권 이전을 담당한다.

- 멤버
  - `TArray<FStationAssignment> ActiveStationAssignments`
  - `TArray<FTransportTask> PendingTransportTasks`
  - `TMap<FGuid, FPendingHandoff> PendingHandoffs`
- 함수
  - `void DecomposeOrder(const FDeliveryOrder& Order)` (선반별 필요 수량만큼 `FStationAssignment` 생성, 입고/출고 구역 동시 운용 가능. 생성되는 각 `FStationAssignment.SourceOrderID`는 `Order.OrderID`로 채워진다)
  - `bool TryCancelAssignmentsForOrder(const FGuid& OrderID)` (8단계 — `SourceOrderID`가 일치하는 항목 중 하나라도 `AssignedAtlas`가 배정돼 있으면 전체 취소 거부. 전부 미배정이면 제거하고 성공 반환. `UDeliveryOrderSubsystem::TryCancelOrder`가 호출)
  - `bool TryAssignIdleAtlas(AFactoryAtlasRobot* Atlas, FStationAssignment& OutAssignment)`
  - `bool TryAssignIdleTransportRobot(AFactoryTransportRobot* Robot, FTransportTask& OutTask)` (현재 픽업 대기 물품이 있는 거점 중 선택)
  - `void HandoffStationAssignment(const FGuid& AssignmentID, AFactoryAtlasRobot* From, AFactoryAtlasRobot* To)` (**소프트 핸드오프.** 즉시 점유를 넘기지 않고 `PendingHandoffs`에 등록한 뒤 To를 `AStorageShelf`의 스테이징 트랜스폼(`InboundStagingTransform`/`OutboundStagingTransform`)으로 이동시킨다. 실제 점유 이전은 `OnHandoffAtlasArrivedAtStagingPoint`에서 처리하며, 그 전까지 From은 계속 거점을 점유한 채 작업을 이어간다)
  - `void OnHandoffAtlasArrivedAtStagingPoint(const FGuid& AssignmentID)` (To의 `AFactoryAIController::OnMoveCompleted`가 스테이징 지점 도착을 알리면 호출. `AStorageShelf::TransferZoneOccupancy` 호출로 점유를 From→To로 원자적 이전 → `FStationAssignment::AssignedAtlas`를 To로 교체 → To의 `AcceptStationAssignment` 호출 → From을 대기실로 이동 → `PendingHandoffs`에서 제거)
  - `void OnStationAssignmentCompleted(const FGuid& AssignmentID)` (RemainingCount==0, 아틀라스 재배치 가능 상태로 전환; `FTaskLifecycleEvent(Completed)` 발행)
  - `void OnTransportTaskCompleted(const FGuid& TaskID)` (`FTaskLifecycleEvent(Completed)` 발행)
