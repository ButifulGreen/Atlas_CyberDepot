# 8. 내비게이션 / 코스트 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §8. 구현 3단계 대상. `AIModule`, `NavigationSystem` 모듈이 Build.cs Private Dependency에 필요하다(`CLAUDE.md` 참고).

## 스코프 분리 (Docs 이탈, 승인됨 — `00_DesignPrinciples.md` 참고)

극한 부하 테스트 중 NavMesh 코스트 기반 자유 이동이 진입각도에 따라 트레이 콜리전에 걸리는 버그가 발견됐고, 실제 대형 자동화 물류창고(아마존 로보틱스, 중국 Geek+/Quicktron류)가 자유 회피가 아니라 웨이포인트 그래프+예약 기반으로 동작한다는 점, 그리고 이 프로젝트의 AI 학습 데이터 생성 목적에 이 방식이 더 부합한다는 점을 근거로 이동 레이어를 둘로 나눴다.

- **NPC(정비 인력)**: 아래 §8-A(NavArea/QueryFilter, 자유 이동 + Detour Crowd 회피)를 그대로 사용. 고장 로봇이 어디서 발생할지 예측 불가능해 자유 이동이 맞다.
- **아틀라스/운송로봇**: 거점 간 이동(선반/트레이/대기실 오가기)은 §8-B(웨이포인트 그래프 + 예약)로 전환. 작업구역 진입(그래프를 벗어난 마지막 접근, FinalHop) 이후에도 물리 이동 자체는 여전히 자유 Nav 제어(§8-A, 엔진 Detour Crowd)를 쓰지만, 좁은 구간에서 RVO가 로컬 미니멈(제자리 흔들림)에 빠지는 문제가 실제로 재현되어(회피 재설계, 아래 "FinalHop 전용 면 트레이스" 참고) §8-B 쪽에 전용 안전망을 추가했다 — RVO는 여유 공간에서의 매끄러운 스무딩 역할로 축소되고, 조밀한 구간의 정지/재개 판단은 이제 §8-B가 담당한다.

## §8-A. NavMesh 코스트 레이어 (NPC 전용 + 아틀라스 작업구역 내부)

### `UNavArea_MainLane` / `UNavArea_SideSpace` / `UNavArea_Critical` (UNavArea 상속)
- `DefaultCost` 오버라이드만 다르게 설정 (1 / 10 / 100). 정적 페인팅 값 그대로 쓰인다 — 런타임 코스트 조정은 하지 않는다.

### `UNavQueryFilter_Robot` / `UNavQueryFilter_NPC` (UNavigationQueryFilter 상속)
- 로봇: MainLane 선호 / SideSpace 기피. NPC: 전 구역 동일 코스트.

> **삭제됨(재검토 후 확정)**: 런타임 혼잡도에 따라 이 NavQueryFilter들의 코스트를 동적으로 올려주던 `ACostZoneVolume`(AActor, 미리 배치되는 풀링 대상)/`AFactoryAIController::ApplyDynamicCongestionCost`를 제거했다. 유일한 등록 주체(`BlockerCount` 증가)였던 `AFactoryTransportRobot::OnBlockedTick`/`OnUnblocked`도 함께 제거 — 이 로봇이 실제로 멈추는 상황은 이미 §8-B의 웨이포인트 예약/`Pause`/재탐색 메커니즘이 처리한다. NPC는 3명뿐이라 애초에 동시다발 congestion 시나리오가 드물었고, 로봇-NPC 간 충돌 회피는 엔진 RVO와 `AFactoryAgentBase`의 안전거리 감지(`RunGraphSegmentTraceCheck`/`RunFinalHopAreaTraceCheck`, `Cast<AFactoryAgentBase>` 기반이라 `AFactoryNPCHuman`도 대상에 포함)가 이미 이중으로 처리하고 있어 삭제로 인한 손실이 없다고 판단했다(`Docs/14_OpenIssues.md` 참고).

## §8-B. 웨이포인트 그래프 + 예약 (아틀라스/운송로봇 거점 간 이동)

### `AFactoryNavWaypoint` (AActor)
그래프 노드 1개. 특정 인프라(선반/트레이)를 모른다. 대기실도 포함해 모든 목적지가 "이 마커에 가장 가깝고 실제로 도달 가능한 웨이포인트"를 매번 동적으로 찾는 동일한 방식으로 통일됐다 — 아래 "목적지별 통합 방식" 참고.

