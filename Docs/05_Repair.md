# 5. 정비 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §5. 구현 7단계(정비/고장) 대상.

### `ERepairType` (enum)
- `QuickCheck` (대기 공간에서만, 짧은 소요시간; 패시브 RestDecay를 NPC/플레이어가 가속하는 옵션으로 병용), `FullRepair` (Broken 상태, 위치 무관 즉시 수행)

> **NPC 배정 원칙**: 고장 로봇 1대당 AI NPC 1명만 배정한다. 고장 로봇 수보다 가용 NPC가 많아도 AI NPC끼리 같은 로봇을 협력 정비하지 않는다. 단, 플레이어가 NPC에 빙의(`Server_JoinRepair`)하여 AI NPC가 이미 정비 중인 로봇에 합류하는 것은 가능하다(`URepairProgressComponent::ActiveRepairers`에 빙의 NPC와 AI NPC가 동시에 등록될 수 있음). NPC 인원은 3명 고정.

### `URepairProgressComponent` (UActorComponent, Atlas/TransportRobot에 부착)
- 멤버
  - `ERepairType CurrentRepairType`
  - `float RepairProgress` (Replicated)
  - `float QuickCheckDurationSeconds`
  - `float FullRepairDurationSeconds`
  - `TArray<TWeakObjectPtr<AFactoryAgentBase>> ActiveRepairers` (플레이어 빙의 NPC + AI NPC 동일하게 취급; 유효한 참조만 카운트)
- 함수
  - `void Server_JoinRepair(AFactoryAgentBase* Repairer)`
  - `void Server_LeaveRepair(AFactoryAgentBase* Repairer)`
  - `void TickRepairProgress(float DeltaTime)` (진행 속도 = `BaseRepairRate * 유효 ActiveRepairers.Num()`; `ActiveRepairers`가 0명이 되어도 `RepairProgress`는 그대로 보존되고 진행 속도만 0이 된다 — NPC가 배치 정비 도중 다른 임무로 이탈해도 진행 상황은 유실되지 않음)
  - `void OnRepairCompleted()` (`OperationCount` 리셋, Broken/bMaintenanceDue 상태 해제, `FAnomalyEvent` 발행, `AMSmartFactoryManager::OnRepairCompleted` 호출. 버그 수정 — 정비자 목록을 통보 없이 비우면 AI NPC가 `UnderRepair`에 영구히 멈춰 `FindNearestAvailableNPC` 후보에서 계속 빠지는 문제가 있어, 목록을 비우기 전 AI 제어 중인 NPC 정비자에게 `AFactoryNPCHuman::ReturnToOfficeRoom()`을 자동 호출하도록 함. 단, `AMSmartFactoryManager::TryAssignNextPendingMaintenance`로 대기열에 다른 정비 필요 로봇이 있는지 먼저 확인해 있으면 그쪽에 재배정하고, 없을 때만 사무실로 복귀시킨다. 빙의 중인 플레이어는 AI 재배차 대상에서 제외하고 `AFactoryNPCHuman::OnJoinedRepairCompleted()`로 참여 상태만 정리한다 — 8단계)

### 동시 다발 고장 대응(버그 수정)
- **정비 요청 재시도 큐**: `AMSmartFactoryManager::RequestMaintenance`가 가용 NPC를 못 찾으면(전원 `UnderRepair`) 조용히 포기하는 대신 `PendingMaintenanceQueue`(private, `TArray<TWeakObjectPtr<AFactoryAgentBase>>`)에 등록한다. 이 큐는 `Broken`(FullRepair) 요청과 QuickCheck 요청(파킹 중 `Idle` 상태로 임계치 초과, `OnAgentBecameIdle` 경유) 둘 다 받는다. `TryAssignNextPendingMaintenance(AFactoryNPCHuman* NPC)`가 큐 선두부터 훑어 여전히 정비가 필요한(`Broken` 이거나 `IsMaintenanceDue()`) 항목을 찾으면, 그 시점 상태로 `ERepairType`을 동적으로 정해(`Broken`→`FullRepair`, 아니면→`QuickCheck`) NPC를 배정한다(더 이상 유효하지 않은 항목은 폐기). `URepairProgressComponent::OnRepairCompleted()`가 정비를 마친 NPC를 사무실로 돌려보내기 전에 이 함수부터 호출한다. 버그 수정 — 최초 구현은 `CurrentState == Broken`만 통과시켜서 QuickCheck로 큐에 들어온 항목을 전부 조용히 버렸다(그 로봇들이 영영 정비를 못 받음). 실기 테스트로 발견해 "여전히 정비가 필요한가" 기준으로 수정.
- **고장 복구 후 유휴 재배차**: `AFactoryAtlasRobot`/`AFactoryTransportRobot`이 `ResumeAfterRepair()`를 override해, 고장 직전 진행 중이던 배정(`CurrentAssignment`/`CurrentTask`)이 없었을 때만(디버그 강제 고장 등) `UOutboundDispatchSubsystem::TryDispatchIdleAgents()`를 재호출한다. 배정이 남아있으면(자연 발생 고장은 항상 Working 도중 롤링되므로 배정이 유효함) 건드리지 않는다 — 그 경우의 "이어서 재개"는 Working 재진입이 필요한 별개 문제라 아직 미해결(`Docs/14_OpenIssues.md` 참고). 이 훅이 없으면 대기 중이던 트립/배정이 있어도 복구된 로봇이 유휴 상태로 방치되는 문제가 있었음.
