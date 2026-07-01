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

### `UMinimapWidget` (UUserWidget)
- 멤버: `UCongestionHeatmapSubsystem* HeatmapSource`
- 함수: `void RefreshOverlay()`, `void OnAnomalyMarkerAdded(const FAnomalyEvent& Event)`

### `UFactoryDashboardWidget` (UUserWidget) — "관제실" 전체화면, 2차 목표
- 멤버: 재고 패널, 주문 큐 패널, 로봇 상태 리스트, 최근 이상 징후 로그 (각각 하위 위젯 참조)
- 함수
  - `void RefreshAll()`
  - `void OnInventoryChanged(EItemType ItemType, int32 NewStock)`
  - `void OnAnomalyLogAppended(const FAnomalyEvent& Event)`

### `UAgentStatusIndicatorWidget` (UUserWidget, 에이전트 머리 위 부착)
- 멤버: `EAgentState DisplayedState`, `float RepairProgressDisplay`
- 함수: `void BindToAgent(AFactoryAgentBase* Agent)`

### `UCoopRoleHUDWidget` (UUserWidget)
- 멤버: `EPlayerRole LocalRole`, `TArray<FRemotePlayerStatus> OtherPlayers`
- 함수: `void RefreshRemoteStatuses()`
