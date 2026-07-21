# 3. 입출고 / 평판 운영 루프

> Atlas_CyberDepot 아키텍처 설계안 v5 — §3. 구현 6단계(배정/디스패치)와 함께 진행.

### `UInventoryOrderSubsystem` (UWorldSubsystem)
- 멤버: `TMap<EItemType, FStockLineState> StockLines` (6단계 신규 — `OnWorldBeginPlay`가 서버 권한에서 `EItemType` 전 종류를 기본값으로 채워 넣는다. 이 초기화가 없으면 `TryPlaceOrder`가 항상 실패하던 누락이었음)
- 함수
  - `bool TryPlaceOrder(EItemType ItemType, int32 Quantity)` (8단계 — 전용 플레이어 개념이 없어져 `AFactoryPlayerController::Server_SubmitKioskOrder` 경유로 누구나 호출 가능. 5단계 후속 — 재고 잠금 통과 시 `AHorizontalTray::BoundItemType`이 일치하는 Inbound 트레이를 찾아 비어있으면 `ALogisticsItemSpawner::TryAcquireItem`으로 풀에서 물품을 꺼내 `OnItemSpawnedAtStart` 호출. 트레이가 이미 점유 중이면 이번 호출에선 물리적으로 올리지 않고 주문만 유효 처리 — Quantity>1의 나머지 수량을 트레이가 빌 때마다 이어서 흘려보내는 대기열은 아직 없음, 후속 과제. 6단계 신규 — 물품을 실제로 올린 직후, 같은 `ItemType`의 선반을 찾아 `UOutboundDispatchSubsystem::EnqueueInboundWork(ItemType, Tray, Shelf)`를 호출해 트레이→선반 이동 작업(아틀라스 배정 2건 + 배송로봇 트립 1건)을 생성한다 — `07_TaskAssignment.md` 참고)
  - `void OnInboundArrived(EItemType ItemType, int32 Quantity)`
  - `bool IsLineLocked(EItemType ItemType) const` (`Code:004` 선반 포화 시 true)
  - `FOnLineLockChanged OnLineLockChanged`

> **Docs 이탈, 승인됨(구조 변경)** — `ItemPriceTable`/`ReorderCooldownSeconds`(및 `UDeliveryOrderSubsystem`의 `VendorNames`/`MinOrderIntervalSeconds`/`MaxOrderIntervalSeconds`/`MinQuantityPerItem`/`MaxQuantityPerItem`, `GetUnitPrice`/`GetSellPrice`)는 원래 각 `UWorldSubsystem`에 `EditAnywhere`로 있었으나 **`AMSmartFactoryManager`(AGameStateBase)로 전부 이전**했다. 엔진의 서브시스템 인스턴스화 방식상(`SubsystemCollection.cpp`) 상속 계층의 concrete 클래스마다(네이티브 + BP 서브클래스 각각) 별도 인스턴스가 생성되고, 게임 코드가 호출하는 `GetSubsystem<T>()`는 항상 네이티브 인스턴스만 반환한다 — 즉 `UWorldSubsystem`을 `Blueprintable`로 만들어 BP 서브클래스에서 값을 바꿔도 실제 게임에는 절대 반영되지 않는다(플레이테스트로 실제 확인됨: BP에서 `ItemPriceTable`/`ReorderCooldownSeconds`를 정확히 설정해도 가격이 항상 0, 쿨다운이 항상 기본값으로 동작). `AGameStateBase`는 BP 서브클래스가 그대로 유일한 인스턴스로 스폰되므로 `SharedFunds`와 동일한 방식으로 확실히 동작한다. 아래 `UDeliveryOrderSubsystem`/`AMSmartFactoryManager` 섹션에 반영.

### `FStockLineState` (USTRUCT)
- `EItemType ItemType`, `int32 CurrentStock`, `int32 MaxCapacity`, `bool bIsLineLocked`
- (5단계 후속 — 트레이 식별용이던 `FName BoundInboundLineID`는 미사용 상태로 죽어있어 제거. 대신 `AHorizontalTray`가 `AStorageShelf::BoundItemType`과 동일한 패턴으로 `EItemType BoundItemType`을 직접 들고 있어, `GetAllActorsOfClass` + 타입 비교로 찾는다)

