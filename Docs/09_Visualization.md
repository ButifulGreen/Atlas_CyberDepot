# 9. 시각화 / UI 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §9. 구현 9단계 대상. `UMG`, `Slate`, `SlateCore` 모듈이 Build.cs Private Dependency에 필요하다(`CLAUDE.md` 참고).

이벤트 버스/스냅샷 데이터를 구독해 화면에 표현하는 순수 구독자 레이어. 게임 로직에 영향을 주지 않는다. MVP는 미니맵(혼잡도+이상상황 마킹)이고, 관제실 대시보드는 2차 목표다.

### `UCongestionHeatmapSubsystem` (UWorldSubsystem)
- 멤버
  - `TMap<FIntPoint, float> CellCongestionScore` (그리드 셀 단위 이동 경로 누적치)
  - `float UpdateIntervalSeconds = 8.f`
  - `float DecayRatePerUpdate = 0.85f`
- 함수
  - `void OnAgentSnapshot(const FStateSnapshot& Snapshot)` (이동 경로 누적)
  - `void TickRecompute(float DeltaTime)` (UpdateIntervalSeconds마다 감쇠 후 재계산, 매 틱 연산 금지)
  - `TArray<FCongestionCell> GetCurrentSnapshot() const`

### `FCongestionCell` (USTRUCT)
- `FIntPoint GridCoord`, `float Score`, `bool bHasActiveAnomaly` (고장/정비 발생 경로 마킹용)

> **구현 비고**: `TickRecompute(float DeltaTime)`는 `UCongestionHeatmapSubsystem`이 `FTickableGameObject`를 함께 상속해 매 틱 호출되며, 내부에서 경과 시간을 누적하다가 `UpdateIntervalSeconds`에 도달했을 때만 감쇠 연산을 수행한다("매 틱 연산 금지" 요구사항 충족). `FVector Location`을 그리드 셀로 변환하는 단위 크기는 Docs에 없어 `GridCellSize`(밸런싱 값, 기본 200)로 노출했다. `bHasActiveAnomaly` 마킹은 `UpdateIntervalSeconds` 주기마다 함께 정리(초기화)된다.

### `UMinimapWidget` (UUserWidget)
- 멤버: `UCongestionHeatmapSubsystem* HeatmapSource`
- 함수: `void RefreshOverlay()`, `void OnAnomalyMarkerAdded(const FAnomalyEvent& Event)`

### `UFactoryDashboardWidget` (UUserWidget) — "관제실" 전체화면, 2차 목표
- 9단계에서는 미구현. MVP(미니맵)만 우선 구현하고 이 위젯은 후순위로 미룬다(`Docs/14_OpenIssues.md`).
- 멤버: 재고 패널, 주문 큐 패널, 로봇 상태 리스트, 최근 이상 징후 로그 (각각 하위 위젯 참조)
- 함수
  - `void RefreshAll()`
  - `void OnInventoryChanged(EItemType ItemType, int32 NewStock)`
  - `void OnAnomalyLogAppended(const FAnomalyEvent& Event)`

### `UAgentStatusIndicatorWidget` (UUserWidget, 에이전트 머리 위 부착)
- 멤버: `EAgentState DisplayedState`, `float RepairProgressDisplay`
- 함수: `void BindToAgent(AFactoryAgentBase* Agent)`
- `NativeTick`에서 `BoundAgent`의 `CurrentState`/`GetRepairComponent()->RepairProgress`를 매 프레임 폴링해 갱신한다. 실제 에이전트 머리 위 부착(`UWidgetComponent` 배치)은 에디터 작업으로 남아있다(`Docs/14_OpenIssues.md`).

### `UVendorOrderListWidget` (UUserWidget, Docs 이탈, 승인됨)
`Docs/03_InventoryOrder.md` 외부업체 랜덤 주문 시스템의 표시 + 수락 UI. `UFactoryDashboardWidget`(2차 목표, 미구현)이 예정했던 "주문 큐 패널"의 축소 선행 구현 — 전체 대시보드가 아니라 이 패널 하나만 독립 위젯으로 우선 구현했다.

- 멤버: `TArray<FVendorOrderDisplay> DisplayedOrders`(`AMSmartFactoryManager::VendorOrderDisplays`의 로컬 사본, `BP_OnOrdersUpdated`로 갱신 시점을 통지)
- 함수
  - `void BindToManager(AMSmartFactoryManager* Manager)` (`OnVendorOrdersUpdated` 델리게이트 구독 시작 + 즉시 1회 갱신. `UAgentStatusIndicatorWidget::BindToAgent`와 동일한 바인딩 철학이지만, `NativeTick` 폴링이 아니라 델리게이트 푸시 방식 — `VendorOrderDisplays`는 `ReplicatedUsing`이라 변경 시점에만 갱신하면 충분하다)
  - `void BindToKiosk(AFactoryKioskTerminal* Kiosk)` (`AcceptOrder`가 호출하는 `Server_SubmitKioskOrder`의 거리 체크(`KioskInteractRadius`)를 통과하려면 로컬 플레이어가 실제로 근접한 키오스크가 필요 — 이 위젯을 배치한 곳 근처의 키오스크를 지정해야 한다. 관제실 화면처럼 항상 떠 있는 UI를 의도한다면, 그 키오스크 앞에 있을 때만 수락 버튼이 실제로 동작한다는 제약이 함께 따라온다)
  - `void AcceptOrder(FGuid OrderID)` (`BlueprintCallable` — 수락 버튼이 호출. `EOrderRequestType::OutboundApproval` + `TargetOrderID`로 기존 `Server_SubmitKioskOrder` RPC를 그대로 태운다. 새 RPC를 추가하지 않았다)
  - `void BP_OnOrdersUpdated()` (`BlueprintImplementableEvent` — 실제 행 다시 그리기는 BP 서브클래스가 구현, C++ 쪽은 데이터+통지만 제공)
- 각 업체 한 줄: 이름 + A/B/C 수량(`bAvailable==false`면 0/0/0으로 표시 — `RequestedQuantities` 원본은 안 지움) + 수락 버튼(BP에서 `AcceptOrder(OrderID)` 바인딩).

### ~~`UCoopRoleHUDWidget`~~ (제거됨)
8단계에서 `EPlayerRole`/역할 배정 체계 자체가 제거되어(`Docs/02_Multiplayer_RPC.md`), 이 위젯은 더 이상 의미가 없어 9단계에서 구현하지 않는다. 다른 플레이어의 빙의 상태가 필요해지면 `UAgentStatusIndicatorWidget`처럼 대상 폰을 직접 바인딩하는 형태로 별도 설계한다.
