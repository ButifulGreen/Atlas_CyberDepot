# 7. 작업 배정 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §7. 구현 6단계(배정/디스패치) 대상. `06_Infrastructure.md`, `04_Agent_AI.md`가 먼저 준비되어 있어야 한다.

출고 작업을 아틀라스용 "거점 배정"과 운송로봇용 "트립 작업"으로 이원화한다. 교대(로테이션) 시 아틀라스의 거점 배정은 `HandoffStationAssignment`를 통해 다른 아틀라스에게 중도 이전 가능하다.

## 역할 분담 (6단계 오케스트레이션 확정 사항)

**아틀라스만 트레이/선반을 직접 건드린다. 배송로봇은 트레이·선반에 절대 직접 접근하지 않고, 오직 아틀라스와의 소켓 대 소켓 전달(`AFactoryAtlasRobot::TransferItem`의 배송로봇 분기 ↔ `AFactoryTransportRobot::OnItemGivenByAtlas`/`OnItemCollectedByAtlas`)로만 물품을 주고받는다.**

- `TrayWorkZone` 배정을 받은 아틀라스는 `AHorizontalTray::GetAtlasWorkLocation()`에 서서 트레이와 직접 상호작용하고, **같은 위치에서 이동 없이** 그 옆 `GetTransportRobotWorkLocation()`에 대기 중인 배송로봇과 물품을 주고받는다. 즉 트레이 배정 1건당 이동은 1회뿐이다.
- `ShelfInboundZone`/`ShelfOutboundZone` 배정을 받은 아틀라스는 이동 2회(스테이징 지점에서 로봇과 핸드오프 ↔ 슬롯별 정확한 위치에서 IK 적재/인출)를 반복한다.
- 배송로봇은 항상 "아틀라스가 줄 때까지 대기 → 받으면 다음 목적지로 이동 → 도착하면 대기"만 반복하는 수동적 셔틀이다.

이 결과 물품 하나의 완전한 이동은 다음과 같이 구성된다:

- **Inbound(입고)**: 트레이에 물품 스폰 → 아틀라스A(`TrayWorkZone`, Inbound 트레이)가 트레이에서 집어 대기 중인 배송로봇에게 전달 → 배송로봇이 선반까지 장거리 이동 → 아틀라스B(`ShelfInboundZone`)가 배송로봇에게서 받아 정확한 슬롯에 IK로 적재.
- **Outbound(출고)**: 아틀라스B(`ShelfOutboundZone`)가 슬롯에서 IK로 인출해 선반 스테이징에서 대기 중인 배송로봇에게 전달 → 배송로봇이 트레이까지 장거리 이동 → 아틀라스A(`TrayWorkZone`, Outbound 트레이)가 배송로봇에게서 받아 트레이에 적재.

아틀라스가 1대뿐인 경우 한 로봇이 두 배정(`TrayWorkZone`, `ShelfZone`)을 유휴 상태가 될 때마다 번갈아 받는다 — 별도 처리 불필요.

### 아틀라스 상태머신 (`AFactoryAtlasRobot`)

- `AcceptStationAssignment`가 `StartCurrentAssignment()`를 호출해 존 예약(`TryReserveWorkZone`/`TryReserveInboundZone`/`TryReserveOutboundZone`) + 첫 이동을 킥오프한다. `ShelfOutboundZone`은 첫 이동 전에 `ReserveNextSlot()`으로 슬롯을 먼저 예약(목적지 계산에 필요), `ShelfInboundZone`은 스테이징으로 먼저 이동(받은 뒤에 슬롯 예약).
- `AFactoryAIController::OnMoveCompleted` → `AFactoryAgentBase::OnArrivedAtDestination()`(virtual) → 아틀라스는 배정 대상이 `AHorizontalTray`인지 `AStorageShelf`인지에 따라 `ContinueTrayAssignment`/`ContinueShelfAssignment`를 호출한다.
- 두 `Continue*` 함수는 "emit(내보냄)"과 "receive(받음)"으로 통일해서 처리한다 — 판정은 `AHorizontalTray::Direction`(Tray) 또는 `ZoneType`(Shelf) 기준. `HeldItem` 보유 여부가 곧 "지금 어느 다리(leg)에 있는가"를 알려준다(Tray는 이동이 1회뿐이라 값 자체보다 순서로 판단).
- 파트너(배송로봇)가 아직 도착하지 않았으면 그 자리에서 기다린다 — `SetState(Working)` 상태에서 `AFactoryAgentBase::OnWorkingTick`이 `ZoneRetryIntervalSeconds`마다 같은 `Continue*` 함수를 재시도한다(`FindWaitingTransportRobot`이 `RendezvousSearchRadius` 안에서 짐 보유 여부가 일치하는 배송로봇을 탐색).
- 배정 소진(`RemainingCount<=0`) 시 `OnAssignmentExhausted()`가 존 반납 + `OnTaskCompleted()` + `Dispatch->OnStationAssignmentCompleted` 후, 유휴 전환 즉시 `Dispatch->TryAssignIdleAtlas(this, ...)`로 스스로 다음 배정을 이어받는다(Pull).

