# 9. 시각화 / UI 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §9. 구현 9단계 대상. `UMG`, `Slate`, `SlateCore` 모듈이 Build.cs Private Dependency에 필요하다(`CLAUDE.md` 참고).

이벤트 버스/스냅샷 데이터를 구독해 화면에 표현하는 순수 구독자 레이어. 게임 로직에 영향을 주지 않는다. 관제실 대시보드는 2차 목표다.

> **v1.0.0 범위 확정(2026-07-22)**: 미니맵(`UMinimapWidget`/`UCongestionHeatmapSubsystem`)과 `UAgentStatusIndicatorWidget`은 v1.0.0에서 제외한다 — 향후 재도입 여부도 미정. 순수 시각화 레이어라 게임 로직에 영향이 없고(위 문단 참고), 이미 `AFactoryAgentBase`의 디버그 라벨(OperationCount, Broken 시 빨강)로 상태 확인이 대체 가능해 급하지 않다고 판단. 코드(데이터/바인딩 로직)는 그대로 남겨두되 추가 작업은 하지 않는다. v1.0.0 범위에 남는 건 `UVendorOrderListWidget`(주문 UI) 뿐이다.

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

### `UMinimapWidget` (UUserWidget) — v1.0.0 제외
- 멤버: `UCongestionHeatmapSubsystem* HeatmapSource`
- 함수: `void RefreshOverlay()`, `void OnAnomalyMarkerAdded(const FAnomalyEvent& Event)`

### `UFactoryDashboardWidget` (UUserWidget) — "관제실" 전체화면, 2차 목표
- 9단계에서는 미구현. MVP(미니맵)만 우선 구현하고 이 위젯은 후순위로 미룬다(`Docs/14_OpenIssues.md`).
- 멤버: 재고 패널, 주문 큐 패널, 로봇 상태 리스트, 최근 이상 징후 로그 (각각 하위 위젯 참조)
- 함수
  - `void RefreshAll()`
  - `void OnInventoryChanged(EItemType ItemType, int32 NewStock)`
  - `void OnAnomalyLogAppended(const FAnomalyEvent& Event)`

### `UAgentStatusIndicatorWidget` (UUserWidget, 에이전트 머리 위 부착) — v1.0.0 제외
- 멤버: `EAgentState DisplayedState`, `float RepairProgressDisplay`
- 함수: `void BindToAgent(AFactoryAgentBase* Agent)`
- `NativeTick`에서 `BoundAgent`의 `CurrentState`/`GetRepairComponent()->RepairProgress`를 매 프레임 폴링해 갱신한다. 실제 에이전트 머리 위 부착(`UWidgetComponent` 배치)은 에디터 작업으로 남아있다(`Docs/14_OpenIssues.md`).

### `UVendorOrderListWidget` (UUserWidget, Docs 이탈, 승인됨) — v1.0.0 범위, 키오스크 주문 화면 본체
`Docs/03_InventoryOrder.md` 외부업체 랜덤 주문 시스템의 표시 + 수락 UI. `UFactoryDashboardWidget`(2차 목표, 미구현)이 예정했던 "주문 큐 패널"의 축소 선행 구현 — 전체 대시보드가 아니라 이 패널 하나만 독립 위젯으로 우선 구현했다. 별도 위젯을 새로 만들지 않고, 플레이어의 인바운드 주문(A/B/C 직접 구매) 패널도 이 위젯 안에 함께 통합하기로 확정(2026-07-22) — 키오스크 상호작용 시 뜨는 화면은 이 위젯 하나뿐이다.