### `UDeliveryOrderSubsystem` (UWorldSubsystem)
- 멤버: `TArray<FDeliveryOrder> ActiveOrders`, `float OrderRefreshIntervalSeconds`
- 함수
  - `void RefreshOrderList()`
  - `bool TryPlaceTestOrder(EItemType ItemType, int32 Quantity)` (Docs에 없는 구현값 — 6단계 사이클 테스트용, `BlueprintCallable`. `RefreshOrderList`가 아직 신규 주문을 생성하지 않아(품목/수량 랜덤화 규칙 미정, 후속 밸런싱 단계) 지정한 품목/수량으로 `FDeliveryOrder`를 즉석 생성해 `ActiveOrders`에 추가한 뒤 바로 `TryAcceptOrder`까지 호출한다)
  - `bool TryAcceptOrder(const FGuid& OrderID)` (수락 시 `UOutboundDispatchSubsystem::DecomposeOrder` 호출. 서버 게임스레드에서 순차 처리되어 동시에 여러 플레이어/현실 키오스크가 같은 주문을 승인 시도해도 먼저 처리된 쪽만 성공 — 별도 타임스탬프 큐잉 불필요)
  - `bool TryCancelOrder(const FGuid& OrderID)` (8단계 — `Status == Accepted`이고 `UOutboundDispatchSubsystem::TryCancelAssignmentsForOrder`가 성공(아직 로봇 미배정)할 때만 `Status`를 `Cancelled`로 전환)
  - `void OnOrderExpired(const FGuid& OrderID)`
  - `FOnDeliveryResult OnDeliveryResult`

### `FDeliveryOrder` (USTRUCT)
- `FGuid OrderID`, `TMap<EItemType, int32> RequestedQuantities`, `FDateTime Deadline`, `EOrderStatus Status`

### `FVendorOrderDisplay` (USTRUCT, Docs 이탈, 승인됨)
`UDeliveryOrderSubsystem::ActiveOrders`(서버 전용, `UWorldSubsystem`이라 `DOREPLIFETIME` 불가)의 리플리케이트 표시 전용 사본 — `AMSmartFactoryManager::VendorOrderDisplays`에 저장된다. 게임 로직(배차 등)은 계속 `ActiveOrders`를 참조하고, 이 구조체는 UI 렌더링에만 쓰인다.
- `FName VendorName`, `FGuid OrderID`, `int32 QtyA/QtyB/QtyC`, `bool bAvailable`(`Status == Available`일 때만 true — false면 UI가 0/0/0으로 그린다. `RequestedQuantities` 원본은 수락 후에도 안 지워짐)

### `EOrderStatus` (enum)
- `Available`, `Accepted`, `Completed`, `Expired`, `Cancelled`(8단계 추가 — 예약됐지만 아직 로봇 미배정 상태에서 취소된 주문)

