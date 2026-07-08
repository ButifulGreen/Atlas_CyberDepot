# 7. 작업 배정 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §7. 구현 6단계(배정/디스패치) 대상. `06_Infrastructure.md`, `04_Agent_AI.md`가 먼저 준비되어 있어야 한다.

출고 작업을 아틀라스용 "거점 배정"과 운송로봇용 "트립 작업"으로 이원화한다. 교대(로테이션) 시 아틀라스의 거점 배정은 `HandoffStationAssignment`를 통해 다른 아틀라스에게 중도 이전 가능하다.

## 역할 분담 (6단계 오케스트레이션 확정 사항)

**아틀라스만 트레이/선반을 직접 건드린다. 배송로봇은 트레이·선반에 절대 직접 접근하지 않고, 오직 아틀라스와의 소켓 대 소켓 전달(`AFactoryAtlasRobot::TransferItem`의 배송로봇 분기 ↔ `AFactoryTransportRobot::OnItemGivenByAtlas`/`OnItemCollectedByAtlas`)로만 물품을 주고받는다.**

- `TrayWorkZone` 배정을 받은 아틀라스는 `AHorizontalTray::GetAtlasWorkLocation()`에 서서 트레이와 직접 상호작용하고, **같은 위치에서 이동 없이** 그 옆 `GetTransportRobotWorkLocation()`에 대기 중인 배송로봇과 물품을 주고받는다. 즉 트레이 배정 1건당 이동은 1회뿐이다.
- `ShelfInboundZone`/`ShelfOutboundZone` 배정을 받은 아틀라스도 **동일하게 이동 1회뿐이다.** 별도의 "스테이징 지점"을 거치지 않고, 물품이 들어갈(또는 나올) **바로 그 슬롯**의 (X, Y) 위치로 아틀라스와 배송로봇이 직접 이동해서 거기서 만난다 — 층(Z)은 이동 목적지 계산에서 무시되고(`AStorageShelf::ComputeWorkLocation`이 항상 선반 자신의 지상 Z로 고정) IK(`CurrentIKHandTarget`)만 실제 층 높이까지 뻗는다. 이렇게 해야 배송로봇이 층별로 다른 높이까지 갈 필요 없이(원래 지상 이동체라 애초에 못 감) 항상 같은 지상 높이에서, 슬롯별로만 다른 (X,Y) 위치에서 아틀라스를 만날 수 있다.
- 배송로봇은 항상 "아틀라스가 줄 때까지 대기 → 받으면 다음 목적지로 이동 → 도착하면 대기"만 반복하는 수동적 셔틀이다.
- **어느 슬롯을 쓸지는 아틀라스가 그때그때 정하지 않고, 작업이 생성되는 시점(`DecomposeOrder`/`EnqueueInboundWork`)에 미리 예약**되어 아틀라스 배정(`FStationAssignment::ReservedSlots`)과 배송로봇의 개별 트립(`FTransportTask::FloorIndex`/`SlotIndex`) 양쪽에 동시에 실린다 — 그래야 서로 다른 두 로봇이 같은 슬롯 위치로 어긋남 없이 수렴할 수 있다.
- **버그 수정 — 아틀라스가 파트너 배송로봇을 찾을 때도 더 이상 거리로 추정하지 않는다.** 원래 목적지 좌표 반경(`RendezvousSearchRadius`) 안에서 짐 보유 여부만 일치하면 아무 배송로봇이나 매칭했는데, 반경 튜닝값(300→100)을 조정해도 실제 도착 로봇과 계속 어긋나는 문제가 재발했다. 이제 같은 트립을 생성할 때 Shelf/Tray 두 배정에 같은 `FGuid TripTaskID`(=`FTransportTask::TaskID`)를 함께 실어(`FReservedSlotEntry`), `FindWaitingTransportRobot`이 `CurrentTask.TaskID`가 일치하는 그 로봇 하나만 정확히 찾는다.

이 결과 물품 하나의 완전한 이동은 다음과 같이 구성된다:

- **Inbound(입고)**: 트레이에 물품 스폰 → 아틀라스A(`TrayWorkZone`, Inbound 트레이)가 트레이에서 집어 대기 중인 배송로봇에게 전달 → 배송로봇이 선반의 예약된 슬롯 위치까지 장거리 이동 → 아틀라스B(`ShelfInboundZone`)가 같은 슬롯 위치에서 배송로봇에게서 받아 IK로 적재.
- **Outbound(출고)**: 아틀라스B(`ShelfOutboundZone`)가 예약된 슬롯 위치에서 IK로 인출해 같은 위치에서 대기 중인 배송로봇에게 전달 → 배송로봇이 트레이까지 장거리 이동 → 아틀라스A(`TrayWorkZone`, Outbound 트레이)가 배송로봇에게서 받아 트레이에 적재.

