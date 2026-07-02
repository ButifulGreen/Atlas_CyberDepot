# 14. 남겨둔 의도적 미해결 사항

> Atlas_CyberDepot 아키텍처 설계안 v5 — §14. 어떤 시스템을 구현하든 착수 전에 이 문서를 훑어, 해당 시스템에 걸린 미해결/미채택 사항이 있는지 확인할 것.

- `ACostZoneVolume`을 NavQueryFilter 런타임 코스트 조정 방식으로 전환했지만, 그럼에도 구현 후 `stat navigation`으로 실측은 필요하다(코스트 조정 자체도 쿼리마다 계산 비용이 있으므로).
- ~~키오스크 전용 플레이어가 "주문 승인" 외에 NPC 디스패치 판단 같은 추가 권한을 가질지는 `02_Multiplayer_RPC.md` 확정 시 함께 정한다.~~ → 8단계에서 해소: 키오스크는 전용 플레이어가 아닌 인게임 장비로 재설계되어 전원 동일 권한(`02_Multiplayer_RPC.md`).
- (8단계 신규) `FTransportTask::SourceOrderID`는 필드만 추가됐고 실제로 채워지지 않는다 — `UOutboundDispatchSubsystem::DecomposeOrder`가 아직 `PendingTransportTasks`를 채우는 로직이 없기 때문(`07_TaskAssignment.md`). 배정/디스패치 로직이 정교화되는 이후 단계에서 함께 채우고, 그때 운송 작업 취소 지원 여부도 재검토한다.
- (8단계 신규) `AFactorySpectatorPawn`의 `FactoryBoundary` 콜리전 채널은 코드/설정으로 정의만 해뒀고, 실제 경계 볼륨 배치는 레벨이 만들어지는 시점(에디터 작업)으로 미뤄져 있다.
- (8단계 신규) `AFactoryNPCHuman::ReleasePossession()`은 `SpawnDefaultController()`로 AI 제어 복귀를 시도하는데, 이는 각 로봇/NPC 액터의 `AutoPossessAI`/`AIControllerClass` 설정이 레벨/블루프린트에서 되어 있어야 실제로 동작한다 — 현재 코드베이스에는 이 설정이 어디에도 없어 실기 확인이 필요하다.
- (8단계 신규) Enhanced Input 에셋(`IA_Interact`, 매핑 컨텍스트)은 코드로 생성할 수 없어 에디터에서 별도 제작 필요.
- 선반 포화 해소 수단 중 "폐기 처리"의 구체 설계(전용 구역/트레이 위치, 폐기 트리거 방식, 폐기된 물품의 후속 처리)는 미정이다.
- `UFactoryDashboardWidget`(관제실)의 인게임 접근 방식 — 특정 위치에 진입해야 여는 형태인지, 아무 위치에서나 단축키로 호출 가능한 형태인지 — 는 미정이다. (9단계에서도 미구현 확정, 후순위 유지)
- (9단계 신규) `UCoopRoleHUDWidget`은 8단계에서 `EPlayerRole`이 제거되며 구현하지 않기로 확정했다(`Docs/09_Visualization.md`).
- (9단계 신규) `AMyMQTTClient`가 실제 브로커에 연결할 언리얼 MQTT 플러그인이 아직 선택/설치되지 않았다 — 큐잉/JSON 직렬화/키오스크 수신 디스패치까지만 구현되고, `Connect()`/`FlushPendingQueue()`의 실제 소켓 발행은 `TODO`로 남아있다(`Docs/11_MQTT.md`).
- (9단계 신규) MQTT 토픽 이름(`atlas_cyberdepot/anomaly` 등)과 JSON 필드 대소문자(`FJsonObjectConverter` 기본 변환 규칙)는 실제 브로커 연동 테스트 전까지 잠정치다 — 플러그인 연결 시 재검증 필요.
- (9단계 신규) `UAgentStatusIndicatorWidget`을 에이전트 머리 위에 실제로 부착하는 `UWidgetComponent` 배치는 에디터/Content 작업으로 남아있다.
- (9단계 신규) `UMinimapWidget`/`UAgentStatusIndicatorWidget`의 실제 드로잉은 `BlueprintImplementableEvent`만 정의돼 있고, UMG 디자이너에서 위젯 블루프린트를 만들어 구현해야 한다.
- `UCongestionHeatmapSubsystem`의 `UpdateIntervalSeconds`(예시 8초), `DecayRatePerUpdate`(예시 0.85)는 가안이며, 실제 플레이 규모(로봇 약 30대)에서 시각적으로 의미 있는 값인지 구현 후 튜닝이 필요하다.
- `AIdleWaitingZone`의 `RestDecayIntervalSeconds`(예시 10초)·`RestDecayAmountPerInterval`(예시 10)·`FullyRestedThresholdRatio`(예시 0.2)와 각 로봇의 `MaintenanceThreshold`는 상호 균형이 중요하다. 실제 로봇 운용 규모에서 플레이테스트 후 튜닝 필요.
- `HandoffStationAssignment`를 소프트 핸드오프로 바꿨지만, To가 스테이징 지점에서 대기하는 동안 제3의 아틀라스가 해당 스테이징 지점을 우연히 지나가는 경합은 발생하지 않는지(스테이징 지점은 점유 예약 대상이 아니므로) 구현 후 검증 필요.
- (10단계 신규) `UBenchmarkHarnessSubsystem::RecordPerfSample`의 `NavigationTickTimeMs`는 대응하는 공개 전역 카운터가 없어 항상 0 — 실측은 `stat navigation` 수동 확인에 계속 의존한다.
- (10단계 신규) `RunScalingComparison`/`StartForcedDeadlockDemo`는 레벨/내비메시가 없는 기획단계 특성상 코드 구조만 완성됐고 실기 계측·시연 테스트는 못 했다. 레벨이 만들어지면 `AgentClassToSpawn`/`SpawnOrigin` 등 밸런싱 값을 실제 레벨에 맞게 조정하고 재검증 필요.
- (10단계 신규) `UReplayPlaybackSubsystem`이 방출하는 `FOnPlaybackFrame` 델리게이트를 실제 화면에 어떻게 시각화(고스트 액터 스폰 등)할지는 미정 — 소비자 측 설계가 필요하다.

## 검토 후 미채택 사항 (근거 포함)

- **예약 락 타임아웃/리스(lease) 패턴**: 로봇이 고장나도 슬롯/거점 예약을 유지한 채 수리 후 재개하는 것이 설계 원칙(`00_DesignPrinciples.md`)이고, FullRepair는 유한 시간 내 항상 완료되므로 영구 락(zombie lock) 위험이 구조적으로 없다. 타임아웃으로 강제 해제하면 오히려 "재개" 원칙이 깨진다. 미채택.
- **고장 위험 로봇을 시스템이 강제로 대기실까지 Navigate시키는 안**: `EvaluateRotationOrContinue` 기반 로테이션/핸드오프가 이동 직전·`TransferItem` 직전이라는 자연스러운 작업 경계에서만 판단하므로, 임무 도중 시스템이 로봇을 강제 인터럽트하는 것보다 우아하고 원칙에도 부합한다. 미채택.
- **배치 정비 중단 시 `Paused` 상태 별도 도입**: `RepairProgress`가 로봇 컴포넌트에 저장되고 `ActiveRepairers`가 0이 되어도 값은 보존되므로(진행 속도만 0), 중단 후 `BatchMaintenanceTargetSet`을 초기화하고 재트리거 시 새 스냅샷을 뜨는 현재 방식으로도 진행 상황 유실이 없다. 별도 상태머신을 추가하면 오버엔지니어링. 미채택.
