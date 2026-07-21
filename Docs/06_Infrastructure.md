# 6. 물류 인프라 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §6. 구현 4단계(인프라) 대상. 로봇 없이 예약/슬롯 로직만 독립적으로 검증 가능하다.

### `EItemType` (enum)
- `ItemA`, `ItemB`, `ItemC` (현재 3종 고정, 사이즈 동일·외형만 상이)

### `FItemTypeDefinition` (FTableRowBase, DataTable 행)
- `EItemType Type`
- `FText DisplayName`
- `TSoftObjectPtr<UStaticMesh> PreviewMesh`
- `TSubclassOf<AStorageShelf> BoundShelfClass`
- `int32 UnitPrice`(Docs 이탈, 승인됨 — `Docs/03_InventoryOrder.md` 금액 산정 시스템. 플레이어 입고 주문 1개당 비용)
- `int32 SellPrice`(Docs 이탈, 승인됨 — 구매가와 별도로 책정하는 마진. 외부업체 출고 주문 수락 시 1개당 수익)

### `ALogisticsItem` (AActor)
- 멤버: `FGuid ItemID`, `EItemType ItemType`, `FDateTime CreatedTimestamp`
- 함수: `float GetAgeSeconds() const`

### `AStorageShelf` (AActor) — 기차형 좌/우 분리 구조
3개 층(Floor) × 9칸(Slot) 고정. 각 슬롯은 좌측에서 입고(채움)만, 우측에서 출고(비움)만 가능한 독립된 칸으로, 슬롯 자체가 큐가 아니라 개별 예약 단위다. 어떤 슬롯이 가장 먼저 채워졌는지는 슬롯별 `EnteredTimestamp`로 판별한다. 고장나지 않으며 정비 대상이 아니다.

- 멤버
  - `EItemType BoundItemType`
  - `TArray<FShelfSlot> Slots` (3층 × 9칸 = 27개, FloorIndex·SlotIndex로 식별. 버그 수정 — 레벨에 배치하는 `UStorageSlotMarkerComponent`가 1부터 시작하는 값으로 세팅되어 있어(0이 아님), `FloorIndex`/`SlotIndex`는 외부 인터페이스 전체(마커, `TryReserveEmptySlot`/`TryReserveOldestOccupiedSlot`의 Out 파라미터, `FTransportTask`/`FPendingSlotReservation`에 저장되는 값 등)에서 1-based로 통일한다. 내부 `Slots` 배열 인덱스로 변환할 때만 `ToSlotArrayIndex`가 -1 보정)
  - `int32 MaxConcurrentAtlas = 4` (Docs 이탈, 승인됨 — Balance|WorkZone, `00_DesignPrinciples.md` 참고. 원래 "아틀라스 1대 거점 고정"이었으나 실측 튜닝을 위해 에디터 조정 가능한 동시 진입 수로 확장. 1로 두면 기존 동작과 동일)
  - `TArray<TWeakObjectPtr<AFactoryAgentBase>> InboundZoneOccupants` (좌측 구역, `MaxConcurrentAtlas`까지 동시 점유. `TArray<TWeakObjectPtr<T>>`는 UHT가 블루프린트 노출을 지원하지 않아 BlueprintReadOnly 미지정)
  - `TArray<TWeakObjectPtr<AFactoryAgentBase>> OutboundZoneOccupants` (우측 구역, 동일)
  - `TArray<TObjectPtr<AFactoryNavWaypoint>> TransportRobotInboundDocks` / `TransportRobotOutboundDocks` (Docs 이탈, 승인됨 — `08_Navigation.md` §8-B. 운송로봇 전용 도킹 웨이포인트, `SlotIndex`(1-based)로 인덱싱하며 `NumSlotsPerFloor`(9)개까지 배치)
  - `UInboundStagingMarkerComponent* InboundStagingMarker`, `UOutboundStagingMarkerComponent* OutboundStagingMarker` (설계 변경 — 배송로봇↔아틀라스 메인 사이클 핸드오프는 더 이상 이 지점을 쓰지 않는다. 슬롯별 (X,Y) 위치에서 직접 만나는 방식으로 바뀌었고, 이 마커는 이제 `07_TaskAssignment.md`의 소프트 핸드오프(교대/로테이션, 대기실 미배치로 현재 미사용)에서 대체 아틀라스가 대기하는 지점으로만 쓰인다. 버그 수정 — 원래 순수 `FTransform` 프로퍼티였는데 뷰포트에 기즈모가 안 보이고 기본값이 월드 원점이라 배치를 깜빡하기 쉬웠다. 슬롯 마커와 동일한 패턴의 씬 컴포넌트로 변경, `GetInboundStagingLocation()`/`GetOutboundStagingLocation()`으로 조회)
