# 6. 물류 인프라 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §6. 구현 4단계(인프라) 대상. 로봇 없이 예약/슬롯 로직만 독립적으로 검증 가능하다.

### `EItemType` (enum)
- `ItemA`, `ItemB`, `ItemC` (현재 3종 고정, 사이즈 동일·외형만 상이)

### `FItemTypeDefinition` (FTableRowBase, DataTable 행)
- `EItemType Type`
- `FText DisplayName`
- `TSoftObjectPtr<UStaticMesh> PreviewMesh`
- `TSubclassOf<AStorageShelf> BoundShelfClass`

### `ALogisticsItem` (AActor)
- 멤버: `FGuid ItemID`, `EItemType ItemType`, `FDateTime CreatedTimestamp`
- 함수: `float GetAgeSeconds() const`

### `AStorageShelf` (AActor) — 기차형 좌/우 분리 구조
3개 층(Floor) × 9칸(Slot) 고정. 각 슬롯은 좌측에서 입고(채움)만, 우측에서 출고(비움)만 가능한 독립된 칸으로, 슬롯 자체가 큐가 아니라 개별 예약 단위다. 어떤 슬롯이 가장 먼저 채워졌는지는 슬롯별 `EnteredTimestamp`로 판별한다. 고장나지 않으며 정비 대상이 아니다.

- 멤버
  - `EItemType BoundItemType`
  - `TArray<FShelfSlot> Slots` (3층 × 9칸 = 27개, FloorIndex·SlotIndex로 식별)
  - `TWeakObjectPtr<AFactoryAgentBase> InboundZoneOccupant` (좌측 구역, 아틀라스 1대 거점 제한)
  - `TWeakObjectPtr<AFactoryAgentBase> OutboundZoneOccupant` (우측 구역, 아틀라스 1대 거점 제한)
  - `FTransform InboundStagingTransform`, `FTransform OutboundStagingTransform` (교대 핸드오프 시 대체 아틀라스가 실제 점유를 넘겨받기 전 대기하는 근접 지점 — `07_TaskAssignment.md`의 소프트 핸드오프 참고)
- 함수
  - `bool TryReserveInboundZone(AFactoryAtlasRobot* Atlas)` / `void ReleaseInboundZone()`
  - `bool TryReserveOutboundZone(AFactoryAtlasRobot* Atlas)` / `void ReleaseOutboundZone()`
  - `bool TryReserveEmptySlot(int32& OutFloorIndex, int32& OutSlotIndex)` (입고 측, 운송로봇 수 제한 없이 슬롯 단위로만 경합 방지)
  - `bool TryReserveOldestOccupiedSlot(int32& OutFloorIndex, int32& OutSlotIndex, ALogisticsItem*& OutItem)` (출고 측, 전체 슬롯 중 `EnteredTimestamp` 최솟값 탐색)
  - `void ConfirmInbound(int32 FloorIndex, int32 SlotIndex, ALogisticsItem* Item)`
  - `void ConfirmOutboundRemoved(int32 FloorIndex, int32 SlotIndex)`
  - `bool IsFull() const`
  - `int32 GetOccupiedCount() const`
  - `void TransferZoneOccupancy(EWorkZoneType ZoneType, AFactoryAtlasRobot* From, AFactoryAtlasRobot* To)` (교대 핸드오프 시 `InboundZoneOccupant` 또는 `OutboundZoneOccupant`를 From→To로 원자적 교체. Release+Reserve를 단일 호출로 처리해 제3 아틀라스의 선점 방지. 이 호출은 To가 스테이징 트랜스폼에 도착한 시점에만 발생한다)

### `FShelfSlot` (USTRUCT)
- `TWeakObjectPtr<ALogisticsItem> OccupyingItem`
- `FDateTime EnteredTimestamp`
- `bool bReservedForInbound`, `bool bReservedForOutbound` (예약~확정 사이의 임시 락)

