# 3. 입출고 / 평판 운영 루프

> Atlas_CyberDepot 아키텍처 설계안 v5 — §3. 구현 6단계(배정/디스패치)와 함께 진행.

### `UInventoryOrderSubsystem` (UWorldSubsystem)
- 멤버: `TMap<EItemType, FStockLineState> StockLines` (6단계 신규 — `OnWorldBeginPlay`가 서버 권한에서 `EItemType` 전 종류를 기본값으로 채워 넣는다. 이 초기화가 없으면 `TryPlaceOrder`가 항상 실패하던 누락이었음)
  - `FDateTime LastOrderTimestamp`(private, 런타임 상태 — 쿨다운 기준값 `ReorderCooldownSeconds`는 아래 이유로 `AMSmartFactoryManager`로 이전됨)
  - `TMap<EItemType, int32> PendingInboundQuantities`(버그 수정, 대기열 신설 — Quantity>1 주문 중 입고 트레이가 점유돼 즉시 못 올린 나머지 수량을 품목별로 누적)
- 함수
  - `bool TryPlaceOrder(EItemType ItemType, int32 Quantity)` (8단계 — 전용 플레이어 개념이 없어져 `AFactoryPlayerController::Server_SubmitKioskOrder` 경유로 누구나 호출 가능. Docs 이탈, 승인됨 — 재고 잠금 체크 통과 후 쿨다운(`AMSmartFactoryManager::ReorderCooldownSeconds`) 미경과 시 실패, 통과 시 `AMSmartFactoryManager::GetUnitPrice(ItemType) * Quantity`만큼 `TryAdjustFunds`로 공용 자금 차감(잔액 부족 시 주문 자체를 진행하지 않고 실패). 성공 시 쿨다운 타임스탬프 갱신 + `PendingInboundQuantities[ItemType] += Quantity` 후 `TryDrainInboundBacklog` 1회 시도. 실물 키오스크/MQTT 경로(`EOrderRequestType::Inbound`) 전용 — 품목 1개씩만 처리한다)
  - `bool TryPlaceBatchOrder(int32 QuantityA, int32 QuantityB, int32 QuantityC)` (금액 산정 시스템 신규, `EOrderRequestType::InboundBatch` — `UVendorOrderListWidget`의 인바운드 주문 패널 전용. `TryPlaceOrder`를 품목별로 3번 호출하면 첫 성공이 쿨다운 타임스탬프를 즉시 갱신해 나머지 두 품목이 곧바로 쿨다운에 걸려 실패하는 문제가 있어 신설 — 쿨다운 체크 1회 + A/B/C 합산 비용으로 자금 체크/차감 1회를 먼저 통과시킨 뒤, 유효한(수량>0이고 재고 잠금 안 걸린) 품목만 `PendingInboundQuantities`에 반영한다. 셋 다 무효면(수량 0 이하이거나 전부 잠김) 쿨다운 갱신 없이 실패)
  - `bool TryPlaceItemOnInboundTray(UWorld*, EItemType) -> bool`(private — 5단계 후속, `TryPlaceOrder`/`DebugForcePlaceOrder`/`TryDrainInboundBacklog` 공용. `AHorizontalTray::BoundItemType`이 일치하는 Inbound 트레이가 비어있으면 `ALogisticsItemSpawner::TryAcquireItem`으로 풀에서 물품을 꺼내 `OnItemSpawnedAtStart` 호출 후 true. 점유 중이면 아무것도 안 하고 false. 물품을 실제로 올린 직후, 같은 `ItemType`의 선반을 찾아 `UOutboundDispatchSubsystem::EnqueueInboundWork(ItemType, Tray, Shelf)`를 호출해 트레이→선반 이동 작업(아틀라스 배정 2건 + 배송로봇 트립 1건)을 생성한다 — `07_TaskAssignment.md` 참고)
  - `void TryDrainInboundBacklog(UWorld*, EItemType)`(private, 버그 수정·대기열 신설 — `PendingInboundQuantities`에 이 품목의 대기가 있으면 `TryPlaceItemOnInboundTray` 1회 시도, 성공 시 카운트 차감)
  - `void OnInboundTrayCleared(EItemType ItemType)`(버그 수정, 대기열 신설 — `AFactoryAtlasRobot::TransferItem`이 Inbound 트레이에서 물품을 집어가 트레이가 빈 직후 호출해 `TryDrainInboundBacklog`로 이어받는다. 이전엔 Quantity>1의 나머지가 아예 소실되지 않고 유령처럼 "유효 처리됐지만 안 올라간" 상태로 남았음)
  - `void OnInboundArrived(EItemType ItemType, int32 Quantity)`
  - `bool IsLineLocked(EItemType ItemType) const` (`Code:004` 선반 포화 시 true)
  - `float GetRemainingCooldownSeconds() const` (Docs 이탈, 승인됨 — UI에서 재주문 가능 시점 표시용, 이미 가능하면 0. `AMSmartFactoryManager::ReorderCooldownSeconds`를 기준값으로 조회)
  - `FOnLineLockChanged OnLineLockChanged`