- 함수
  - `bool TryReserveInboundZone(AFactoryAtlasRobot* Atlas)` / `void ReleaseInboundZone(AFactoryAgentBase* Atlas)` (버그 수정 — 동시 다수 점유로 바뀌면서 "누가" 반납하는지가 필요해져 `Release*Zone`에 인자 추가)
  - `bool TryReserveOutboundZone(AFactoryAtlasRobot* Atlas)` / `void ReleaseOutboundZone(AFactoryAgentBase* Atlas)`
  - `bool IsZoneFull(EWorkZoneType ZoneType) const` (Docs 이탈, 승인됨 — `Occupants.Num() >= MaxConcurrentAtlas`. `UOutboundDispatchSubsystem::IsZoneOccupied`가 배정 시점 만석 판정에 재사용)
  - `AFactoryNavWaypoint* GetTransportRobotDock(int32 SlotIndex, EWorkZoneType ZoneType) const` (Docs 이탈, 승인됨 — `SlotIndex`(1-based)에 대응하는 도킹 웨이포인트 조회, 배선 안 됐으면 nullptr)
  - `bool TryReserveEmptySlot(int32& OutFloorIndex, int32& OutSlotIndex)` (입고 측, 품목 슬롯 단위로만 경합 방지. 물리적 도킹 충돌 방지는 위 `TransportRobotInboundDocks`가 별도로 담당)
  - `bool TryReserveOldestOccupiedSlot(int32& OutFloorIndex, int32& OutSlotIndex, ALogisticsItem*& OutItem)` (출고 측, 전체 슬롯 중 `EnteredTimestamp` 최솟값 탐색)
  - `void ConfirmInbound(int32 FloorIndex, int32 SlotIndex, ALogisticsItem* Item)`
  - `void ConfirmOutboundRemoved(int32 FloorIndex, int32 SlotIndex)`
  - `bool IsFull() const`
  - `int32 GetOccupiedCount() const`
  - `FVector GetAtlasWorkLocation(int32 FloorIndex, int32 SlotIndex, EWorkZoneType ZoneType) const` / `FVector GetTransportRobotWorkLocation(...)` (버그 수정 — 이동 목적지의 Z는 항상 선반 자신의 지상 높이로 고정한다. 아틀라스든 배송로봇이든 슬롯이 몇 층에 있든 항상 같은 지상 높이로 이동하고, 실제 층 높이까지 뻗는 건 IK(`CurrentIKHandTarget`, `GetSlotMarkerTransform`의 원본 Z 그대로 사용)뿐이다 — 배송로봇은 지상 이동체라 애초에 2층·3층 높이까지 갈 수 없다)
  - `void TransferZoneOccupancy(EWorkZoneType ZoneType, AFactoryAtlasRobot* From, AFactoryAtlasRobot* To)` (교대 핸드오프 시 `InboundZoneOccupants`/`OutboundZoneOccupants` 배열에서 From 항목을 To로 원자적 교체. 이 호출은 To가 스테이징 트랜스폼에 도착한 시점에만 발생한다)

### `FShelfSlot` (USTRUCT)
- `TWeakObjectPtr<ALogisticsItem> OccupyingItem`
- `FDateTime EnteredTimestamp`
- `bool bReservedForInbound`, `bool bReservedForOutbound` (예약~확정 사이의 임시 락)