### `AHorizontalTray` (AActor) — 입출고 공통 단일 클래스
물품을 항상 1개씩만 이동시키는 단순 상태 머신. 작업 구역은 아틀라스 1대 + 운송로봇 1대로 동시 접근이 제한된다. 입고 트레이는 주문 배정 시 시작점에 물품을 보이지 않게 텔레포트로 생성하고, 출고 트레이는 아틀라스가 직접 물품을 올린다. 양쪽 모두 끝에 도달한 물품은 보이지 않게 텔레포트로 제거된다. 고장나지 않으며 정비 대상이 아니다.

- 멤버
  - `ETrayDirection Direction` (Inbound / Outbound)
  - `TWeakObjectPtr<ALogisticsItem> CurrentItem`
  - `bool bIsHaltedAtEnd`
  - `TWeakObjectPtr<AFactoryAgentBase> WorkZoneOccupant` (아틀라스+운송로봇 각 1대 제한 구역 점유자)
- 함수
  - `bool TryReserveWorkZone(AFactoryAgentBase* Agent)` / `void ReleaseWorkZone()`
  - `void OnItemSpawnedAtStart(ALogisticsItem* Item)` (Inbound 전용)
  - `void OnItemPlacedByAtlas(ALogisticsItem* Item)` (Outbound 전용)
  - `void TickConveyance(float DeltaTime)`
  - `void OnItemReachedEnd()`
  - `void OnItemCleared()`

### `ADockingPoint` (AActor)
- 멤버: `FGridIndex GridIndex`, `bool bOccupied`, `TWeakObjectPtr<AFactoryAgentBase> OccupyingAgent`
- 함수: `bool TryReserve(AFactoryAgentBase* Agent)`, `void Release()`

### `EZoneMaintenanceState` (enum)
- `Idle` (조건 모니터링 중), `Active` (NPC 출동·배치 정비 진행 중)

### `AIdleWaitingZone` (AActor)
아틀라스/운송로봇 전용 대기 공간. 업무 큐가 빈 로봇이 파킹하는 곳이며, 패시브 회복 감쇠(RestDecay)와 QuickCheck 정비가 가능한 유일한 장소. 대기 중인 로봇 전원이 위험 임계치(`IsMaintenanceDue() == true`) 상태일 때 NPC 배치 정비(Batch Maintenance)가 자동 발동된다.

- 멤버
  - `EAgentType AllowedAgentType` (Atlas 또는 TransportRobot, 두 타입은 서로 다른 존 사용)
  - `TArray<FTransform> ParkingSlots`
  - `TMap<int32, TWeakObjectPtr<AFactoryAgentBase>> SlotOccupancy`
  - `float RestDecayIntervalSeconds = 10.f` (UPROPERTY EditAnywhere; 패시브 회복 감쇠 주기. `FTimerManager` 기반 이벤트 콜백이며 엔진 Tick과 무관하다 — 매 프레임이 아니라 이 값(기본 10초)마다 한 번씩만 실행되므로 5~10초 단위로 널널하게 잡아도 성능에 영향 없음)
  - `int32 RestDecayAmountPerInterval = 10` (UPROPERTY EditAnywhere; 주기마다 삭감할 `OperationCount` 수치; 0 하한 클램프)
  - `float FullyRestedThresholdRatio = 0.2f` (UPROPERTY EditAnywhere, 교대 후보로 인정하는 "완전 회복" 기준. `OperationCount <= MaintenanceThreshold * FullyRestedThresholdRatio`인 로봇만 `FindRestedOccupant()`가 반환한다. `IsMaintenanceDue() == false`를 그대로 기준으로 쓰면 임계치 바로 아래에서 교대 투입된 로봇이 한 번의 작업만으로 즉시 재위험 상태가 되는 스래싱이 발생하므로, 히스테리시스 구간을 둔다)
  - `FTimerHandle RestDecayTimerHandle` (`FTimerManager`로 `RestDecayIntervalSeconds`마다 `OnRestDecayInterval` 호출)
  - `EZoneMaintenanceState MaintenanceState = EZoneMaintenanceState::Idle`
  - `TArray<TWeakObjectPtr<AFactoryAgentBase>> BatchMaintenanceTargetSet` (NPC 출동 시점에 `SlotOccupancy` 기준으로 고정; 이후 신규 입실 로봇은 포함되지 않음)
  - `TWeakObjectPtr<AFactoryNPCHuman> BatchMaintenanceNPC` (현재 배치 정비를 수행 중인 NPC)