- `int32 AccessFlags`(비트마스크, `meta=(Bitmask, BitmaskEnum=EWaypointAccess)`) — `Common`(검은, 전 타입 공용 백본) / `Inbound`(파랑) / `Outbound`(빨강) / `Waitbound`(노랑) 중 여러 개를 동시에 켤 수 있다. 버그 수정(사용자 지시) — 원래 단일값 `EWaypointAccess Access`였는데, 하나의 노드가 여러 역할을 겸해야 하는 경우(대표적으로 Inbound+Outbound 동시 지정)를 지원하려고 `AllowedAgentTypes`와 동일한 비트마스크 패턴으로 전환하며 이름도 `AccessFlags`로 바꿨다 — **프로퍼티 타입이 바뀌어서 기존에 레벨에서 체크해뒀던 Access 값은 이어받지 못한다. 이미 배치한 웨이포인트는 이 변경 이후 AccessFlags를 다시 체크해야 한다.** `HasAccessFlag(EWaypointAccess)`로 개별 플래그를, `IsPureCommon()`으로 "Inbound/Outbound/Waitbound 중 아무것도 안 겸함"을 조회한다. `GetDirectionalTier(bool bWantOutbound)`는 목적(출발지 탐색=true/도착지 탐색=false)에 맞는 방향을 0순위, 그 외 전용 레인을 1순위, 순수 Common을 2순위로 매긴다(경로 탐색 정렬에 사용 — 아래 참고). Inbound는 그래프→마커 방향(마커 진입 직전의 "탈출 지점" — 나가는 연결이 없는 게 정상), Outbound는 마커→그래프 방향(복귀 진입점 — 백본으로 돌아나가는 연결이 있어야 정상)이다 — 이 방향성 자체는 `ConnectedWaypoints` 배선으로만 결정되고 코드가 Inbound/Outbound를 구분해서 처리하는 지점은 없으므로, 한 노드에 둘 다 켜도 로직이 꼬이지 않는다(레벨 배선 책임만 남는다). `Waitbound`는 Inbound와 마커 사이에 두는 게이트 노드 — 아래 "Waitbound 게이팅" 참고.
- `int32 AllowedAgentTypes`(비트마스크, `meta=(Bitmask, BitmaskEnum=EActorType)`) — 버그 수정(회피 재설계) — 어떤 에이전트가 이 웨이포인트를 쓸 수 있는지를 `Access`에서 분리했다. 아틀라스/배송로봇/NPC 중 하나만, 일부만, 전부를 자유롭게 조합해 허용할 수 있다(NPC는 이번 단계에서 실제로 이 그래프를 타지는 않지만 — 아래 참고 — 미래를 위해 타입 자체는 포함해둠). 기본값은 전부 허용(기존 `Common`의 동작과 동일) — 기존에 배치된 Inbound/Outbound 웨이포인트가 있었다면(예전엔 배송로봇 전용으로 하드코딩) 이 값이 새로 생기며 기본값(전부 허용)을 물려받으므로, 배송로봇 전용으로 유지하고 싶은 지점은 에디터에서 직접 체크를 해제해야 한다.
- `TArray<AFactoryNavWaypoint*> ConnectedWaypoints` — 방향성은 이 배열 자체가 편도/양방향인지로 표현한다(레인 노드는 다음 노드만 편도 연결, 백본은 서로 왕복 연결). 레벨에서 직접 배선(자동 탐지 안 함) — 통로의 코너/교차로/분기점마다 반드시 노드를 둬야 한다(건너뛰면 그 구간이 대각선으로 잘림).
- `TryReserve(AFactoryAgentBase*)` / `Release(AFactoryAgentBase*)` / `IsOccupied()` — 단일 점유(`HorizontalTray::WorkZoneOccupant`와 동일 패턴). 도킹/레인 노드는 관문일 뿐 정지 지점이 아니므로, 에이전트는 그 노드를 떠나는 즉시 반납한다.
- `IsUsableBy(EActorType)` — `AllowedAgentTypes` 비트마스크 조회로 단순화(`AccessFlags` 값과 무관하게 동일한 로직).
- 디버그: `DrawDebugVisualization()`이 `DebugDrawIntervalSeconds`마다 플래그 우선순위(Waitbound>Inbound+Outbound 동시>단일)에 따른 색(노/보라/파/빨, 점유 중이면 초록)으로 스피어+연결 화살표를 그린다.

### `UFactoryWaypointNavigationSubsystem` (UWorldSubsystem)
상태 없는 탐색 전용 유틸리티(그래프를 중앙에서 관리하는 매니저가 아님 — 예약 상태는 각 웨이포인트 자신이 들고 있다).