> **Docs 이탈, 승인됨(구조 변경)** — `ItemPriceTable`/`ReorderCooldownSeconds`(및 `UDeliveryOrderSubsystem`의 `VendorNames`/`MinOrderIntervalSeconds`/`MaxOrderIntervalSeconds`/`MinQuantityPerItem`/`MaxQuantityPerItem`, `GetUnitPrice`/`GetSellPrice`)는 원래 각 `UWorldSubsystem`에 `EditAnywhere`로 있었으나 **`AMSmartFactoryManager`(AGameStateBase)로 전부 이전**했다. 엔진의 서브시스템 인스턴스화 방식상(`SubsystemCollection.cpp`) 상속 계층의 concrete 클래스마다(네이티브 + BP 서브클래스 각각) 별도 인스턴스가 생성되고, 게임 코드가 호출하는 `GetSubsystem<T>()`는 항상 네이티브 인스턴스만 반환한다 — 즉 `UWorldSubsystem`을 `Blueprintable`로 만들어 BP 서브클래스에서 값을 바꿔도 실제 게임에는 절대 반영되지 않는다(플레이테스트로 실제 확인됨: BP에서 `ItemPriceTable`/`ReorderCooldownSeconds`를 정확히 설정해도 가격이 항상 0, 쿨다운이 항상 기본값으로 동작). `AGameStateBase`는 BP 서브클래스가 그대로 유일한 인스턴스로 스폰되므로 `SharedFunds`와 동일한 방식으로 확실히 동작한다. 아래 `UDeliveryOrderSubsystem`/`AMSmartFactoryManager` 섹션에 반영.

### `FStockLineState` (USTRUCT)
- `EItemType ItemType`, `int32 CurrentStock`, `int32 MaxCapacity`, `bool bIsLineLocked`
- (5단계 후속 — 트레이 식별용이던 `FName BoundInboundLineID`는 미사용 상태로 죽어있어 제거. 대신 `AHorizontalTray`가 `AStorageShelf::BoundItemType`과 동일한 패턴으로 `EItemType BoundItemType`을 직접 들고 있어, `GetAllActorsOfClass` + 타입 비교로 찾는다)

### `UDeliveryOrderSubsystem` (UWorldSubsystem)
- 멤버: `TArray<FDeliveryOrder> ActiveOrders`, `float OrderRefreshIntervalSeconds`
  - `TArray<FTimerHandle> VendorTimers`(private — 업체별 독립 타이머, `AMSmartFactoryManager::VendorNames`와 인덱스로 매칭. `VendorNames`/`MinOrderIntervalSeconds`/`MaxOrderIntervalSeconds`/`MinQuantityPerItem`/`MaxQuantityPerItem`은 위 구조 변경으로 `AMSmartFactoryManager`로 이전됨)