### 배송로봇 상태머신 (`AFactoryTransportRobot`)

- `AcceptTransportTask`가 `PickupPoint` 방향으로 즉시 이동을 시작한다(대상이 `AHorizontalTray`면 `GetTransportRobotWorkLocation()`, `AStorageShelf`면 `OutboundStagingTransform`).
- `OnArrivedAtDestination()`은 `SetState(Working)`으로 그 자리에 파킹할 뿐, 트레이/선반을 스스로 건드리지 않는다.
- `OnItemGivenByAtlas(Item)`: 아틀라스가 물품을 건네줄 때 호출 — 소켓 부착 + `DropoffPoint` 방향(대상이 Shelf면 `InboundStagingTransform`)으로 이동 재개.
- `OnItemCollectedByAtlas()`: 아틀라스가 물품을 가져갈 때 호출 — 비우고 `OnTaskCompleted()`(카운트 증가, 이벤트 발행, `Dispatch->TryAssignIdleTransportRobot`로 다음 트립 스스로 수령).
- Blocked 판정(`AFactoryAgentBase::Tick`이 `BlockedThresholdSeconds` 경과를 감지)이 `OnBlockedTick`을 부르면 `OnEnterBlockedState()`로 `ACostZoneVolume::RegisterBlocker`를 1회 등록하고, `OnUnblocked()`에서 동일 존들에 `UnregisterBlocker`를 호출해 해제한다.

### `EWorkZoneType` (enum)
- `ShelfInboundZone`, `ShelfOutboundZone`, `TrayWorkZone`(트레이 쪽 아틀라스 배정 — 6단계에서 실제로 생성되기 시작함)

### `FStationAssignment` (USTRUCT)
- `FGuid AssignmentID`
- `FGuid SourceOrderID` (8단계 — 어느 `FDeliveryOrder`에서 파생됐는지 추적, 주문 취소 시 역추적용으로 추가. Inbound 경로로 생성되는 배정은 개별 주문 단위가 아니라 값을 채우지 않는다)
- `EWorkZoneType ZoneType`
- `TWeakObjectPtr<AActor> TargetZoneOwner` (`AStorageShelf` 또는 `AHorizontalTray`)
- `int32 RemainingCount`
- `TWeakObjectPtr<AFactoryAtlasRobot> AssignedAtlas`

### `FTransportTask` (USTRUCT)
- `FGuid TaskID`
- `FGuid SourceOrderID` (8단계 — 같은 목적. 6단계부터 `DecomposeOrder`/`EnqueueInboundWork`가 실제로 채운다)
- `TWeakObjectPtr<AActor> PickupPoint` (선반 또는 트레이)
- `TWeakObjectPtr<AActor> DropoffPoint`
- `EItemType ItemType`

### `FPendingHandoff` (USTRUCT)
소프트 핸드오프 진행 중인 교대 건을 추적한다. To가 스테이징 지점에 도착하기 전까지는 From이 계속 실제 거점을 점유한다.
- `FGuid AssignmentID`
- `TWeakObjectPtr<AFactoryAtlasRobot> From`
- `TWeakObjectPtr<AFactoryAtlasRobot> To`
- `EWorkZoneType ZoneType`

> **알려진 제한사항(6단계)**: `HandoffStationAssignment`는 `ShelfOutboundZone` 교대 시 To를 `OutboundStagingTransform`으로 보내는데, Outbound 배정의 첫 다리는 원래 슬롯에서 시작해야 해서 완전히 정합하지 않는다. 대기실(`AIdleWaitingZone`)이 이번 6단계 스코프 밖(레벨 미배치)이라 실사용되지 않아 당장 영향은 없다 — 대기실/교대를 실제로 테스트하는 단계에서 재검토.