- 멤버: `TArray<FVendorOrderDisplay> DisplayedOrders`(`AMSmartFactoryManager::VendorOrderDisplays`의 로컬 사본, `BP_OnOrdersUpdated`로 갱신 시점을 통지)
- 함수
  - `void BindToManager(AMSmartFactoryManager* Manager)` (`OnVendorOrdersUpdated` 델리게이트 구독 시작 + 즉시 1회 갱신. `NativeTick` 폴링이 아니라 델리게이트 푸시 방식 — `VendorOrderDisplays`는 `ReplicatedUsing`이라 변경 시점에만 갱신하면 충분하다)
  - `void BindToKiosk(AFactoryKioskTerminal* Kiosk)` (`AcceptOrder`/`SubmitInboundOrder`가 호출하는 `Server_SubmitKioskOrder`의 거리 체크(`KioskInteractRadius`)를 통과하려면 로컬 플레이어가 실제로 근접한 키오스크가 필요 — 이 위젯을 배치한 곳 근처의 키오스크를 지정해야 한다)
  - `void AcceptOrder(FGuid OrderID)` (`BlueprintCallable` — 외부업체 주문 수락 버튼이 호출. `EOrderRequestType::OutboundApproval` + `TargetOrderID`로 기존 `Server_SubmitKioskOrder` RPC를 그대로 태운다)
  - `void SubmitInboundOrder(int32 QuantityA, int32 QuantityB, int32 QuantityC)` (`BlueprintCallable` — 인바운드 주문 패널의 "주문하기" 버튼이 호출. `EOrderRequestType::InboundBatch`로 A/B/C를 한 번에 제출한다. 쿨다운(`ReorderCooldownSeconds`)/자금(`SharedFunds`) 체크는 전부 서버측 `UInventoryOrderSubsystem::TryPlaceBatchOrder`가 합산 1회로 처리하므로 위젯/클라이언트 쪽 사전 검증은 없다 — 실패해도 서버 로그만 남고 조용히 무시된다)
  - `void BP_OnOrdersUpdated()` (`BlueprintImplementableEvent` — 실제 행 다시 그리기는 BP 서브클래스가 구현, C++ 쪽은 데이터+통지만 제공)
- 각 업체 한 줄: 이름 + A/B/C 수량(`bAvailable==false`면 0/0/0으로 표시 — `RequestedQuantities` 원본은 안 지움) + 수락 버튼(BP에서 `AcceptOrder(OrderID)` 바인딩).

**인바운드 주문 패널 구조(2026-07-22 확정, `WBP_VendorOrderList`)**: 품목별 수량은 서버에 제출하기 전까지 위젯이 로컬로만 들고 있는 순수 BP 상태다 — C++에 별도로 두지 않는다(게임 로직과 무관한 화면 전용 값이라 기존 "C++=데이터/RPC, BP=표시" 분리 원칙을 그대로 따름).
- BP 변수 3개(`StagedQtyA`/`B`/`C`, Integer, 기본 0).
- 품목별 `+`/`-` 버튼 6개: `+`는 해당 변수를 `+1`, `-`는 `Max(현재값 - 1, 0)`로 갱신(하한 0, 상한 없음). 텍스트 블록 3개로 각 값을 표시(바인딩 또는 버튼 클릭 시 수동 갱신, 자유).
- "주문하기" 버튼 1개: `SubmitInboundOrder(StagedQtyA, StagedQtyB, StagedQtyC)` 호출 직후 세 변수를 전부 0으로 리셋 + 텍스트 갱신. 서버 응답을 기다리지 않는 낙관적 리셋(성공/실패 무관 항상 0) — 현재 단계에서는 의도된 단순화.
- 재주문 쿨다운(5초, `AMSmartFactoryManager::ReorderCooldownSeconds`)에 대한 클라이언트 측 시각 표시(버튼 비활성화, 카운트다운 등)는 이번 범위에 포함하지 않음 — 쿨다운 중 눌러도 서버가 조용히 거부할 뿐 화면엔 아무 표시가 없다. 추후 이펙트/카운트다운 UI로 별도 작업 예정(`Docs/14_OpenIssues.md`).

