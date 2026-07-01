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
  - `void OnRepairCompleted()` (`OperationCount` 리셋, Broken/bMaintenanceDue 상태 해제, `FAnomalyEvent` 발행, `AMSmartFactoryManager::OnRepairCompleted` 호출)