### `UOutboundDispatchSubsystem` (UWorldSubsystem)
주문을 거점 배정과 트립 작업으로 분해하고, 유휴 아틀라스/운송로봇에게 배정한다. 교대 시 거점 배정 소유권 이전을 담당한다.

- 멤버
  - `TArray<FStationAssignment> ActiveStationAssignments`
  - `TArray<FTransportTask> PendingTransportTasks`
  - `TMap<FGuid, FPendingHandoff> PendingHandoffs`
- 함수
  - `void DecomposeOrder(const FDeliveryOrder& Order)` (품목별로 `ShelfOutboundZone` 배정 + 그 품목의 Outbound 트레이를 찾아 `TrayWorkZone` 배정을 함께 생성. 수량만큼 개별 `FTransportTask`(Pickup=Shelf, Dropoff=Tray)를 큐에 넣는다 — 배송로봇은 짐을 1개씩만 나르므로 트립 단위로 분리. 끝나면 `TryDispatchIdleAgents()` 호출)
  - `bool TryCancelAssignmentsForOrder(const FGuid& OrderID)` (8단계 — `SourceOrderID`가 일치하는 항목 중 하나라도 `AssignedAtlas`가 배정돼 있으면 전체 취소 거부. 전부 미배정이면 제거하고 성공 반환. `UDeliveryOrderSubsystem::TryCancelOrder`가 호출)
  - `bool TryAssignIdleAtlas(AFactoryAtlasRobot* Atlas, FStationAssignment& OutAssignment)`
  - `bool TryAssignIdleTransportRobot(AFactoryTransportRobot* Robot, FTransportTask& OutTask)` (현재 픽업 대기 물품이 있는 거점 중 선택)
  - `void HandoffStationAssignment(const FGuid& AssignmentID, AFactoryAtlasRobot* From, AFactoryAtlasRobot* To)` (**소프트 핸드오프.** 즉시 점유를 넘기지 않고 `PendingHandoffs`에 등록한 뒤 To를 `AStorageShelf`의 스테이징 트랜스폼(`InboundStagingTransform`/`OutboundStagingTransform`)으로 이동시킨다. 실제 점유 이전은 `OnHandoffAtlasArrivedAtStagingPoint`에서 처리하며, 그 전까지 From은 계속 거점을 점유한 채 작업을 이어간다)
  - `void OnHandoffAtlasArrivedAtStagingPoint(const FGuid& AssignmentID)` (To의 `AFactoryAIController::OnMoveCompleted`가 스테이징 지점 도착을 알리면 호출. `AStorageShelf::TransferZoneOccupancy` 호출로 점유를 From→To로 원자적 이전 → `FStationAssignment::AssignedAtlas`를 To로 교체 → To의 `AcceptStationAssignment` 호출 → From을 대기실로 이동 → `PendingHandoffs`에서 제거)
  - `void OnStationAssignmentCompleted(const FGuid& AssignmentID)` (RemainingCount==0, 아틀라스 재배치 가능 상태로 전환; `FTaskLifecycleEvent(Completed)` 발행)
  - `void OnTransportTaskCompleted(const FGuid& TaskID)` (`FTaskLifecycleEvent(Completed)` 발행)
  - `void TryDispatchIdleAgents()` (6단계 신규 — 월드의 Idle 상태 아틀라스/배송로봇 전체를 훑어 `TryAssignIdleAtlas`/`TryAssignIdleTransportRobot`을 시도하는 Push 경로. `DecomposeOrder`/`EnqueueInboundWork`가 새 작업 생성 직후 호출해, 이미 대기 중이던 유휴 로봇이 놓치지 않고 새 작업을 받게 한다)
  - `void EnqueueInboundWork(EItemType ItemType, AHorizontalTray* Tray, AStorageShelf* Shelf)` (6단계 신규 — `UInventoryOrderSubsystem::TryPlaceOrder`가 Inbound 트레이에 물품을 올린 직후 호출. `TrayWorkZone`(Tray) + `ShelfInboundZone`(Shelf) 배정과 이를 잇는 `FTransportTask`(Pickup=Tray, Dropoff=Shelf) 1건을 생성하고 `TryDispatchIdleAgents()` 호출)