- `FindNearestWaypoint(FVector Location, EActorType AgentType)` — 레벨의 웨이포인트 중 해당 타입이 쓸 수 있는 것 중 최단 거리(단순 최근접 조회 — 현재 호출부 없음, 디버그/향후 용도로 보존).
- `FindPath(AFactoryNavWaypoint* Start, AFactoryNavWaypoint* Target, AFactoryAgentBase* RequestingAgent, TArray<AFactoryNavWaypoint*>& OutRoute, bool bIgnoreOccupancy = false)` — 점유 중(요청자 자신이 점유한 노드는 예외)이거나 타입이 못 쓰는 노드를 후보에서 제외한 다익스트라. 경로 없으면 false. `bIgnoreOccupancy=true`면 점유 체크를 건너뛴다 — "지금 당장 갈 수 있는지"가 아니라 "점유만 없었다면 위상적으로 도달 가능한지"를 묻는 순수 구조 확인 용도(아래 두 함수가 "점유 경합 vs 진짜 막다른 길"을 구분하는 데 사용).
- `FindPathFromNearestReachable(FVector Location, AFactoryNavWaypoint* Target, AFactoryAgentBase* RequestingAgent, TArray<AFactoryNavWaypoint*>& OutRoute)` — **출발점** 쪽 견고성. 최근접 웨이포인트 단 하나만 시작점으로 시도하면, 마커 진입 전용(나가는 연결이 없는 "탈출 지점") 노드가 우연히 가장 가까울 때 경로 탐색이 영구 실패한다. 해당 타입이 쓸 수 있는 후보를 정렬해, `FindPath`가 실제로 성공하는 시작점을 찾을 때까지 순서대로 시도한다. 버그 수정(사용자 지시) — 정렬은 단순 거리순이 아니라 `GetDirectionalTier(true)`(항상 **출발지 탐색** — Outbound를 0순위) 기준이다. 레벨 배치상 우연히 Common 백본 노드가 전용 레인보다 마커(현재 위치)에 더 가까우면 공용 백본에서 마커로 바로 붙는 결과가 나올 수 있어, 전용 레인이 하나라도 있고 도달 가능하면 반드시 그쪽을 먼저 쓰도록 강제한다. Common은 전용 레인이 하나도 없거나 전부 도달 불가할 때만 폴백으로 쓰인다.
  - **버그 수정(사용자 지시, 확정 재현)** — 원래는 `IsPureCommon()` 기반 2단계 정렬(Common 여부만 구분)이라 Inbound/Outbound를 동일하게 취급했다. 이 함수는 언제나 "지금 있는 자리에서 그래프로 이탈"하는 목적인데, 근처에 도착용 Inbound가 출발용 Outbound보다 가까우면 Inbound 쪽을 시작점으로 잘못 골랐다(배송로봇이 마커에서 이탈할 때 가까운 Outbound 대신 Inbound/Common을 타는 증상으로 재현, 간헐적이 아니라 확정 발생). `GetDirectionalTier(true)`로 Outbound를 0순위로 강제해 해결.
  - **버그 수정(사용자 리포트)** — 후보 목록에 `Target` 자신이 포함돼 있으면, 앞선 후보가 전부 실패했을 때(Inbound류 막다른 노드가 많을수록 흔함) 루프가 결국 `Target` 자신을 시작점으로 시도해 `FindPath`의 `Start==Target` 트리비얼 케이스가 발동했다 — `Location`이 `Target`과 전혀 무관하게 멀리 떨어져 있어도 "이미 도착"으로 오판해 그래프 전체를 건너뛰고 FinalHop으로 직행하는 버그였다(배송로봇 대기실→트레이 이동에서 거의 100% 재현 보고됨, Waitbound를 대량 추가하며 막다른 노드 비중이 늘어 노출됨). 이제 `FindNearestWaypoint(Location, AgentType) == Target`(실제로 `Location`에 가장 가까운 사용 가능 노드가 `Target` 자신)일 때만 1노드 경로를 허용하고, 그 외에는 `Target`을 후보 목록에서 아예 제외한 뒤 나머지 후보로만 그래프 탐색을 진행한다 — 전부 실패하면 가짜 "이미 도착" 대신 정직하게 실패를 반환해 `WaypointRetryIntervalSeconds` 재시도로 넘어간다.
  - **버그 수정(사용자 리포트, 2차 — 도입 후 폐기)** — 위 수정 이후에도, 가장 가까운 전용 레인이 그 순간 막혀 있으면 폴백 루프가 레벨 반대편의 아무 전용 레인이나(아무리 멀어도) 채택하는 문제가 있어(배송로봇 트레이→선반 이동에서 재현), 한때 "시작점 후보가 Location 기준 일정 거리보다 멀면 후보에서 제외"하는 거리 가드(`MaxFallbackDistanceMultiplier`/`MinFallbackBaselineDistance`)를 넣었었다. 그런데 정상적인 후보(대기실 옆에서 백본까지의 첫 홉 등)도 실제 그래프 간격상 그 임계값보다 먼 경우가 흔해서, 정상 후보가 전부 걸러지고 루프가 끝까지 밀려 결국 `Target`이 `NearestToLocation` 자신이 되는 지점까지 도달했다 — 위 1차 수정의 "이미 도착" 트리비얼 케이스가 엉뚱하게 발동해 대기실 옆 노드를 목표로 오판, 그래프 전체를 건너뛰는 회귀를 냈다(대기실에 있던 모든 로봇이 첫 배차부터 100% 재현). **거리 가드는 완전히 제거**했다.
  - **버그 수정(사용자 지시, 3차)** — 2차에서 제거한 거리 가드 대신, 근본적으로 "가장 가까운 후보가 막혔을 때 왜 막혔는지"를 구분하게 했다. 후보가 `FindPath`에 실패하면, 곧장 다음 후보로 넘어가지 않고 `bIgnoreOccupancy=true`로 한 번 더 확인한다 — 점유만 없었다면 뚫렸을 길이면(진짜 막다른 길이 아니라 순수 점유 경합이면) **다음 후보로 갈아타지 않고 정직하게 실패를 반환**한다. 호출부의 기존 재시도(`WaypointRetryIntervalSeconds`)가 매번 이 함수를 처음부터 다시 부르므로, 정렬 결과 항상 이 같은 후보가 1순위로 재시도되어 결과적으로 "가장 가까운 Outbound의 점유가 풀릴 때까지 계속 기다림"이 된다(사용자가 명시적으로 원한 동작). `bIgnoreOccupancy` 확인 자체에서도 실패하면(설계상 진짜 막다른 길) 그제서야 다음 후보로 넘어간다.
- `FindPathToNearestMarkerWaypoint(FVector StartLocation, FVector MarkerLocation, AFactoryAgentBase* RequestingAgent, TArray<AFactoryNavWaypoint*>& OutRoute)` — 버그 수정(설계 변경): **목표** 쪽도 대칭으로 필요하다. 선반/트레이 슬롯마다 사람이 미리 지정해둔 고정 도킹 참조는 슬롯 27개 중 몇 개만 배선되면 나머지가 조용히 직행하는 등 근본적으로 취약했다. `MarkerLocation`(실제 작업 지점 좌표) 기준으로 `GetDirectionalTier(false)`(항상 **도착지 탐색** — Inbound를 0순위)로 웨이포인트를 목표 후보로 정렬하고, 각 후보에 대해 `FindPathFromNearestReachable`을 재사용해 `StartLocation`에서 실제로 도달 가능한 것을 찾는다. Outbound가 더 가까워도 마커 진입용으로는 쓰지 않는다(위 `FindPathFromNearestReachable`의 확정 재현 버그와 대칭인 문제를 막기 위함). **버그 수정(사용자 지시)** — `FindPathFromNearestReachable`의 "점유 경합이면 다음 후보로 안 넘어가고 기다림" 판단과 대칭으로, 마커측 후보가 `FindPathFromNearestReachable(..., bIgnoreOccupancy=true)`로 구조적으로는 도달 가능하다고 확인되면(점유 경합일 뿐이면) 더 먼 마커측 후보로 넘어가지 않고 정직하게 실패를 반환해 이 후보를 계속 기다린다.

