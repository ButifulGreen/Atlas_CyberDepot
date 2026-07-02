# 3. 입출고 / 평판 운영 루프

> Atlas_CyberDepot 아키텍처 설계안 v5 — §3. 구현 6단계(배정/디스패치)와 함께 진행.

### `UInventoryOrderSubsystem` (UWorldSubsystem)
- 멤버: `TMap<EItemType, FStockLineState> StockLines`
- 함수
  - `bool TryPlaceOrder(EItemType ItemType, int32 Quantity)` (8단계 — 전용 플레이어 개념이 없어져 `AFactoryPlayerController::Server_SubmitKioskOrder` 경유로 누구나 호출 가능)
  - `void OnInboundArrived(EItemType ItemType, int32 Quantity)`
  - `bool IsLineLocked(EItemType ItemType) const` (`Code:004` 선반 포화 시 true)
  - `FOnLineLockChanged OnLineLockChanged`

### `FStockLineState` (USTRUCT)
- `EItemType ItemType`, `int32 CurrentStock`, `int32 MaxCapacity`, `bool bIsLineLocked`, `FName BoundInboundLineID`

### `UDeliveryOrderSubsystem` (UWorldSubsystem)
- 멤버: `TArray<FDeliveryOrder> ActiveOrders`, `float OrderRefreshIntervalSeconds`
- 함수
  - `void RefreshOrderList()`
  - `bool TryAcceptOrder(const FGuid& OrderID)` (수락 시 `UOutboundDispatchSubsystem::DecomposeOrder` 호출. 서버 게임스레드에서 순차 처리되어 동시에 여러 플레이어/현실 키오스크가 같은 주문을 승인 시도해도 먼저 처리된 쪽만 성공 — 별도 타임스탬프 큐잉 불필요)
  - `bool TryCancelOrder(const FGuid& OrderID)` (8단계 — `Status == Accepted`이고 `UOutboundDispatchSubsystem::TryCancelAssignmentsForOrder`가 성공(아직 로봇 미배정)할 때만 `Status`를 `Cancelled`로 전환)
  - `void OnOrderExpired(const FGuid& OrderID)`
  - `FOnDeliveryResult OnDeliveryResult`

### `FDeliveryOrder` (USTRUCT)
- `FGuid OrderID`, `TMap<EItemType, int32> RequestedQuantities`, `FDateTime Deadline`, `EOrderStatus Status`

### `EOrderStatus` (enum)
- `Available`, `Accepted`, `Completed`, `Expired`, `Cancelled`(8단계 추가 — 예약됐지만 아직 로봇 미배정 상태에서 취소된 주문)

### `AMSmartFactoryManager` (AGameStateBase)
- 멤버: `float ReputationScore`
- 함수
  - `void AdjustReputation(float Delta, FName Reason)`
  - `void RequestMaintenance(AFactoryAgentBase* Agent, ERepairType RepairType)` (Broken은 즉시 최우선으로 `FindNearestAvailableNPC` 호출; 단 해당 로봇의 `URepairProgressComponent`에 AI NPC가 이미 배정된 경우 추가 AI NPC 배정 없음. PendingMaintenance는 대기 공간 도착 후 `OnAgentBecameIdle` 경유로만 호출)
  - `void OnAgentBecameIdle(AFactoryAgentBase* Agent)` (대기 공간 도착 시 호출, `IsMaintenanceDue()` 체크)
  - `void OnRepairCompleted(AFactoryAgentBase* Agent)` (FullRepair 완료 후 각 `AIdleWaitingZone`의 `ShouldDispatchNPCForMaintenance()` 재평가 트리거; 조건 충족 시 즉시 배치 정비 발동)
  - `AFactoryNPCHuman* FindNearestAvailableNPC(const FVector& Location) const`

> **선반 포화(돌발 이벤트) 처리**: 입고 물품의 전용 선반(`AStorageShelf`)이 가득 차면 해당 입고 트레이는 의도적으로 정지된 채 유지된다(데드락이 아니라 설계된 이상 상황). 해소 수단은 두 가지뿐이다 — ① 플레이어가 해당 품목 비중이 큰 외부 출고 주문을 수락해 재고를 비우거나, ② 향후 추가 예정인 폐기 처리(가칭 `ADiscardZone` + 전용 트레이, 세부 설계 미정)로 직접 비운다. 발생/해소 시점은 `Code:004` 이벤트로 기록되어 "재고 적체 패턴" 분석 데이터가 된다.