**키오스크 상호작용 연결(`AFactorySpectatorPawn`/`AFactoryNPCHuman`, 2026-07-22 재설계)**: `KioskWidgetClass`(`TSubclassOf<UVendorOrderListWidget>`, EditDefaultsOnly — 에디터에서 이 위젯을 상속한 WBP를 만들어 양쪽 BP 모두에 할당 필요)를 **좌클릭**(`IA_Click`)으로 키오스크를 바라보고 누르면 `CreateWidget`+`AddToViewport`로 열고, `BindToManager`/`BindToKiosk`를 자동 호출한다. 인터렉트(F)는 빙의 전용으로 분리돼 키오스크와 무관하다(`Docs/02_Multiplayer_RPC.md` "빙의 중 입력" 참고) — **빙의 상태와 관전자 상태 모두 동일하게 동작**한다(사용자 지시, `AFactoryNPCHuman`도 같은 `OpenKioskWidget`/`CloseKioskWidget`/`FindKioskInFrontOfCamera`를 자체 구현).

- 열려있는 동안 `SetInputMode(FInputModeGameAndUI)` + `SetShowMouseCursor(true)`로 마우스 커서를 보이게 하고, 추가로 `PlayerController::SetIgnoreMoveInput(true)`/`SetIgnoreLookInput(true)`와 (`AFactoryNPCHuman`의 경우) `OnMoveTriggered`/`OnLookTriggered` 자체의 `ActiveKioskWidget` 조기 반환 가드로 캐릭터 이동/시야 회전을 완전히 막는다 — 마우스만 자유롭게 움직여 버튼을 누를 수 있어야 한다는 요구사항(2026-07-22) 반영. `AFactorySpectatorPawn`의 자유비행 이동은 레거시 축 입력 기반이라(Enhanced Input 매핑 컨텍스트와 무관) `SetIgnoreMoveInput`/`SetIgnoreLookInput`가 이를 막아주는지는 실기 확인 필요(`Docs/14_OpenIssues.md`).
- 좌클릭을 다시 누르면(위젯이 열려있는 동안은 어떤 대상을 보고 있든 무조건) 토글로 닫으며 입력모드/이동잠금을 원복한다. UMG 버튼 클릭은 `FInputModeGameAndUI`가 UI에 먼저 우선순위를 줘 좌클릭 액션까지 전달되지 않으므로 버튼을 눌러도 위젯이 닫히지 않는다 — 버튼이 없는 영역(또는 키오스크 방향 자체)을 클릭해야 닫힌다.
- 사거리 재검증(`KioskInteractRadius`) 개념은 삭제했다(2026-07-22, 사용자 지시) — 위젯을 여는 시점의 사거리(`AFactorySpectatorPawn::InteractTraceDistance`/`AFactoryNPCHuman::KioskInteractTraceDistance`, 둘 다 EditAnywhere)만이 유일한 게이트다. `Server_SubmitKioskOrder`는 더 이상 거리를 재검증하지 않는다.
- NPC 빙의/해제 등으로 폰이 `UnPossessed`될 때도 안전하게 닫는다(입력모드/이동잠금 복구 포함).

**남은 건 `KioskWidgetClass`에 할당할 실제 WBP 제작(UMG 디자인)과, 각 버튼의 텍스트 자식 위젯 Visibility를 `Not Hit-Testable`(구 명칭 Hit Test Invisible)로 설정하는 것뿐** — 그 전까지는 상호작용해도 빈 클래스라 아무 일도 안 일어난다.

### ~~`UCoopRoleHUDWidget`~~ (제거됨)
8단계에서 `EPlayerRole`/역할 배정 체계 자체가 제거되어(`Docs/02_Multiplayer_RPC.md`), 이 위젯은 더 이상 의미가 없어 9단계에서 구현하지 않는다. 다른 플레이어의 빙의 상태가 필요해지면 `UAgentStatusIndicatorWidget`처럼 대상 폰을 직접 바인딩하는 형태로 별도 설계한다.