### 이동 실행 상태머신 — `AFactoryAgentBase` (공용)
- `TryRequestWaypointRoute(AFactoryNavWaypoint* TargetWaypoint, const FVector& FinalHopTarget)` — `TargetWaypoint`를 명시하면(대기실처럼 목표가 고정 1개뿐인 경우) `FindPathFromNearestReachable`로 시작점만 동적으로 찾는다. `TargetWaypoint`를 `nullptr`로 넘기면(선반/트레이처럼 슬롯마다 목표가 여러 개인 경우) `FindPathToNearestMarkerWaypoint`로 시작점·목표점 양쪽을 전부 `FinalHopTarget`(마커 좌표) 기준으로 동적으로 찾는다. 어느 경로든 성공하면 첫 홉 예약 후 이동 시작.
- `TryHandleWaypointRouteArrival()` — `OnArrivedAtDestination` 맨 앞에서(`TryHandleIdleZoneArrival`보다도 먼저) 호출. 경로 중간/최종 홉 이동 중이면 다음 단계를 알아서 진행하고 true 반환(호출부는 아무것도 안 함). 웨이포인트 경로가 아예 없었거나 최종 홉까지 전부 끝났으면 false — 호출부가 평소처럼 처리(기존 `TryHandleIdleZoneArrival`/도착 로직이 그대로 이어받음).
  - **버그 수정(사용자 지시)** — 다음 홉 예약(`TryReserve`)이 경합으로 실패하면, 예전엔 곧장 `AbandonWaypointRouteAndReroute()`(전체 재탐색)를 호출했다. 그런데 지금 막힌 노드 하나 때문에 경로 전체를 처음부터 다시 짜면서, 후보 정렬 로직이 엉뚱하게 먼(심지어 이미 지나온) 노드를 새 시작점으로 채택하는 사고가 실제로 재현됐다(60개 노드 경로 중간 재탐색이 수 홉 전 노드로 되돌아감). 이제 원래 계획했던 **같은 목표 노드를 그대로 유지한 채** 그 자리에서 기다렸다가 `RetryNextHopReservation()`으로 `WaypointRetryIntervalSeconds`(기본 1초)마다 같은 노드 예약만 다시 시도한다 — 다른 후보로 갈아타지 않으므로 엉뚱한 노드로 빠질 여지 자체가 없다. 노드 자체가 무효(레벨 변경 등)일 때만 예외적으로 전체 재탐색.
  - 계속 막혀 있으면(같은 자리에서 기다리는 상태가 오래 지속) 아래 "최후의 안전망"(`BlockedTimer`)이 결국 전체 재탐색으로 넘어간다 — 이 재시도 자체에는 타임아웃이 없다.
- `AbandonWaypointRouteAndReroute()` — 안전거리 감지가 전방에서 고장 로봇을 발견했을 때, 그리고 최후의 안전망(`BlockedTimer`)이 호출. 현재 홉 예약을 반납하고 현재 위치 기준으로 같은 최종 목적지를 다시 탐색한다(막힌 노드는 점유 중이라 자동 제외). 버그 수정(사용자 요청) — 원래 `TravelPhase==TraversingGraph`(그래프 구간)에서만 동작했는데, FinalHop 중 정적 지오메트리에 막히는 경우(두 안전 트레이스 모두 감지 못함)의 유일한 복구 수단이 되도록 FinalHop도 처리 대상에 포함했다 — FinalHop이었으면 `TryRequestWaypointRoute(nullptr, PendingFinalHopTarget)`로 마커 좌표 기준 처음부터 동적 재탐색한다.

### 목적지별 통합 방식

버그 수정(설계 변경) — 슬롯/트레이마다 사람이 미리 지정해둔 고정 도킹 참조(`AStorageShelf::TransportRobotInboundDocks`/`OutboundDocks`/`GetTransportRobotDock`, `AStorageShelf::AtlasInboundZoneAccessWaypoint`/`OutboundZoneAccessWaypoint`, `AHorizontalTray::TransportRobotInboundDock`)를 전부 제거했다. 선반 슬롯은 27개(3층×9칸)인데 도킹 배열은 존당 9개뿐이라 애초에 층 구분이 없었고, 실제로는 사람이 손으로 채운 슬롯만 그래프를 타고 나머지는 조용히 직행하는 근본적으로 취약한 구조였다. 대기실 복귀 경로(로봇 현재 위치 → 가장 가깝고 도달 가능한 웨이포인트, 이미 동작하던 방식)와 대칭이 되도록, 선반/트레이 목적지도 목표 마커 좌표 기준으로 가장 가깝고 도달 가능한 웨이포인트를 동적으로 찾는 방식으로 통일했다.