아틀라스가 1대뿐인 경우 한 로봇이 두 배정(`TrayWorkZone`, `ShelfZone`)을 유휴 상태가 될 때마다 번갈아 받는다 — 별도 처리 불필요.

### 아틀라스 상태머신 (`AFactoryAtlasRobot`)

- `AcceptStationAssignment`가 `StartCurrentAssignment()`를 호출해 존 예약(`TryReserveWorkZone`/`TryReserveInboundZone`/`TryReserveOutboundZone`) + 첫 이동을 킥오프한다. Shelf 배정은 `PopNextReservedSlot()`으로 `CurrentAssignment.ReservedSlots`(작업 생성 시점에 이미 예약돼 있는 큐)에서 슬롯을 하나 꺼내 그 위치로 바로 이동한다.
- `AFactoryAIController::OnMoveCompleted` → `AFactoryAgentBase::OnArrivedAtDestination()`(virtual) → 아틀라스는 배정 대상이 `AHorizontalTray`인지 `AStorageShelf`인지에 따라 `ContinueTrayAssignment`/`ContinueShelfAssignment`를 호출한다.
- 두 `Continue*` 함수는 "emit(내보냄)"과 "receive(받음)"으로 통일해서 처리한다 — 판정은 `AHorizontalTray::Direction`(Tray) 또는 `ZoneType`(Shelf) 기준. `HeldItem` 보유 여부가 곧 "지금 어느 절반에 있는가"를 알려준다. `PendingSlotReservation`은 한 슬롯의 leg(로컬 상호작용 + 로봇 핸드오프 양쪽)가 완전히 끝날 때까지 `TransferItem`이 지우지 않고 `ContinueShelfAssignment`만 `PopNextReservedSlot()`으로 갱신한다 — 재시도 중에도 같은 슬롯 좌표를 안전하게 계속 참조하기 위함.
- 파트너(배송로봇)가 아직 도착하지 않았으면 그 자리에서 기다린다 — `SetState(Working)` 상태에서 `AFactoryAgentBase::OnWorkingTick`이 `ZoneRetryIntervalSeconds`마다 같은 `Continue*` 함수를 재시도한다. `FindWaitingTransportRobot(PendingSlotReservation.TripTaskID, bNeedsPayload)`이 `CurrentTask.TaskID`가 일치하는 배송로봇을 찾아 `CurrentState == Working`(도착 완료)과 짐 보유 상태까지 확인한 뒤에만 반환한다 — 아직 배정 전이거나 이동 중이거나 짐 상태가 안 맞으면 로그만 남기고 다음 재시도로 넘긴다.
- `ContinueTrayAssignment`의 emit(Inbound 트레이) 분기는 `AHorizontalTray::bIsHaltedAtEnd`가 true일 때만(=컨베이어가 `ItemEndMarker`까지 다 이동해서 멈춘 뒤에만) 집는다 — 아직 이동 중이면 같은 재시도 주기로 대기한다.
- 배정 소진(`RemainingCount<=0`) 시 `OnAssignmentExhausted()`가 존 반납 + `OnTaskCompleted()` + `Dispatch->OnStationAssignmentCompleted` 후, 유휴 전환 즉시 `Dispatch->TryDispatchIdleAgents()`로 스스로(또는 대기 중이던 다른 로봇이) 다음 배정을 이어받는다.

### 배송로봇 상태머신 (`AFactoryTransportRobot`)

- `AcceptTransportTask`가 `PickupPoint` 방향으로 즉시 이동을 시작한다(대상이 `AHorizontalTray`면 `GetTransportRobotWorkLocation()`, `AStorageShelf`면 `GetTransportRobotWorkLocation(Task.FloorIndex, Task.SlotIndex, ZoneType)` — 작업 생성 시점에 트립에 미리 실려온 정확한 슬롯 위치).
- `OnArrivedAtDestination()`은 `SetState(Working)`으로 그 자리에 파킹할 뿐, 트레이/선반을 스스로 건드리지 않는다.
- `OnItemGivenByAtlas(Item)`: 아틀라스가 물품을 건네줄 때 호출 — 소켓 부착 + `DropoffPoint` 방향(같은 방식으로 슬롯 위치 계산)으로 이동 재개.
- `OnItemCollectedByAtlas()`: 아틀라스가 물품을 가져갈 때 호출 — 비우고 `OnTaskCompleted()`(카운트 증가, 이벤트 발행, `Dispatch->TryAssignIdleTransportRobot`로 다음 트립 스스로 수령).
- Blocked 판정(`AFactoryAgentBase::Tick`이 `BlockedThresholdSeconds` 경과를 감지)이 `OnBlockedTick`을 부르면 `OnEnterBlockedState()`로 `ACostZoneVolume::RegisterBlocker`를 1회 등록하고, `OnUnblocked()`에서 동일 존들에 `UnregisterBlocker`를 호출해 해제한다.