- 함수
  - `void RefreshOrderList()`
  - `bool TryPlaceTestOrder(EItemType ItemType, int32 Quantity)` (Docs에 없는 구현값 — 6단계 사이클 테스트용, `BlueprintCallable`. `RefreshOrderList`가 아직 신규 주문을 생성하지 않아(품목/수량 랜덤화 규칙 미정, 후속 밸런싱 단계) 지정한 품목/수량으로 `FDeliveryOrder`를 즉석 생성해 `ActiveOrders`에 추가한 뒤 바로 `TryAcceptOrder`까지 호출한다)
  - `bool TryAcceptOrder(const FGuid& OrderID)` (수락 시 `UOutboundDispatchSubsystem::DecomposeOrder` 호출. 서버 게임스레드에서 순차 처리되어 동시에 여러 플레이어/현실 키오스크가 같은 주문을 승인 시도해도 먼저 처리된 쪽만 성공 — 별도 타임스탬프 큐잉 불필요. Docs 이탈, 승인됨 — `RequestedQuantities`를 품목별 `AMSmartFactoryManager::GetSellPrice`로 합산해 `TryAdjustFunds`로 공용 자금에 가산, 이후 `BroadcastVendorOrderDisplays`로 표시 사본 갱신)
  - `bool TryCancelOrder(const FGuid& OrderID)` (8단계 — `Status == Accepted`이고 `UOutboundDispatchSubsystem::TryCancelAssignmentsForOrder`가 성공(아직 로봇 미배정)할 때만 `Status`를 `Cancelled`로 전환. Cancelled/Expired 전환 시에도 표시 사본 갱신)
  - `void OnOrderExpired(const FGuid& OrderID)`
  - `void GenerateRandomVendorOrder(int32 VendorIndex)` / `void ScheduleNextVendorOrder(int32 VendorIndex)`(Docs 이탈, 승인됨, private — 타이머 만료 시 A/B/C 랜덤 수량으로 그 업체의 `FDeliveryOrder`를 새로 생성(기존 항목 있으면 교체, 간단한 구조 유지), 표시 사본 갱신 후 다음 랜덤 간격으로 재예약. `OnWorldBeginPlay`가 업체마다 최초 1회 예약)
  - `void BroadcastVendorOrderDisplays()`(private — `AMSmartFactoryManager::UpdateVendorOrderDisplays(ActiveOrders)` 호출)
  - `FOnDeliveryResult OnDeliveryResult`

### `FDeliveryOrder` (USTRUCT)
- `FGuid OrderID`, `TMap<EItemType, int32> RequestedQuantities`, `FDateTime Deadline`, `EOrderStatus Status`
- `FName VendorName`(Docs 이탈, 승인됨 — 외부업체 랜덤 주문 시스템, 어느 업체의 요청인지 식별)

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
  - `float ReorderCooldownSeconds = 5.f`(Docs 이탈, 승인됨 — `UInventoryOrderSubsystem`에서 이전. 전역 단일 재주문 쿨다운. 품목과 무관하게 마지막 주문 이후 이 시간이 지나야 다음 주문이 가능하다. "물류센터에 들어오는 차량과 연계"할 예정인 이전까지의 임시 제약 — `14_OpenIssues.md` 참고. 값 자체는 2026-07-22 기준 UI 피드백(이펙트/카운트다운) 붙이기 전까지 테스트 편의를 위한 임시 5초)
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
  - `AFactoryNPCHuman* FindNearestAvailableNPC(const FVector& Location) const` (`UnderRepair` 상태이거나 **플레이어가 빙의 중인 NPC**(`Cast<APlayerController>(NPC->GetController())`)는 후보에서 제외한다 — 8단계 멀티플레이어 정교화 항목이었으나 8단계 구현 시 누락됐던 것을 2026-07-22 실기 버그 리포트로 확인 후 추가. 없으면 빙의 중인 NPC가 본인도 모르게 다른 로봇 정비 인원으로 배정될 수 있었다)

> **선반 포화(돌발 이벤트) 처리**: 입고 물품의 전용 선반(`AStorageShelf`)이 가득 차면 해당 입고 트레이는 의도적으로 정지된 채 유지된다(데드락이 아니라 설계된 이상 상황). 해소 수단은 두 가지뿐이다 — ① 플레이어가 해당 품목 비중이 큰 외부 출고 주문을 수락해 재고를 비우거나, ② 향후 추가 예정인 폐기 처리(가칭 `ADiscardZone` + 전용 트레이, 세부 설계 미정)로 직접 비운다. 발생/해소 시점은 `Code:004` 이벤트로 기록되어 "재고 적체 패턴" 분석 데이터가 된다.