| 목적지 | 운송로봇 | 아틀라스 |
|---|---|---|
| 선반 | `TryRequestWaypointRoute(nullptr, GetTransportRobotWorkLocation(FloorIndex, SlotIndex, ZoneType))` — 파랑/빨강 레인 구분 없이 마커에 가장 가깝고 도달 가능한 웨이포인트로 그래프 진입. 노랑(`Waitbound`)이 배치돼 있으면 그쪽이 그래프 마지막 노드로 우선 선택되고, 도착 즉시 FinalHop으로 넘어가지 않고 짝 아틀라스가 먼저 마커에 도착(`Working`)했는지 확인한 뒤에만 진입한다(위 "Waitbound 게이팅" 참고) → FinalHop(짧은 직진)으로 정확한 슬롯 좌표 → 작업. 후보가 전부 점유/불가면(트레이와 달리 물리적 충돌 방지 수단이 이 예약뿐이라) 직행 폴백 없이 `TryRequestWaypointRoute` 내부 재시도만 반복 | `TryRequestWaypointRoute(nullptr, GetAtlasWorkLocation(FloorIndex, SlotIndex, ZoneType))` — 버그 수정(회피 재설계) — 예전엔 검은 공용 백본만 쓰고 진입 즉시 자유 이동으로 넘어갔는데, 아틀라스도 이제 §8-B의 Inbound/Outbound 웨이포인트를 (`AllowedAgentTypes`로) 쓸 수 있어 FinalHop 구간을 최대한 짧게 줄인다. `MaxConcurrentAtlas`(기본 4, 에디터 조정 가능) 초과 시 진입 불가. **버그 수정(사용자 리포트)** — 이건 최초 진입(`StartCurrentAssignment`)만 해당됐고, 같은 배정 안에서 다음 슬롯으로 넘어가는 `ContinueShelfAssignment`의 연속 이동은 여전히 `AIController->RequestMoveWithFilter`로 그래프를 안 타는 순수 직행이었다(같은 선반이라 거리가 짧다는 전제) — 슬롯이 선반 위에서 멀리 떨어져 있으면(다른 층/칸) 근처 웨이포인트를 무시하고 목표 슬롯 근처로 곧장 이동하는 것처럼 보였다. `TryRequestWaypointRoute`로 통일 |
| 트레이 | `TryRequestWaypointRoute(nullptr, GetTransportRobotWorkLocation())` → 도착 후 기존 `TryReserveTransportRobotWorkZone`. **버그 수정(사용자 지시)** — 트레이는 작업 지점이 하나뿐인데, 같은 아틀라스의 여러 트립이 이 트레이를 순차 방문할 수 있다. 트립 순서는 절대 불변(고장은 그 자리에서 정비되므로 다른 로봇으로 대체 불가)인데, 도착 순서로 존을 선점하면 나중 트립의 로봇이 먼저 도착해 정작 아틀라스가 기다리는 이전 트립 로봇이 영원히 못 들어오는 순환 교착이 발생했다(실제 재현). `TryStartMoveToPoint`가 `FindAtlasForTrip(CurrentTask.TaskID)`(이미 있는 함수 — 아틀라스가 `PopNextReservedSlot()`으로 이 트립을 꺼내야만 non-null)로 "아틀라스가 이미 내 트립까지 처리 중인지" 먼저 확인하고, 아니면(이전 트립 처리 중) `TryReserveTransportRobotWorkZone` 시도는커녕 `TryRequestWaypointRoute` 자체를 호출하지 않는다 — **물리적으로 마커에 접근하는 이동 자체가 시작되지 않는다.** 로봇-트립 배정은 절대 바뀌지 않고, "언제 들어갈 수 있는지"만 트립 순서에 강제로 맞춘다. | 버그 수정(사용자 지시) — 원래 여기가 `AIController->RequestMoveWithFilter`로 그래프를 아예 안 타는 순수 직행이었다(마커까지 거리가 짧다는 전제). 아틀라스가 Inbound/Outbound 웨이포인트를 쓸 수 있게 된 이상 배송로봇과 동일하게 `TryRequestWaypointRoute(nullptr, GetAtlasWorkLocation())`로 통일 — 트래픽이 가장 몰리는 트레이 접근이 이번 회피 재설계의 보호를 받게 됐다 |
| 대기실 | 버그 수정(회피 재설계) — `AIdleWaitingZone::AccessWaypoint`(고정 단일 참조)를 제거했다. 선반/트레이와 동일하게 `TryRequestWaypointRoute(nullptr, HomeSlotTransform.GetLocation())`로 통일 — 진입/이탈 양쪽 모두 목표 마커(홈 슬롯) 기준 최근접 도달가능 웨이포인트를 매번 동적으로 찾는다. 레벨에 대기실 진입 측/이탈 측 각각 가까운 위치에 전용 웨이포인트를 배치해두면(Inbound/Outbound + `AllowedAgentTypes`) 코드가 방향을 강제하지 않아도 배치된 거리만으로 자연히 분리돼 쓰인다 | 동일 패턴, `AIdleWaitingZone::AllowedAgentTypes`(비트마스크, 버그 수정(사용자 지시) — 단일값에서 전환)로 대기실별 허용 타입 구분 |

버그 수정 — 예전엔 도킹 웨이포인트가 레벨에 안 배선돼 있으면(과도기) 그래프 없이 직행하는 폴백이 있었다. 고정 도킹 참조 자체가 없어지면서 "배선 여부" 개념도 사라져 이 폴백을 제거했다 — 그래프가 목표 근처를 전혀 커버하지 못해도(레벨 공사 중 등) 직행하지 않고 `TryRequestWaypointRoute`가 실패 로그를 남기며 재시도만 반복한다(대기실 포함 전 목적지 공통 원칙). 로봇이 멈춘 것처럼 보이면 "TryRequestWaypointRoute 실패" 로그부터 확인할 것.

### Waitbound 게이팅 — 선반 마커 진입 순서 보장 (배송로봇 전용)