### `AHorizontalTray` (AActor) — 입출고 공통 단일 클래스
물품을 항상 1개씩만 이동시키는 단순 상태 머신. 작업 구역은 아틀라스 1대 + 운송로봇 1대로 동시 접근이 제한된다. 입고 트레이는 주문 배정 시 `ItemStartMarker`에 물품을 텔레포트로 생성해 `ItemEndMarker`로 흘려보내고(Atlas가 End에서 픽업), 출고 트레이는 아틀라스가 `ItemEndMarker`에 물품을 올려 반대 방향인 `ItemStartMarker`로 흘려보낸다(운송로봇이 Start에서 픽업) — 두 방향이 서로 반대다(5단계 후속 정정, 최초 설계 문구의 "끝에서 텔레포트 제거"는 실제로는 로봇이 손 소켓으로 직접 부착해 옮겨가는 방식으로 대체됨). 고장나지 않으며 정비 대상이 아니다.

- 멤버
  - `ETrayDirection Direction` (Inbound / Outbound)
  - `EItemType BoundItemType` (5단계 후속 신규 — `AStorageShelf::BoundItemType`과 동일 패턴, 이 트레이가 담당하는 품목)
  - `TWeakObjectPtr<ALogisticsItem> CurrentItem`
  - `bool bIsHaltedAtEnd`
  - `TWeakObjectPtr<AFactoryAgentBase> WorkZoneOccupant` (아틀라스 전용 점유자 — `GetAtlasWorkLocation()` 접근 시 사용)
  - `TWeakObjectPtr<AFactoryAgentBase> TransportRobotWorkZoneOccupant` (버그 수정 신규 — `GetTransportRobotWorkLocation()`은 트레이당 좌표 하나뿐인데, 원래 이 지점엔 예약이 전혀 없었다. 선반 슬롯은 트립마다 다른 (FloorIndex,SlotIndex)로 자연히 분산되지만, 트레이는 모든 트립이 같은 좌표 하나로 몰려서, 물량이 많아 트립이 연달아 들어오면 두 번째 배송로봇이 첫 번째가 서있는 지점으로 그대로 이동을 시도해 길찾기가 영구 차단될 수 있었다. 실기 테스트로 발견)
  - 버그 수정(설계 변경) — 트레이 진입 시 경유하던 고정 도킹 웨이포인트(`TransportRobotInboundDock`)는 제거됐다. 슬롯/트레이마다 사람이 미리 지정해두는 방식 자체가 취약해, 이제 목표 마커 좌표에 가장 가깝고 실제로 도달 가능한 웨이포인트를 매번 동적으로 찾는다 — 상세는 `08_Navigation.md` §8-B 참고.
  - `float AtlasWorkDistance`, `float AtlasWorkLateralOffset`, `float TransportRobotWorkDistance`, `float TransportRobotWorkLateralOffset` (Balance — `ItemEndMarker` 기준 작업 위치 = 정면축(Forward) 오프셋(Distance) + 좌우축(Right) 오프셋(LateralOffset), 선반과 달리 좌우 부호 반전 없음. 버그 수정 — 원래 정면축 거리 하나뿐이라 아틀라스/배송로봇이 좌우로 벌어질 방법이 없어, 같은 축 선상에 너무 가깝게 위치하면 내비게이션 충돌 회피가 끼어들어 도착 판정이 간헐적으로 안 나는 문제가 있었다. 좌우 오프셋을 추가해 서로 자리를 벌릴 수 있게 함. 그 이전엔 별도의 `WorkMarker` 컴포넌트를 기준으로 계산했으나, `ItemEndMarker`와 물리적으로 동기화해야 하는 중복 컴포넌트라 레벨에서 둘을 다르게 배치하면 엉뚱한 위치로 이동하는 버그가 있어 `ItemEndMarker` 기준 직접 계산으로 변경하고 `WorkMarker`는 제거)
  - `USceneComponent* ItemStartMarker`, `USceneComponent* ItemEndMarker` (5단계 후속 신규 — 물품이 처음 텔레포트되는 지점과 컨베이어가 멈추는 지점은 서로 다른 지점이라 분리. 기존 `TrayLength`로 끝점을 역산하던 방식은 제거)