### `AMSmartFactoryManager` (AGameStateBase)
- 멤버: `float ReputationScore`
  - `float SharedFunds`(Docs 이탈, 승인됨 — `ReputationScore`와 동일한 패턴의 리플리케이트 공용 자금. 시작값은 클래스 디폴트에서 조정)
  - `TArray<FVendorOrderDisplay> VendorOrderDisplays`(Docs 이탈, 승인됨 — `ReplicatedUsing = OnRep_VendorOrderDisplays`. `UDeliveryOrderSubsystem`의 표시 전용 사본)
  - `FOnVendorOrdersUpdated OnVendorOrdersUpdated`(Docs 이탈, 승인됨 — `UVendorOrderListWidget`이 구독하는 순수 C++ 멀티캐스트 델리게이트, 서버/클라이언트 양쪽에서 `VendorOrderDisplays` 갱신 시 발생)
  - `UDataTable* ItemPriceTable`(Docs 이탈, 승인됨 — `UInventoryOrderSubsystem`에서 이전. `FItemTypeDefinition::UnitPrice`/`SellPrice` 행 데이터. 에디터에서 이 액터/BP의 Class Defaults에 할당 필요, 비어있으면 가격 0으로 처리됨)
  - `float ReorderCooldownSeconds = 30.f`(Docs 이탈, 승인됨 — `UInventoryOrderSubsystem`에서 이전. 전역 단일 재주문 쿨다운. 품목과 무관하게 마지막 주문 이후 이 시간이 지나야 다음 주문이 가능하다. "물류센터에 들어오는 차량과 연계"할 예정인 이전까지의 임시 제약 — `14_OpenIssues.md` 참고)
  - `TArray<FName> VendorNames`(Docs 이탈, 승인됨 — `UDeliveryOrderSubsystem`에서 이전. 외부업체 랜덤 주문 시스템, 기본 5개 placeholder, 배열 크기가 곧 업체 수)
  - `float MinOrderIntervalSeconds` / `MaxOrderIntervalSeconds`(Docs 이탈, 승인됨 — `UDeliveryOrderSubsystem`에서 이전. 업체마다 독립적으로 다음 주문까지 대기하는 랜덤 간격 범위)
  - `int32 MinQuantityPerItem` / `MaxQuantityPerItem`(Docs 이탈, 승인됨 — `UDeliveryOrderSubsystem`에서 이전. 한 번 생성될 때 A/B/C 품목당 랜덤 수량 범위. 0 포함 허용)
- 함수
  - `void AdjustReputation(float Delta, FName Reason)`
  - `bool TryAdjustFunds(float Delta, FName Reason)`(Docs 이탈, 승인됨 — 음수 조정 시 `SharedFunds + Delta < 0`이면 적용하지 않고 false 반환)
  - `int32 GetUnitPrice(EItemType) const` / `int32 GetSellPrice(EItemType) const` (Docs 이탈, 승인됨 — `UInventoryOrderSubsystem`에서 이전. `ItemPriceTable`에서 `Type`이 일치하는 행을 순회 검색, 없으면 0)
  - `void UpdateVendorOrderDisplays(const TArray<FDeliveryOrder>& ActiveOrders)`(Docs 이탈, 승인됨 — `UDeliveryOrderSubsystem`이 주문 생성/수락/취소/만료마다 호출. 서버 자신에게는 `OnRep`이 자동 호출되지 않아 `HasAuthority()`면 `OnRep_VendorOrderDisplays()`를 직접 호출한다 — `SetState`/`OnRep_CurrentState`와 동일 패턴)
  - `void RequestMaintenance(AFactoryAgentBase* Agent, ERepairType RepairType)` (Broken은 즉시 최우선으로 `FindNearestAvailableNPC` 호출; 단 해당 로봇의 `URepairProgressComponent`에 AI NPC가 이미 배정된 경우 추가 AI NPC 배정 없음. PendingMaintenance는 대기 공간 도착 후 `OnAgentBecameIdle` 경유로만 호출)
  - `void OnAgentBecameIdle(AFactoryAgentBase* Agent)` (대기 공간 도착 시 호출, `IsMaintenanceDue()` 체크)
  - `void OnRepairCompleted(AFactoryAgentBase* Agent)` (FullRepair 완료 후 각 `AIdleWaitingZone`의 `ShouldDispatchNPCForMaintenance()` 재평가 트리거; 조건 충족 시 즉시 배치 정비 발동)
  - `AFactoryNPCHuman* FindNearestAvailableNPC(const FVector& Location) const`

> **선반 포화(돌발 이벤트) 처리**: 입고 물품의 전용 선반(`AStorageShelf`)이 가득 차면 해당 입고 트레이는 의도적으로 정지된 채 유지된다(데드락이 아니라 설계된 이상 상황). 해소 수단은 두 가지뿐이다 — ① 플레이어가 해당 품목 비중이 큰 외부 출고 주문을 수락해 재고를 비우거나, ② 향후 추가 예정인 폐기 처리(가칭 `ADiscardZone` + 전용 트레이, 세부 설계 미정)로 직접 비운다. 발생/해소 시점은 `Code:004` 이벤트로 기록되어 "재고 적체 패턴" 분석 데이터가 된다.