**원인**: `AStorageShelf::ComputeWorkLocation`은 마커 위치에서 선반 정면축으로만(Lateral 오프셋 없음) `AtlasWorkDistance`(150)/`TransportRobotWorkDistance`(300)만큼 뗀 지점을 쓴다 — 같은 축, 같은 부호라 **아틀라스 마커가 배송로봇 마커보다 항상 선반에 더 가깝다**(존 타입 무관, 모든 슬롯 공통 — `HorizontalTray`는 `LateralOffset`이 있어 이 문제가 없다). 즉 아틀라스가 자기 마커에 닿으려면 반드시 배송로봇 마커 지점을 지나야 한다. safety trace의 트립 파트너 제외(`GetCurrentTripPartner`)는 Pause 유발 여부만 빼주는 연성 로직이라 실제 캡슐 충돌은 못 막는다 — 배송로봇이 먼저 도착해 정지하면 뒤따르는 아틀라스가 물리적으로 가로막힌다(실제 재현된 버그).

**해결**: `EWaypointAccess::Waitbound`를 Inbound와 마커 사이에 두는 게이트 노드로 도입 — 경로는 `... → Inbound → Waitbound → (FinalHop) → 마커`가 된다.
- `AFactoryAgentBase::CanProceedFromWaitbound()`(가상 함수, 기본 `true`=게이팅 없음) — `TryHandleWaypointRouteArrival()`이 그래프 마지막 노드가 `Waitbound`인데 이 함수가 `false`를 반환하면, FinalHop으로 넘어가지 않고 그 자리에서 대기한다. `WaitboundRecheckIntervalSeconds`(기본 1초, 에디터 조정 가능) 간격으로 재확인.
- `AFactoryTransportRobot::CanProceedFromWaitbound()` override — `GetCurrentTripPartner()`(짝 아틀라스)가 존재하고 `CurrentState == EAgentState::Working`(이미 자기 마커에 도착)일 때만 `true`. 아틀라스는 항상 더 안쪽이라 배송로봇을 기다릴 필요가 없으므로 override하지 않는다(기본 `true` 유지).
- **노드 예약**: 다른 노드는 도착 즉시 반납하지만(관문일 뿐), Waitbound에서 대기 중(`bAwaitingWaitboundClearance == true`)인 동안은 예약을 쥔 채로 둔다 — 안 그러면 대기 중인 로봇이 실제로 서 있는 자리에 다른 로봇이 예약·진입할 수 있다. `AbandonAnyActiveWaypointRoute()`가 중간에 경로를 취소하면(다른 이동 의도 개입, 안전거리 재탐색 등) 대기 플래그/타이머도 함께 정리되고, 쥐고 있던 Waitbound 예약도 기존 로직 그대로 반납된다.
- **동시 다른 Inbound로 오검지 가능성 검토**: `FindPathToNearestMarkerWaypoint`는 레벨의 모든 후보 중 "이 마커에 가장 가깝고 실제 도달 가능한" 것을 고르므로(Inbound/Outbound/Waitbound 동일 취급, 거리순), 각 선반의 Waitbound가 자기 마커에 물리적으로 가장 가깝게 배치돼 있는 한 다른 선반의 Waitbound로 잘못 갈 일은 없다 — Inbound/Outbound가 이미 같은 방식으로 선택되고 있고 이번 세션 내내 문제없이 검증된 메커니즘과 동일하다. 다른 로봇이 이미 점유한 Inbound/Waitbound에는 `FindPath`의 점유 필터(`IsOccupied() && !IsOccupiedBy(RequestingAgent)`)가 모든 노드 타입에 균일하게 적용되어 자동으로 후보에서 빠진다(신규 코드 불필요, 기존 메커니즘 그대로 적용됨).
- **주의(레벨 배치 책임)** — 위 점유 필터 때문에, 특정 선반에 Waitbound가 1개뿐인데 그 선반에 동시 접근 가능한 배송로봇이 여러 대면(`MaxConcurrentAtlas`가 1보다 큰 선반), 먼저 도착한 로봇이 Waitbound를 점유한 동안 **두 번째 로봇은 그 Waitbound에 도달하지 못해 `FindPathToNearestMarkerWaypoint`가 차순위로 가까운 후보(순수 Inbound)를 대신 골라 그쪽을 그래프 마지막 노드로 확정할 수 있다** — 이 경우 게이팅 자체가 조용히 우회된다(Waitbound를 거치지 않으므로 `CanProceedFromWaitbound()` 검사가 아예 실행되지 않음). 이 케이스를 완전히 막으려면 선반당 Waitbound 개수를 `MaxConcurrentAtlas`와 맞춰 배치할 것 — 코드가 아니라 레벨 배선 책임이다.

### 안전거리 감지 — `AFactoryAgentBase::RunSafetyTraceCheck` (공용, 회피 재설계로 전면 재작성)
예약이 항상 완벽하게 지켜진다는 보장은 없다는 전제로 얹는 반응형 안전망. 웨이포인트 예약(협조적 최적화)과는 독립적으로 동작한다.

**배경 — 왜 재설계했는가**: 기존엔 상대가 Working/Idle(의도된 정지)이면 회피를 전적으로 엔진 Detour Crowd(RVO)에 위임했는데, RVO는 매 틱 국소적으로만 후보 속도를 비교하는 반응형 알고리즘이라 "정지 로봇과 선반 사이처럼 좁은 틈"에 끼면 전역 우회 없이 제자리에서 흔들리기만 하는 로컬 미니멈에 빠질 수 있었다(실제 재현). `TravelPhase`에 따라 서로 다른 판정 레이어를 쓴다 — 그래프 구간(`TraversingGraph`)은 넓은 공용 통로라 정상 상황에선 RVO만으로 충분하지만, FinalHop(그래프를 벗어난 마지막 접근)은 조밀해서 별도 판정이 필요하다.