- 함수
  - `bool TryReserveWorkZone(AFactoryAgentBase* Agent)` / `void ReleaseWorkZone()` (아틀라스 전용)
  - `bool TryReserveTransportRobotWorkZone(AFactoryAgentBase* Agent)` / `void ReleaseTransportRobotWorkZone()` (버그 수정 신규 — `AFactoryTransportRobot::TryStartMoveToPoint`가 Tray 목적지로 이동하기 전에 호출. 실패하면 `TrayZoneRetryIntervalSeconds`(Balance, 기본 1초)마다 재시도하고, `OnItemGivenByAtlas`로 다음 목적지로 넘어가거나 `OnItemCollectedByAtlas`로 트립이 끝날 때 반납한다)
  - `void OnItemSpawnedAtStart(ALogisticsItem* Item)` (Inbound 전용)
  - `void OnItemPlacedByAtlas(ALogisticsItem* Item)` (Outbound 전용, `ItemEndMarker`로 명시적 스냅)
  - `void TickConveyance(float DeltaTime)`
  - `void OnItemReachedEnd()`
  - `void OnItemCleared()`
  - `FVector GetAtlasWorkLocation() const` / `FVector GetTransportRobotWorkLocation() const` (5단계 후속 신규)

### `ALogisticsItemSpawner` (AActor) — 5단계 후속 신규
"입고 트레이는 시작점에 물품을 보이지 않게 텔레포트로 생성한다"는 위 `AHorizontalTray` 설계를 실제로 구현하는 물품 풀. 레벨 시작 시 `EItemType` 3종 전부 `ItemsPerType`(밸런스 값, 기본 30)만큼 미리 스폰해 숨김+콜리전 꺼짐 상태로 대기시키고, 필요할 때 하나씩 꺼내 활성화(텔레포트 대상)한다.
- 멤버: `TSubclassOf<ALogisticsItem> ItemClass`, `int32 ItemsPerType`(Balance)
- 함수
  - `ALogisticsItem* TryAcquireItem(EItemType Type)` (대기 중인 해당 타입 물품 하나를 활성화해 반환, 없으면 nullptr)
  - `void ReturnItem(ALogisticsItem* Item)` (다시 숨기고 콜리전 끄고 대기 위치로 복귀)

### `ADockingPoint` (AActor)
- 멤버: `FGridIndex GridIndex`, `bool bOccupied`, `TWeakObjectPtr<AFactoryAgentBase> OccupyingAgent`
- 함수: `bool TryReserve(AFactoryAgentBase* Agent)`, `void Release()`

### `EZoneMaintenanceState` (enum)
- `Idle` (조건 모니터링 중), `Active` (NPC 출동·배치 정비 진행 중)

### `AIdleWaitingZone` (AActor)
아틀라스/운송로봇 전용 대기 공간. 업무 큐가 빈 로봇이 파킹하는 곳이며, 패시브 회복 감쇠(RestDecay)와 QuickCheck 정비가 가능한 유일한 장소. 대기 중인 로봇 전원이 위험 임계치(`IsMaintenanceDue() == true`) 상태일 때 NPC 배치 정비(Batch Maintenance)가 자동 발동된다. 공간 제약상 레벨에 여러 개 분산 배치할 수 있으며(선반이 품목별로 여러 대 배치되는 것과 동일한 구조), 각 인스턴스는 `AllowedAgentTypes`(비트마스크)로 어떤 타입을 받을지 정한다 — 버그 수정(사용자 지시) — 원래 단일값 `EAgentType`이라 대기실 하나가 Atlas 전용/TransportRobot 전용 둘 중 하나로만 고정됐다. 좁은 구간·로봇 수 확장에 대응해 한 대기실이 두 타입을 동시에(혹은 하나만) 받을 수 있도록 `AFactoryNavWaypoint::AllowedAgentTypes`와 동일한 `meta=(Bitmask, BitmaskEnum=EActorType)` 패턴으로 전환했다. `IsUsableBy(EActorType)`로 조회.