> **참고**: `AStorageShelf::InboundStagingMarker`/`OutboundStagingMarker`(및 `GetInboundStagingLocation`/`GetOutboundStagingLocation`)는 위 메인 사이클에서는 더 이상 쓰이지 않는다. 아래 `HandoffStationAssignment`(교대/로테이션, 대기실 미배치로 현재 미사용)에서만 "대체 아틀라스가 대기하는 지점"으로 여전히 사용된다.

### `EWorkZoneType` (enum)
- `ShelfInboundZone`, `ShelfOutboundZone`, `TrayWorkZone`(트레이 쪽 아틀라스 배정 — 6단계에서 실제로 생성되기 시작함)

### `FStationAssignment` (USTRUCT)
- `FGuid AssignmentID`
- `FGuid SourceOrderID` (8단계 — 어느 `FDeliveryOrder`에서 파생됐는지 추적, 주문 취소 시 역추적용으로 추가. Inbound 경로로 생성되는 배정은 개별 주문 단위가 아니라 값을 채우지 않는다)
- `EWorkZoneType ZoneType`
- `TWeakObjectPtr<AActor> TargetZoneOwner` (`AStorageShelf` 또는 `AHorizontalTray`)
- `int32 RemainingCount`
- `TWeakObjectPtr<AFactoryAtlasRobot> AssignedAtlas`
- `TArray<FReservedSlotEntry> ReservedSlots` (버그 수정 — 슬롯을 아틀라스가 즉흥적으로 정하지 않고 작업 생성 시점에 미리 예약해 큐로 들고 있는다. `AFactoryAtlasRobot::PopNextReservedSlot()`이 앞에서부터 하나씩 꺼내 쓴다. `TrayWorkZone`도 트립마다 항목을 채운다(`SlotCoord`는 (-1,-1) 고정, `TripTaskID`만 유효) — 배송로봇 매칭이 거리 대신 `TripTaskID`로 이뤄지므로 Shelf/Tray 모두 필요)

### `FReservedSlotEntry` (USTRUCT)
- `FIntPoint SlotCoord = (-1,-1)` (Shelf: X=FloorIndex, Y=SlotIndex(1-based). Tray: 슬롯 개념이 없어 항상 (-1,-1))
- `FGuid TripTaskID` (이 트립을 담당할 `FTransportTask::TaskID`와 동일한 값 — 아틀라스·배송로봇 매칭의 유일한 근거)

### `FTransportTask` (USTRUCT)
- `FGuid TaskID`
- `FGuid SourceOrderID` (8단계 — 같은 목적. 6단계부터 `DecomposeOrder`/`EnqueueInboundWork`가 실제로 채운다)
- `TWeakObjectPtr<AActor> PickupPoint` (선반 또는 트레이)
- `TWeakObjectPtr<AActor> DropoffPoint`
- `EItemType ItemType`
- `int32 FloorIndex`, `int32 SlotIndex` (버그 수정 — 배송로봇도 층과 무관하게 슬롯의 (X,Y) 위치로 직접 이동해야 해서 추가. `FStationAssignment::ReservedSlots`와 같은 시점에 같은 슬롯으로 예약된다. Tray 쪽 지점만 관여하는 트립이면 -1로 남는다)

### `FPendingHandoff` (USTRUCT)
소프트 핸드오프 진행 중인 교대 건을 추적한다. To가 스테이징 지점에 도착하기 전까지는 From이 계속 실제 거점을 점유한다.
- `FGuid AssignmentID`
- `TWeakObjectPtr<AFactoryAtlasRobot> From`
- `TWeakObjectPtr<AFactoryAtlasRobot> To`
- `EWorkZoneType ZoneType`

