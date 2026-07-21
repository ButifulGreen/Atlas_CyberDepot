# 14. 남겨둔 의도적 미해결 사항

> Atlas_CyberDepot 아키텍처 설계안 v5 — §14. 어떤 시스템을 구현하든 착수 전에 이 문서를 훑어, 해당 시스템에 걸린 미해결/미채택 사항이 있는지 확인할 것.

- `ACostZoneVolume`을 NavQueryFilter 런타임 코스트 조정 방식으로 전환했지만, 그럼에도 구현 후 `stat navigation`으로 실측은 필요하다(코스트 조정 자체도 쿼리마다 계산 비용이 있으므로).
- ~~키오스크 전용 플레이어가 "주문 승인" 외에 NPC 디스패치 판단 같은 추가 권한을 가질지는 `02_Multiplayer_RPC.md` 확정 시 함께 정한다.~~ → 8단계에서 해소: 키오스크는 전용 플레이어가 아닌 인게임 장비로 재설계되어 전원 동일 권한(`02_Multiplayer_RPC.md`).
- ~~(8단계 신규) `FTransportTask::SourceOrderID`는 필드만 추가됐고 실제로 채워지지 않는다 — `UOutboundDispatchSubsystem::DecomposeOrder`가 아직 `PendingTransportTasks`를 채우는 로직이 없기 때문(`07_TaskAssignment.md`).~~ → 6단계 오케스트레이션 작업에서 해소: `DecomposeOrder`/`EnqueueInboundWork`가 실제로 채운다. 단, 운송 작업 취소(`TryCancelAssignmentsForOrder`가 `PendingTransportTasks`는 건드리지 않음)는 아직 미지원 — 재검토 필요.
- (6단계 신규) `UOutboundDispatchSubsystem::HandoffStationAssignment`는 `ShelfOutboundZone` 교대 시 To를 `OutboundStagingTransform`으로 보내는데, Outbound 배정의 첫 다리는 원래 슬롯에서 시작해야 해서 완전히 정합하지 않는다(`07_TaskAssignment.md` 참고). 대기실(`AIdleWaitingZone`)이 아직 레벨에 배치되지 않아 실사용 안 돼 당장 영향 없음 — 대기실/교대를 실제로 테스트하는 단계에서 재검토.
- ~~(6단계 신규) 아틀라스의 파트너(배송로봇) 대기 재시도(`OnWorkingTick` + `ZoneRetryIntervalSeconds`)와 배송로봇 탐색 반경(`RendezvousSearchRadius`)은 레벨 제작 후 실제 통로 길이/로봇 속도에 맞춰 튜닝이 필요한 가안이다.~~ → 6단계 후속에서 해소: 거리 기반 탐색(`RendezvousSearchRadius`) 자체를 폐기하고 `TripTaskID`(`FTransportTask::TaskID`) 정확 매칭으로 교체(`07_TaskAssignment.md`). `ZoneRetryIntervalSeconds`(재시도 간격)는 여전히 레벨 제작 후 튜닝이 필요한 가안으로 남는다.
- (8단계 신규) `AFactorySpectatorPawn`의 `FactoryBoundary` 콜리전 채널은 코드/설정으로 정의만 해뒀고, 실제 경계 볼륨 배치는 레벨이 만들어지는 시점(에디터 작업)으로 미뤄져 있다.
- (8단계 신규) `AFactoryNPCHuman::ReleasePossession()`은 `SpawnDefaultController()`로 AI 제어 복귀를 시도하는데, 이는 각 로봇/NPC 액터의 `AutoPossessAI`/`AIControllerClass` 설정이 레벨/블루프린트에서 되어 있어야 실제로 동작한다 — 현재 코드베이스에는 이 설정이 어디에도 없어 실기 확인이 필요하다.
- (8단계 신규) Enhanced Input 에셋(`IA_Interact`, 매핑 컨텍스트)은 코드로 생성할 수 없어 에디터에서 별도 제작 필요.
- 선반 포화 해소 수단 중 "폐기 처리"의 구체 설계(전용 구역/트레이 위치, 폐기 트리거 방식, 폐기된 물품의 후속 처리)는 미정이다.
- `UFactoryDashboardWidget`(관제실)의 인게임 접근 방식 — 특정 위치에 진입해야 여는 형태인지, 아무 위치에서나 단축키로 호출 가능한 형태인지 — 는 미정이다. (9단계에서도 미구현 확정, 후순위 유지)
- (9단계 신규) `UCoopRoleHUDWidget`은 8단계에서 `EPlayerRole`이 제거되며 구현하지 않기로 확정했다(`Docs/09_Visualization.md`).
- ~~(9단계 신규) `AMyMQTTClient`가 실제 브로커에 연결할 언리얼 MQTT 플러그인이 아직 선택/설치되지 않았다~~ → 후속 갱신에서 해소: Eclipse Paho MQTT C(`MQTTAsync`)를 서드파티로 직접 연동(`Docs/11_MQTT.md`).
- ~~(MQTT 후속 신규) `Source/ThirdParty/PahoMQTT/`에 실제 라이브러리 파일이 아직 배치되지 않아 컴파일이 안 된다~~ → 해소: 사용자가 보유한 파일을 배치, 컴파일 확인됨.
- (MQTT 후속 신규) 실기 브로커가 없어 `Connect()`/구독/발행 전체가 컴파일만 확인됐고 실제 연동 테스트는 못 했다.
- (MQTT 후속 신규) `Binaries/`에는 `paho-mqtt3as.dll`/`.pdb`만 있고 OpenSSL 등 이 dll이 런타임에 의존할 수 있는 추가 라이브러리(`libssl`/`libcrypto` 등)는 확인되지 않았다 — 실행 시 로드 실패하면 별도로 구해 배치해야 할 수 있다.
- (MQTT 후속 신규) `TryPublish`는 브로커가 발행을 사후에 거부(비동기 실패 콜백)해도 자동으로 `PendingPublishQueue`에 재적재하지 않는다 — 미연결 상태의 즉시 실패만 큐잉 대상이다.
- (9단계 신규) MQTT 토픽 이름(`atlas_cyberdepot/anomaly` 등)과 JSON 필드 대소문자(`FJsonObjectConverter` 기본 변환 규칙)는 실제 브로커 연동 테스트 전까지 잠정치다 — 플러그인 연결 시 재검증 필요.
- (9단계 신규) `UAgentStatusIndicatorWidget`을 에이전트 머리 위에 실제로 부착하는 `UWidgetComponent` 배치는 에디터/Content 작업으로 남아있다.
- (9단계 신규) `UMinimapWidget`/`UAgentStatusIndicatorWidget`의 실제 드로잉은 `BlueprintImplementableEvent`만 정의돼 있고, UMG 디자이너에서 위젯 블루프린트를 만들어 구현해야 한다.
- `UCongestionHeatmapSubsystem`의 `UpdateIntervalSeconds`(예시 8초), `DecayRatePerUpdate`(예시 0.85)는 가안이며, 실제 플레이 규모(로봇 약 30대)에서 시각적으로 의미 있는 값인지 구현 후 튜닝이 필요하다.
- `AIdleWaitingZone`의 `RestDecayIntervalSeconds`(예시 10초)·`RestDecayAmountPerInterval`(예시 10)·`FullyRestedThresholdRatio`(예시 0.2)와 각 로봇의 `MaintenanceThreshold`는 상호 균형이 중요하다. 실제 로봇 운용 규모에서 플레이테스트 후 튜닝 필요.
- `HandoffStationAssignment`를 소프트 핸드오프로 바꿨지만, To가 스테이징 지점에서 대기하는 동안 제3의 아틀라스가 해당 스테이징 지점을 우연히 지나가는 경합은 발생하지 않는지(스테이징 지점은 점유 예약 대상이 아니므로) 구현 후 검증 필요.
- (10단계 신규) `UBenchmarkHarnessSubsystem::RecordPerfSample`의 `NavigationTickTimeMs`는 대응하는 공개 전역 카운터가 없어 항상 0 — 실측은 `stat navigation` 수동 확인에 계속 의존한다.
- (10단계 신규) `RunScalingComparison`/`StartForcedDeadlockDemo`는 레벨/내비메시가 없는 기획단계 특성상 코드 구조만 완성됐고 실기 계측·시연 테스트는 못 했다. 레벨이 만들어지면 `AgentClassToSpawn`/`SpawnOrigin` 등 밸런싱 값을 실제 레벨에 맞게 조정하고 재검증 필요.
- (10단계 신규) `UReplayPlaybackSubsystem`이 방출하는 `FOnPlaybackFrame` 델리게이트를 실제 화면에 어떻게 시각화(고스트 액터 스폰 등)할지는 미정 — 소비자 측 설계가 필요하다.
- ~~(5단계 후속 신규) `UInventoryOrderSubsystem::TryPlaceOrder`가 Inbound 트레이에 물품을 올릴 때, 트레이가 이미 점유 중이면 이번 호출에서는 물리적으로 안 올리고 넘어간다 — `Quantity`가 1보다 큰 주문의 나머지 수량을 트레이가 빌 때마다 이어서 자동으로 흘려보내는 대기열이 없다.~~ → 해소: `PendingInboundQuantities`(품목별 누적) + `AFactoryAtlasRobot::TransferItem`이 Inbound 트레이를 비운 직후 호출하는 `OnInboundTrayCleared` → `TryDrainInboundBacklog`로, 트레이가 빌 때마다 대기 수량을 1개씩 이어서 흘려보낸다.
- ~~(7단계 후속 신규) 자연 발생 고장에서 복구된 로봇은 `SetState(Idle)`만 될 뿐, 보존된 배정/트립을 실제로 이어서 재개하는 로직이 없다.~~ → 실기 테스트로 확인된 연쇄 정지(적재 선반에서 아틀라스 고장 시 대기 중인 배송로봇도 영구 정지, 그 배송로봇은 어떤 디스패치 트리거로도 재개 불가)를 계기로 해소: `AFactoryAtlasRobot`/`AFactoryTransportRobot::ResumeAfterRepair()`가 `CurrentAssignment`/`CurrentTask`가 유효하면 `Idle` 대신 `Working`으로 복귀시킨다. 아틀라스는 기존 `OnWorkingTick`의 `ZoneRetryIntervalSeconds` 재시도 루프(`ContinueShelfAssignment`/`ContinueTrayAssignment`, 원래부터 재진입 안전하게 설계됨)가 자동으로 이어받고, 배송로봇은 능동 재시도가 없어도 `Working`으로만 돌아가면 아틀라스 쪽 `FindWaitingTransportRobot`(`CurrentState==Working` 요구)이 다시 찾아낸다.
- (금액 산정 시스템 신규) `UInventoryOrderSubsystem::ReorderCooldownSeconds`(전역 단일 재주문 대기시간)는 "물류센터에 들어오는 차량과 연계"할 예정인 임시 제약이다 — 실제 차량 도착/배송 시스템이 설계되면 고정 시간 대신 그 시스템이 재주문 가능 시점을 결정하도록 대체해야 한다.
- (금액 산정 시스템 신규) `UVendorOrderListWidget::AcceptOrder`는 기존 `Server_SubmitKioskOrder` RPC를 재사용하는데, 이 RPC는 `SourceKiosk`와의 물리적 거리(`KioskInteractRadius`)를 검사한다. 즉 이 위젯이 "관제실에 항상 떠 있는 화면"으로 의도된 것이라면, 플레이어가 `BindToKiosk`로 지정한 키오스크 근처에 실제로 있어야만 수락이 성공한다는 제약이 함께 따라온다 — 위젯을 배치할 때 키오스크 위치와의 관계를 의도적으로 맞춰야 하며, 만약 위치 무관하게 항상 수락 가능해야 한다면 거리 체크가 없는 별도 RPC를 새로 만들어야 한다(현재는 재사용 우선으로 미대응).
- (회피 시스템 재설계 논의 중 신규) `ACostZoneVolume`/`AFactoryAIController::ApplyDynamicCongestionCost`(동적 혼잡 코스트) — 삭제 후보. 코스트 오버라이드가 우리 웨이포인트 그래프가 아니라 엔진 NavMesh 이동(`MoveTo`)의 NavArea 클래스 전체에 적용되는 방식이라, 짧고 대안 경로가 거의 없는 웨이포인트 홉/FinalHop 구간에서는 실효가 미미하다. 아틀라스는 애초에 이 시스템을 쓰지 않는다(`AFactoryTransportRobot`만 `OnBlockedTick`/`OnUnblocked`를 오버라이드). 자유이동 구간을 최소화하는 방향(회피 재설계)과 태생적으로 맞지 않음 — 지금 당장 제거하지 않고, 재설계 완료 후 재검토.