**슬롯 배정은 선입선출이 아니라 실행 시 1회 고정(Home) 방식이다.** 레벨 시작 시 `UOutboundDispatchSubsystem::AssignHomeIdleZoneSlots`가 로봇마다 대기실/슬롯을 한 번 정해 `AFactoryAgentBase::HomeIdleZone`/`HomeSlotIndex`에 기록하고, 그 뒤로는 대기실을 드나들 때마다 항상 같은 슬롯으로 돌아간다(경합·검색 없음). 대기실 마커 총합은 항상 레벨의 로봇 수 이상이라고 가정하며(사용자가 직접 배치), 로봇이 슬롯보다 많은 경우는 경고 로그만 남긴다. 버그 수정(사용자 지시) — 원래는 타입별로 로봇/대기실 풀을 완전히 나눠 이름순으로 앞에서부터 채웠다(물리적 위치 무관). `AllowedAgentTypes`가 비트마스크가 되며 타입별 분리 풀 자체가 부적절해진 김에, 배정 기준을 "레벨 시작 시점에 슬롯과 물리적으로 가장 가까운 로봇"으로 바꿨다 — `07_TaskAssignment.md`의 `AssignHomeIdleZoneSlots` 참고.

- 멤버
  - `int32 AllowedAgentTypes`(비트마스크, `meta=(Bitmask, BitmaskEnum=EActorType)`) — Atlas/TransportRobot 중 하나 또는 둘 다 허용. 프로퍼티 타입이 단일 enum에서 비트마스크로 바뀌어서 기존에 레벨에서 골라뒀던 값은 이어받지 못한다 — 이미 배치한 대기실은 이 변경 이후 다시 체크해야 한다.
  - ~~`TObjectPtr<AFactoryNavWaypoint> AccessWaypoint`~~ — 회피 재설계로 제거됨(`08_Navigation.md` §8-B). 대기실도 선반/트레이와 동일하게 `TryRequestWaypointRoute(nullptr, 마커좌표)`로 목표 마커 기준 최근접 도달가능 웨이포인트를 매번 동적으로 찾는다(진입/이탈 모두, 고정 단일 참조 없음).
  - `TArray<TObjectPtr<UParkingSlotMarkerComponent>> ParkingMarkers` (7단계 후속 신규 — 원래 `TArray<FTransform> ParkingSlots`였으나, 뷰포트에 기즈모가 안 보이고 기본값이 월드 원점이라 배치를 깜빡하기 쉬운 문제가 `AStorageShelf`의 스테이징 지점과 동일하게 있어 씬 컴포넌트 마커로 교체. `BeginPlay`에서 `GetComponents`로 캐싱)
  - `TMap<int32, TWeakObjectPtr<AFactoryAgentBase>> SlotOccupancy` (키는 배열 인덱스가 아니라 `UParkingSlotMarkerComponent::SlotIndex` — BP에서 마커를 어떤 순서로 추가·삭제해도 안전하게 매칭. "지금 이 슬롯에 누가 앉아있는가"만 추적하며, 슬롯의 소유권(Home) 자체는 각 로봇 쪽 `HomeIdleZone`/`HomeSlotIndex`가 별도로 보존한다)
  - `float RestDecayIntervalSeconds = 2.f` (UPROPERTY EditAnywhere; 패시브 회복 감쇠 주기. `FTimerManager` 기반 이벤트 콜백이며 엔진 Tick과 무관하다 — 매 프레임이 아니라 이 값마다 한 번씩만 실행되므로 성능에 영향 없음. 원래 기본값은 10초였으나 배치 정비 사이클 실기 테스트 편의를 위해 2초로 조정)
  - `int32 RestDecayAmountPerInterval = 20` (UPROPERTY EditAnywhere; 주기마다 삭감할 `OperationCount` 수치; 0 하한 클램프. 원래 기본값 10에서 위와 같은 이유로 20으로 조정)
  - `float FullyRestedThresholdRatio = 0.2f` (UPROPERTY EditAnywhere, 교대 후보로 인정하는 "완전 회복" 기준. `OperationCount <= MaintenanceThreshold * FullyRestedThresholdRatio`인 로봇만 `FindRestedOccupant()`가 반환한다. `IsMaintenanceDue() == false`를 그대로 기준으로 쓰면 임계치 바로 아래에서 교대 투입된 로봇이 한 번의 작업만으로 즉시 재위험 상태가 되는 스래싱이 발생하므로, 히스테리시스 구간을 둔다)
  - `int32 BatchMaintenanceOperationThreshold = 100` (UPROPERTY EditAnywhere, Balance|Maintenance — 7단계 후속 신규. `ShouldDispatchNPCForMaintenance`/`OnBatchMaintenanceProgress`가 쓰는 절대 `OperationCount` 기준. 개별 QuickCheck 적격 기준인 `IsMaintenanceDue()`(`MaintenanceThreshold`, 기본 20)와는 별개의 상위 문턱값 — "한 대라도 임계치 넘으면 개별 QuickCheck"와 "대기실 전원이 훨씬 심하게 쌓이면 배치 정비 파견"을 구분하기 위함)
  - `FTimerHandle RestDecayTimerHandle` (`FTimerManager`로 `RestDecayIntervalSeconds`마다 `OnRestDecayInterval` 호출)
  - `EZoneMaintenanceState MaintenanceState = EZoneMaintenanceState::Idle`
  - `TArray<TWeakObjectPtr<AFactoryAgentBase>> BatchMaintenanceTargetSet` (NPC 출동 시점에 `SlotOccupancy` 기준으로 고정; 이후 신규 입실 로봇은 포함되지 않음)
  - `TWeakObjectPtr<AFactoryNPCHuman> BatchMaintenanceNPC` (현재 배치 정비를 수행 중인 NPC)
