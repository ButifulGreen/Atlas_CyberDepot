# 14. 남겨둔 의도적 미해결 사항

> Atlas_CyberDepot 아키텍처 설계안 v5 — §14. 어떤 시스템을 구현하든 착수 전에 이 문서를 훑어, 해당 시스템에 걸린 미해결/미채택 사항이 있는지 확인할 것.

- `ACostZoneVolume`을 NavQueryFilter 런타임 코스트 조정 방식으로 전환했지만, 그럼에도 구현 후 `stat navigation`으로 실측은 필요하다(코스트 조정 자체도 쿼리마다 계산 비용이 있으므로).
- 키오스크 전용 플레이어가 "주문 승인" 외에 NPC 디스패치 판단 같은 추가 권한을 가질지는 `02_Multiplayer_RPC.md` 확정 시 함께 정한다.
- 선반 포화 해소 수단 중 "폐기 처리"의 구체 설계(전용 구역/트레이 위치, 폐기 트리거 방식, 폐기된 물품의 후속 처리)는 미정이다.
- `UFactoryDashboardWidget`(관제실)의 인게임 접근 방식 — 특정 위치에 진입해야 여는 형태인지, 아무 위치에서나 단축키로 호출 가능한 형태인지 — 는 미정이다.
- `UCongestionHeatmapSubsystem`의 `UpdateIntervalSeconds`(예시 8초), `DecayRatePerUpdate`(예시 0.85)는 가안이며, 실제 플레이 규모(로봇 약 30대)에서 시각적으로 의미 있는 값인지 구현 후 튜닝이 필요하다.
- `AIdleWaitingZone`의 `RestDecayIntervalSeconds`(예시 10초)·`RestDecayAmountPerInterval`(예시 10)·`FullyRestedThresholdRatio`(예시 0.2)와 각 로봇의 `MaintenanceThreshold`는 상호 균형이 중요하다. 실제 로봇 운용 규모에서 플레이테스트 후 튜닝 필요.
- `HandoffStationAssignment`를 소프트 핸드오프로 바꿨지만, To가 스테이징 지점에서 대기하는 동안 제3의 아틀라스가 해당 스테이징 지점을 우연히 지나가는 경합은 발생하지 않는지(스테이징 지점은 점유 예약 대상이 아니므로) 구현 후 검증 필요.

## 검토 후 미채택 사항 (근거 포함)

- **예약 락 타임아웃/리스(lease) 패턴**: 로봇이 고장나도 슬롯/거점 예약을 유지한 채 수리 후 재개하는 것이 설계 원칙(`00_DesignPrinciples.md`)이고, FullRepair는 유한 시간 내 항상 완료되므로 영구 락(zombie lock) 위험이 구조적으로 없다. 타임아웃으로 강제 해제하면 오히려 "재개" 원칙이 깨진다. 미채택.
- **고장 위험 로봇을 시스템이 강제로 대기실까지 Navigate시키는 안**: `EvaluateRotationOrContinue` 기반 로테이션/핸드오프가 이동 직전·`TransferItem` 직전이라는 자연스러운 작업 경계에서만 판단하므로, 임무 도중 시스템이 로봇을 강제 인터럽트하는 것보다 우아하고 원칙에도 부합한다. 미채택.
- **배치 정비 중단 시 `Paused` 상태 별도 도입**: `RepairProgress`가 로봇 컴포넌트에 저장되고 `ActiveRepairers`가 0이 되어도 값은 보존되므로(진행 속도만 0), 중단 후 `BatchMaintenanceTargetSet`을 초기화하고 재트리거 시 새 스냅샷을 뜨는 현재 방식으로도 진행 상황 유실이 없다. 별도 상태머신을 추가하면 오버엔지니어링. 미채택.