- 함수
  - `bool TryReserveSlot(AFactoryAgentBase* Agent, FTransform& OutSlotTransform)`
  - `void ReleaseSlot(AFactoryAgentBase* Agent)`
  - `bool IsAgentParked(const AFactoryAgentBase* Agent) const`
  - `AFactoryAgentBase* FindRestedOccupant() const` (`OperationCount <= MaintenanceThreshold * FullyRestedThresholdRatio`인 점유 로봇 반환; 교대 로테이션 판단용, 없으면 nullptr)
  - `void OnRestDecayInterval()` (FTimerManager 콜백; `SlotOccupancy`를 순회해 각 로봇의 `OperationCount`를 `RestDecayAmountPerInterval`만큼 감쇠·0 하한 클램프 → `BatchMaintenanceTargetSet` 내 해소 여부 갱신 → `ShouldDispatchNPCForMaintenance()` 평가)
  - `bool ShouldDispatchNPCForMaintenance() const` (`SlotOccupancy.Num() >= 1` && 전체 점유 로봇이 `IsMaintenanceDue() == true` && `MaintenanceState == Idle`)
  - `void BeginBatchMaintenance(AFactoryNPCHuman* NPC)` (`BatchMaintenanceTargetSet`을 현재 `SlotOccupancy` 스냅샷으로 고정, `MaintenanceState = Active`, NPC에게 첫 타깃 `AssignMaintenance(QuickCheck)` 호출)
  - `void OnBatchTargetLeft(AFactoryAgentBase* Agent)` (타깃 셋 로봇이 대기실을 이탈할 때 호출; 감쇠 완료 여부 무관하게 `BatchMaintenanceTargetSet`에서 즉시 제거 후 완료 조건 재평가)
  - `void OnBatchMaintenanceProgress()` (각 로봇의 QuickCheck 완료 또는 패시브 감쇠로 `IsMaintenanceDue() == false` 전환 시 호출; `BatchMaintenanceTargetSet` 내 잔여 미완료 로봇이 없으면 `EndBatchMaintenance` 호출)
  - `void EndBatchMaintenance()` (NPC 복귀 지시, `BatchMaintenanceTargetSet` 초기화, `MaintenanceState = Idle`)
  - `void OnBatchMaintenanceInterrupted()` (NPC가 FullRepair 긴급 배정으로 임무를 중단할 때 호출; `BatchMaintenanceTargetSet` 초기화, `MaintenanceState = Idle`; 이후 `AMSmartFactoryManager::OnRepairCompleted` 경유 조건 재평가로 재발동 가능. 개별 로봇의 `RepairProgress`는 컴포넌트에 보존되므로 진행 상황 유실은 없다 — 별도의 `Paused` 상태는 두지 않는다. `14_OpenIssues.md` 참고)

> **배치 정비 NPC 선발 기준**: `AMSmartFactoryManager::FindNearestAvailableNPC`가 반환하는 NPC 중 현재 FullRepair 임무 미진행 중인 1명을 선발한다. 가용 NPC가 없으면 즉시 발동하지 않으며, `AMSmartFactoryManager::OnRepairCompleted` 이후 `ShouldDispatchNPCForMaintenance()`를 재평가하여 조건이 여전히 충족되면 발동한다. 필드워커 플레이어 2명이 모두 NPC에 빙의한 경우, 빙의되지 않은 나머지 NPC(FullRepair 미진행)가 대기실로 향하며, 내부 긴급 수리는 플레이어들이 빙의 NPC를 통해 직접 대응한다.