**`EAgentState::Pause`** — "대기+재확인" 전용 상태(리플리케이트되는 기존 `EAgentState` 끝에 추가). 예전엔 `bYieldingForSafety`라는 bool을 `Moving` 위에 얹어 표현했는데, 그러면 `Moving`이 "이동 의도"와 "실제 이동 중"을 동시에 뜻해 AI 학습 데이터로서의 가치가 떨어졌다 — 이제 `Moving`은 실제로 움직이는 중일 때만 쓴다. `AFactoryAgentBase::Tick`의 Blocked 판정(정적 지오메트리 정지 감지용 최후의 안전망, 아래 참고)도 `Moving`과 함께 `Pause`를 대상에 포함한다.

**그래프 구간 — `RunGraphSegmentTraceCheck`**:
- `USafetyTraceMarkerComponent`(`USceneComponent` 상속, `UStorageSlotMarkerComponent`와 동일한 마커 패턴) 각각의 위치/전방 벡터로 `SafetyTraceIntervalSeconds`(기본 1초, 에디터 조정 가능)마다 `SafeDistanceUnits`(웨이포인트 최장 변보다 살짝 길게 잡을 것 — 에디터 조정 가능)까지 라인트레이스(`ECC_Pawn`). 마커가 없으면(과도기) 이동 방향 1개로 대체. 여러 마커/한 트레이스에 여러 대상이 걸릴 수 있어 전체에서 가장 가까운 히트 하나만 채택한다.
- 이번 트립의 협력 상대(`GetCurrentTripPartner()` — 아틀라스는 짝 배송로봇을, 배송로봇은 짝 아틀라스를 반환)는 트레이스 대상에서 제외한다 — 안 그러면 핸드오프하러 다가가는 상대를 스스로 감지해 접근을 멈춰버린다.
- 감지 시 판정: **Broken**(타입 무관) → 대기 없이 즉시 `AbandonWaypointRouteAndReroute()`. **나와 다른 `EActorType`이면서 Moving** → 즉시 재탐색(지속적인 속도차라 기다려도 다시 벌어지지 않음 — 예: 배송로봇이 아틀라스 뒤에 계속 따라붙는 경우). **그 외**(같은 타입은 상대 상태 무관, 다른 타입이라도 정지 상태) → `Pause` 전환(`PauseMove()`), 없어지면 `Moving` 복귀(`ResumeMove()`). 상대도 Moving이면 `AgentID` 해시로 결정론적 우선순위를 매겨 한쪽만 양보한다(둘 다 양보하면 영구 교착 — 실제 재현됐던 버그의 수정, 새 설계에도 유지).
- **Pause 장시간 지속 시 재탐색** — 원인(체인으로 여러 대가 순차적으로 발이 묶이는 등)을 가리지 않고, `Pause`가 `PauseRerouteTimeoutSeconds`(기본 5초, 에디터 조정 가능) 넘게 이어지면 그래프 구간 한정으로 `AbandonWaypointRouteAndReroute()`를 시도한다.

**FinalHop 전용 면(박스) 트레이스 — `RunFinalHopAreaTraceCheck`**: 정지 로봇과 선반 사이처럼 좁은 구간은 그래프 예약 확장(아틀라스 Inbound/Outbound 도입)만으로 전부 커버되지 않아 별도 판정을 둔다.
- `FinalHopTraceDistance`/`FinalHopTraceHalfWidth`(에디터 조정 가능, 로봇 캡슐 크기에 맞춰 축소됨)로 전방 박스를 스윕(`OverlapMultiByChannel`, `ECC_Pawn`) — 라인이 아니라 면이라 정면에서 살짝 벗어난 대상도 잡는다. 역시 트립 협력 상대는 제외.
- 박스 진행 방향: 이동 중(속도 있음)이면 속도 벡터, 정지 상태(도착 후 제자리 회전 등 속도 0)면 목표 지점(`PendingFinalHopTarget`) 방향 — **actor forward 벡터는 쓰지 않는다**. 버그 수정: forward를 쓰면 제자리 회전이 진행되는 동안 박스가 함께 휩쓸려 옆 칸(작업구역이 좁아 캡슐 크기로 트레이스 폭을 낮춘 만큼 인접 칸과 거리가 매우 가까움)의 무관한 로봇을 오탐하는 문제가 있었다.
- **FinalHop 진입 중(아직 도착 전)에만 동작**하고 도착하면 멈춘다 — 이미 작업을 마치고 벗어나는 로봇(그래프 구간으로 전환됨)은 이 판정을 받지 않는다. 그래서 진입하는 로봇이 상대를 감지하고 멈춰있는 동안, 작업을 마친 로봇은 이 트레이스가 꺼져 있어 구역을 벗어나는 데 지장이 없다.
- 그래프 구간과 달리 타입 구분 없이 감지되면 무조건 `Pause`만 한다(새 목적지 탐색 없음) — 이 짧은 구간은 접근 각도를 바꿀 여지가 거의 없어, 후진/우회 지점을 계산해봐야 또 다른 실패 지점만 만든다(회피 재설계 1차 시도에서 실제로 확인됨).
- **예외 — Broken이 선반 접근을 막는 경우**: `TryHandleFinalHopBrokenBlock(AFactoryAgentBase* BrokenAgent)`(가상 함수, 기본은 처리 안 함=false) 훅을 먼저 거친다. 아틀라스가 override해서, 정비 중인 NPC가 하필 인접 슬롯 접근을 막으면(선반은 연속 구조라 정비 위치에 따라 발생 가능 — 트레이는 슬롯이 하나뿐이라 해당 없음) 같은 선반의 다른 칸으로 재할당을 시도한다:
  1. `AStorageShelf::TryReserveEmptySlot`(Inbound)/`TryReserveOldestOccupiedSlot`(Outbound)로 대체 칸을 **먼저** 확보한다. 못 찾으면(대안 없음) 아무 것도 안 하고 기본 `Pause`로 폴백 — 즉 이 경우 새 로직 없이 수리가 끝날 때까지 대기한다.
  2. 대체 칸을 찾은 뒤에만 `AStorageShelf::ReleaseSlotReservation`로 원래 칸을 반납한다(순서 중요 — 반납을 먼저 하면 그 사이 다른 입고 작업이 이 칸을 채갈 수 있다).
  3. 짝 배송로봇도 같은 트립이라 물리적으로 같은 칸에서 만나야 핸드오프가 성립한다 — `AFactoryTransportRobot::RetargetCurrentTaskSlot`로 같이 갱신(이미 그 칸을 향해 이동/도착해 있었으면 새 칸으로 다시 이동시킨다).
  4. 이 아틀라스 자신의 배정은 막힌 슬롯만 새 좌표로 바꿔 다시 큐에 넣고(`UOutboundDispatchSubsystem::RequeueStationAssignment` 재사용 — 다중 트립 배정 중이면 아직 안 건드린 미래 트립은 그대로 보존), 자신은 Idle로 돌아가 통상적인 배차 스윕에 다시 맡긴다 — 마침 더 가까이 있는 다른 유휴 아틀라스가 새 칸을 대신 가져갈 수도 있다(효율화는 새 로직이 아니라 기존 배차 스윕의 자연스러운 결과).