## 검토 후 미채택 사항 (근거 포함)

- **예약 락 타임아웃/리스(lease) 패턴**: 로봇이 고장나도 슬롯/거점 예약을 유지한 채 수리 후 재개하는 것이 설계 원칙(`00_DesignPrinciples.md`)이고, FullRepair는 유한 시간 내 항상 완료되므로 영구 락(zombie lock) 위험이 구조적으로 없다. 타임아웃으로 강제 해제하면 오히려 "재개" 원칙이 깨진다. 미채택.
- **고장 위험 로봇을 시스템이 강제로 대기실까지 Navigate시키는 안**: `EvaluateRotationOrContinue` 기반 로테이션/핸드오프가 이동 직전·`TransferItem` 직전이라는 자연스러운 작업 경계에서만 판단하므로, 임무 도중 시스템이 로봇을 강제 인터럽트하는 것보다 우아하고 원칙에도 부합한다. 미채택.
- **배치 정비 중단 시 `Paused` 상태 별도 도입**: `RepairProgress`가 로봇 컴포넌트에 저장되고 `ActiveRepairers`가 0이 되어도 값은 보존되므로(진행 속도만 0), 중단 후 `BatchMaintenanceTargetSet`을 초기화하고 재트리거 시 새 스냅샷을 뜨는 현재 방식으로도 진행 상황 유실이 없다. 별도 상태머신을 추가하면 오버엔지니어링. 미채택.