> **알려진 제한사항(6단계, 7단계에도 유지)**: `HandoffStationAssignment`는 `ShelfOutboundZone` 교대 시 To를 `OutboundStagingTransform`으로 보내는데, Outbound 배정의 첫 다리는 원래 슬롯에서 시작해야 해서 완전히 정합하지 않는다. 또한 `OnHandoffAtlasArrivedAtStagingPoint`는 교대로 밀려난 From을 대기실로 보내는 처리가 아직 연결돼 있지 않다(코드 내 TODO 주석 참고) — 7단계에서 대기실 자체는 실사용 가능해졌지만(`TryHeadToIdleZone` 등), 로테이션/핸드오프 경로 자체는 이번에도 스코프 밖이라 그대로 둠. 로테이션을 실제로 테스트하는 단계에서 재검토.

### `UOutboundDispatchSubsystem` (UWorldSubsystem)
주문을 거점 배정과 트립 작업으로 분해하고, 유휴 아틀라스/운송로봇에게 배정한다. 교대 시 거점 배정 소유권 이전을 담당한다.

- 멤버
  - `TArray<FStationAssignment> ActiveStationAssignments`
  - `TArray<FTransportTask> PendingTransportTasks`
  - `TMap<FGuid, FPendingHandoff> PendingHandoffs`