- 두 트레이스 모두 감지 시 `FAnomalyEvent`를 `Code:002`(세이프티존 침범)로 `UFactoryEventBusSubsystem::PublishAnomaly` 발행(`01_EventBus_DataPipeline.md` 참고).

**최후의 안전망 — 정적 지오메트리 정지 감지 (`AFactoryAgentBase::Tick`, 사용자 요청, 대비책)**: 위 두 트레이스는 히트/오버랩된 액터를 `Cast<AFactoryAgentBase>`로 판정하므로, 선반처럼 **정적 지오메트리에 막히면 아예 감지가 안 되고 `Pause`조차 안 걸린 채 영구 정지**할 수 있다(원인 특정 없이도 대비만 해두는 안전망 — 왜 막혔는지는 알려주지 않는다).
- 기존 `BlockedTimer`(속도 0인 채 `BlockedThresholdSeconds`=2초 넘게 지속되면 누적, `Tick`이 이미 관리하던 값)를 재사용. `CurrentState == Moving`(자체 타임아웃 로직이 있는 `Pause`는 제외)일 때만, `BlockedRecoveryRetryIntervalSeconds`(기본 2초, 에디터 조정 가능) 간격으로 원인을 가리지 않고 주기적으로 `AbandonWaypointRouteAndReroute()`를 강제 호출한다.
- 버그 수정 — `AbandonWaypointRouteAndReroute()`는 원래 `TravelPhase==TraversingGraph`(그래프 구간)에서만 동작했다. FinalHop 중 정적 장애물에 막히는 경우(이번 안전망이 노리는 바로 그 케이스)는 처리 대상이 아니었어서, FinalHop도 포함하도록 확장했다 — FinalHop이었으면 그래프 마지막 노드 참조 없이 `TryRequestWaypointRoute(nullptr, PendingFinalHopTarget)`로 마커 좌표 기준 처음부터 동적 재탐색한다.
- **버그 수정(사용자 리포트)** — 이 안전망이 "왜 정지했는지" 구분을 안 해서, 스스로 무기한 재시도하는 정상적인 대기 상태(`bAwaitingWaitboundClearance`, 다음 홉 재시도 `bIsWaitingForNextHopReservation` — 둘 다 `CurrentState`는 `Moving`인 채로 속도만 0)까지 2초 뒤에 강제로 깨버렸다. 그 결과 혼잡한 구간(공용 웨이포인트 병목 등)에서 다음 홉 재시도가 몇 초만 걸려도 이 안전망이 끼어들어 전체 재탐색을 시켜버려, 정작 "같은 노드를 그대로 기다리기" 수정이 막으려던 엉뚱한 노드 점프가 재발했다(실제 재현). 두 대기 플래그가 켜져 있는 동안은 이 안전망이 개입하지 않도록 조건에서 제외했다 — 두 메커니즘 모두 이미 자체적으로 무기한 재시도하므로 안전망이 불필요하고, 오히려 방해가 됐다.

### 리플리케이션
이 레이어에서 추가되는 모든 상태(웨이포인트 점유자, 경로 진행 인덱스, Pause 누적 시간)는 리플리케이트하지 않는다 — `CurrentAssignment`/`HomeIdleZone`과 동일한 근거로 서버 전용 부기 값이며, `CurrentState`(`Pause` 포함, 기존 `EAgentState`가 이미 리플리케이트됨)/위치(기본 이동 복제)만으로 클라이언트 렌더링에 충분하다. `AStorageShelf::InboundZoneOccupants`/`OutboundZoneOccupants`처럼 기존에 이미 `Replicated`였던 필드는 리플리케이션 속성을 유지한 채 타입만 배열로 확장했다.