- 함수
  - `bool GetHomeSlotTransform(int32 SlotIndex, FTransform& OutSlotTransform) const` (버그 수정 — 원래 이름 `TryOccupyHomeSlot`이던 함수를 순수 조회용으로 축소. 검색 없이 지정된 `SlotIndex`(호출부가 이미 아는 자신의 Home) 마커 위치만 반환하며 `SlotOccupancy`를 건드리지 않는다. `TryHeadToIdleZone`(대기실로 출발하는 시점)이 이동 목적지 계산에 쓴다)
  - `void MarkSlotOccupied(AFactoryAgentBase* Agent, int32 SlotIndex)` (버그 수정 — `TryOccupyHomeSlot`에서 실제 파킹 등록(`SlotOccupancy` 추가, `bIsParkedInIdleZone=true`, `AMSmartFactoryManager::OnAgentBecameIdle` 호출) 부분을 분리. 예전엔 출발 시점에 이 등록까지 같이 해버려서 대기실 도착 전(이동 중)부터 `RestDecay`/개별 QuickCheck 판정 대상이 되는 문제가 있었다. 이제 `TryHandleIdleZoneArrival`(실제 도착 시점)에서만 호출한다)
  - `void GetParkingSlotLocations(TArray<TPair<int32, FVector>>& OutSlots) const` (버그 수정(사용자 지시) — 원래 이름 `AssignHomeSlots(TArray<AFactoryAgentBase*>&)`는 이 대기실이 직접 로봇 목록 맨 앞부터 소비해 배정까지 했다. 이제 배정(최근접 매칭)은 `UOutboundDispatchSubsystem::AssignHomeIdleZoneSlots`가 모든 대기실을 아울러 한 번에 처리하므로, 이 함수는 순수 조회로 축소 — 자기 `ParkingMarkers`의 (`SlotIndex`, 월드 위치) 목록만 반환한다)
  - `void ReleaseSlot(AFactoryAgentBase* Agent)`
  - `bool IsAgentParked(const AFactoryAgentBase* Agent) const`
  - `AFactoryAgentBase* FindRestedOccupant() const` (`OperationCount <= MaintenanceThreshold * FullyRestedThresholdRatio`인 점유 로봇 반환; 교대 로테이션 판단용, 없으면 nullptr)
  - `void OnRestDecayInterval()` (FTimerManager 콜백; `SlotOccupancy`를 순회해 각 로봇의 `OperationCount`를 `RestDecayAmountPerInterval`만큼 감쇠·0 하한 클램프 → `BatchMaintenanceTargetSet` 내 해소 여부 갱신 → `ShouldDispatchNPCForMaintenance()` 평가)
  - `bool ShouldDispatchNPCForMaintenance() const` (`SlotOccupancy.Num() >= 1` && 전체 점유 로봇이 `GetOperationCount() >= BatchMaintenanceOperationThreshold` && `MaintenanceState == Idle`. 버그 수정 — 원래 `IsMaintenanceDue()`(`MaintenanceThreshold`)를 그대로 썼는데, 개별 QuickCheck 적격 기준과 배치 정비 파견 기준이 같아 구분이 안 됐음. 절대 `OperationCount` 기준의 별도 상위 임계치로 분리)
  - `void BeginBatchMaintenance(AFactoryNPCHuman* NPC)` (`BatchMaintenanceTargetSet`을 현재 `SlotOccupancy` 스냅샷으로 고정, `MaintenanceState = Active`, NPC에게 첫 타깃만 `AssignMaintenance(QuickCheck)` 호출 — 나머지 대상은 NPC가 순회 방문하지 않고 패시브 감쇠로 자연 해소되길 기다린다. 전원 해소되면 `EndBatchMaintenance`)
  - `void OnBatchTargetLeft(AFactoryAgentBase* Agent)` (타깃 셋 로봇이 대기실을 이탈할 때 호출; 감쇠 완료 여부 무관하게 `BatchMaintenanceTargetSet`에서 즉시 제거 후 완료 조건 재평가)
  - `void OnBatchMaintenanceProgress()` (각 로봇의 QuickCheck 완료 또는 패시브 감쇠로 `GetOperationCount() < BatchMaintenanceOperationThreshold` 전환 시 호출 — 진입 조건과 동일한 절대 기준을 써서 히스테리시스 없이 대칭적으로 판정한다; `BatchMaintenanceTargetSet` 내 잔여 미완료 로봇이 없으면 `EndBatchMaintenance` 호출)
  - `void EndBatchMaintenance()` (NPC 복귀 지시, `BatchMaintenanceTargetSet` 초기화, `MaintenanceState = Idle`)
  - `void OnBatchMaintenanceInterrupted()` (NPC가 FullRepair 긴급 배정으로 임무를 중단할 때 호출; `BatchMaintenanceTargetSet` 초기화, `MaintenanceState = Idle`; 이후 `AMSmartFactoryManager::OnRepairCompleted` 경유 조건 재평가로 재발동 가능. 개별 로봇의 `RepairProgress`는 컴포넌트에 보존되므로 진행 상황 유실은 없다 — 별도의 `Paused` 상태는 두지 않는다. `14_OpenIssues.md` 참고)

> **배치 정비 NPC 선발 기준**: `AMSmartFactoryManager::FindNearestAvailableNPC`가 반환하는 NPC 중 현재 FullRepair 임무 미진행 중인 1명을 선발한다. 가용 NPC가 없으면 즉시 발동하지 않으며, `AMSmartFactoryManager::OnRepairCompleted` 이후 `ShouldDispatchNPCForMaintenance()`를 재평가하여 조건이 여전히 충족되면 발동한다. 필드워커 플레이어 2명이 모두 NPC에 빙의한 경우, 빙의되지 않은 나머지 NPC(FullRepair 미진행)가 대기실로 향하며, 내부 긴급 수리는 플레이어들이 빙의 NPC를 통해 직접 대응한다.