- 함수
  - `void DecomposeOrder(const FDeliveryOrder& Order)` (품목별로 Outbound 트레이를 먼저 찾아 확인한 뒤(버그 수정 — 원래 이 확인이 선반 배정 생성 이후에 있어 트레이를 못 찾으면 선반 배정만 덩그러니 남는 문제가 있었다) `ShelfOutboundZone` 배정 + `TrayWorkZone` 배정을 함께 생성. 수량만큼 `Shelf->TryReserveOldestOccupiedSlot`으로 슬롯을 미리 예약해 트립별 `FTransportTask`(Pickup=Shelf, Dropoff=Tray, FloorIndex/SlotIndex 포함)를 만들고, 그 `TaskID`를 `FReservedSlotEntry::TripTaskID`로 삼아 `Assignment.ReservedSlots`(SlotCoord 포함)와 `TrayAssignment.ReservedSlots`(SlotCoord 없이 TripTaskID만) 양쪽에 동시에 싣는다 — 배송로봇은 짐을 1개씩만 나르므로 트립 단위로 분리하고, 실제 재고가 요청 수량보다 적으면 예약된 만큼만 `RemainingCount`로 잡는다. 끝나면 `TryDispatchIdleAgents()` 호출)
  - `bool TryCancelAssignmentsForOrder(const FGuid& OrderID)` (8단계 — `SourceOrderID`가 일치하는 항목 중 하나라도 `AssignedAtlas`가 배정돼 있으면 전체 취소 거부. 전부 미배정이면 제거하고 성공 반환. `UDeliveryOrderSubsystem::TryCancelOrder`가 호출)
  - `bool TryAssignIdleAtlas(AFactoryAtlasRobot* Atlas, FStationAssignment& OutAssignment)` (버그 수정 — 같은 선반/트레이를 겨냥한 두 배정이 동시에 존재할 때, 이미 물리적으로 점유된 존을 겨냥한 배정은 `IsZoneOccupied`로 건너뛴다. 배정을 병합하지 않고 건너뛰기만 하는 이유는 `SourceOrderID` 기반 취소 추적을 주문 단위로 그대로 유지하기 위함. `AFactoryAtlasRobot::OnAssignmentExhausted`가 존을 반납할 때마다 `TryDispatchIdleAgents()`(자기 자신뿐 아니라 전체 유휴 로봇 스윕)를 호출해 건너뛰어졌던 배정이 다른 로봇에게 넘어가게 한다)
  - `bool TryAssignIdleTransportRobot(AFactoryTransportRobot* Robot, FTransportTask& OutTask)` (현재 픽업 대기 물품이 있는 거점 중 선택)
  - `void HandoffStationAssignment(const FGuid& AssignmentID, AFactoryAtlasRobot* From, AFactoryAtlasRobot* To)` (**소프트 핸드오프.** 즉시 점유를 넘기지 않고 `PendingHandoffs`에 등록한 뒤 To를 `AStorageShelf`의 스테이징 트랜스폼(`InboundStagingTransform`/`OutboundStagingTransform`)으로 이동시킨다. 실제 점유 이전은 `OnHandoffAtlasArrivedAtStagingPoint`에서 처리하며, 그 전까지 From은 계속 거점을 점유한 채 작업을 이어간다)
  - `void OnHandoffAtlasArrivedAtStagingPoint(const FGuid& AssignmentID)` (To의 `AFactoryAIController::OnMoveCompleted`가 스테이징 지점 도착을 알리면 호출. `AStorageShelf::TransferZoneOccupancy` 호출로 점유를 From→To로 원자적 이전 → `FStationAssignment::AssignedAtlas`를 To로 교체 → To의 `AcceptStationAssignment` 호출 → From을 대기실로 이동 → `PendingHandoffs`에서 제거)
  - `void OnStationAssignmentCompleted(const FGuid& AssignmentID)` (RemainingCount==0, 아틀라스 재배치 가능 상태로 전환; `FTaskLifecycleEvent(Completed)` 발행)
  - `void OnTransportTaskCompleted(const FGuid& TaskID)` (`FTaskLifecycleEvent(Completed)` 발행)
  - `void TryDispatchIdleAgents()` (6단계 신규 — 월드의 Idle 상태 아틀라스/배송로봇 전체를 훑어 `TryAssignIdleAtlas`/`TryAssignIdleTransportRobot`을 시도하는 Push 경로. `DecomposeOrder`/`EnqueueInboundWork`가 새 작업 생성 직후 호출해, 이미 대기 중이던 유휴 로봇이 놓치지 않고 새 작업을 받게 한다. 7단계 후속 — 줄 작업이 없는 로봇은 `AFactoryAgentBase::TryHeadToIdleZone()`으로 자신의 고정 홈 슬롯으로 보낸다)
  - `void EnqueueInboundWork(EItemType ItemType, AHorizontalTray* Tray, AStorageShelf* Shelf)` (6단계 신규 — `UInventoryOrderSubsystem::TryPlaceOrder`가 Inbound 트레이에 물품을 올린 직후 호출. `Shelf->TryReserveEmptySlot`으로 슬롯을 먼저 예약(실패하면 조용히 스킵)한 뒤 `FTransportTask`(Pickup=Tray, Dropoff=Shelf, FloorIndex/SlotIndex 포함) 1건을 만들고, 그 `TaskID`를 `TrayWorkZone`(Tray) + `ShelfInboundZone`(Shelf, `ReservedSlots`에 해당 슬롯 SlotCoord 포함) 두 배정의 `ReservedSlots`에 같은 `TripTaskID`로 함께 실은 뒤 `TryDispatchIdleAgents()` 호출)
  - `AStorageShelf* FindShelfForItemType(EItemType ItemType) const` / `AHorizontalTray* FindTrayForItemType(EItemType ItemType, ETrayDirection Direction) const` (버그 수정 — 원래 private였는데, `UInventoryOrderSubsystem::TryPlaceOrder`가 동일한 조회 루프를 별도로 재구현하고 있어 public으로 열어 재사용하도록 변경)
  - `void AssignHomeIdleZoneSlots()` (7단계 후속, private — "대기실은 선입선출이 아니라 실행 시 1회 고정 배정" 규칙. `EActorType`(Atlas/TransportRobot)별로 레벨의 로봇과 `AllowedAgentType`이 일치하는 `AIdleWaitingZone`을 각각 이름순으로 정렬(실행마다 동일한 결과 보장)한 뒤, 대기실을 순서대로 순회하며 `AIdleWaitingZone::AssignHomeSlots`로 자기 마커 개수만큼 로봇을 소비시킨다. 대기실 마커 총합이 항상 로봇 수 이상이라고 가정 — 로봇이 남는 경우는 다루지 않는다)
  - `virtual void OnWorldBeginPlay(UWorld& InWorld) override` / `void RunDeferredWorldBeginPlaySetup()` (7단계 신규. 버그 수정 — `UWorldSubsystem::OnWorldBeginPlay`는 엔진의 `UWorld::BeginPlay()` 안에서 `GameMode->StartPlay()`(레벨의 모든 액터가 실제로 `BeginPlay()`를 실행하는 지점)보다 먼저 호출된다. 그 시점에 바로 `AssignHomeIdleZoneSlots()`를 부르면 `AIdleWaitingZone::ParkingMarkers`(자기 `BeginPlay`에서 캐싱)가 아직 비어있어 아무도 배정받지 못한 채 조용히 실패한다. `OnWorldBeginPlay`는 `SetTimerForNextTick`으로 `RunDeferredWorldBeginPlaySetup()`(`AssignHomeIdleZoneSlots()` → `TryDispatchIdleAgents()` 순서)을 다음 틱으로 미루기만 하고, 실제 배정/배차는 그 안에서 처리한다. `UInventoryOrderSubsystem`의 동일 훅은 `StockLines` 시딩만 하므로 이 문제에 해당하지 않음)
