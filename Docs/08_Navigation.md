# 8. 내비게이션 / 코스트 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §8. 구현 3단계 대상. `AIModule`, `NavigationSystem` 모듈이 Build.cs Private Dependency에 필요하다(`CLAUDE.md` 참고).

## 스코프 분리 (Docs 이탈, 승인됨 — `00_DesignPrinciples.md` 참고)

극한 부하 테스트 중 NavMesh 코스트 기반 자유 이동이 진입각도에 따라 트레이 콜리전에 걸리는 버그가 발견됐고, 실제 대형 자동화 물류창고(아마존 로보틱스, 중국 Geek+/Quicktron류)가 자유 회피가 아니라 웨이포인트 그래프+예약 기반으로 동작한다는 점, 그리고 이 프로젝트의 AI 학습 데이터 생성 목적에 이 방식이 더 부합한다는 점을 근거로 이동 레이어를 둘로 나눴다.

- **NPC(정비 인력)**: 아래 §8-A(NavArea/QueryFilter/CostZoneVolume, 자유 이동 + Detour Crowd 회피)를 그대로 사용. 고장 로봇이 어디서 발생할지 예측 불가능해 자유 이동이 맞다.
- **아틀라스/운송로봇**: 거점 간 이동(선반/트레이/대기실 오가기)은 §8-B(웨이포인트 그래프 + 예약)로 전환. 작업구역 진입(그래프를 벗어난 마지막 접근, FinalHop) 이후에도 물리 이동 자체는 여전히 자유 Nav 제어(§8-A, 엔진 Detour Crowd)를 쓰지만, 좁은 구간에서 RVO가 로컬 미니멈(제자리 흔들림)에 빠지는 문제가 실제로 재현되어(회피 재설계, 아래 "FinalHop 전용 면 트레이스" 참고) §8-B 쪽에 전용 안전망을 추가했다 — RVO는 여유 공간에서의 매끄러운 스무딩 역할로 축소되고, 조밀한 구간의 정지/재개 판단은 이제 §8-B가 담당한다.

## §8-A. NavMesh 코스트 레이어 (NPC 전용 + 아틀라스 작업구역 내부)

### `UNavArea_MainLane` / `UNavArea_SideSpace` / `UNavArea_Critical` (UNavArea 상속)
- `DefaultCost` 오버라이드만 다르게 설정 (1 / 10 / 100). 런타임에 액터가 이 클래스 자체를 교체하는 방식은 사용하지 않는다(아래 `ACostZoneVolume` 참고).

### `UNavQueryFilter_Robot` / `UNavQueryFilter_NPC` (UNavigationQueryFilter 상속)
- 로봇: MainLane 선호 / SideSpace 기피. NPC: 전 구역 동일 코스트.

### `ACostZoneVolume` (AActor, 미리 배치되는 풀링 대상)
런타임 NavArea 클래스 스왑 방식을 사용하지 않고, NavQueryFilter 런타임 코스트 조정 방식을 사용한다. Area Class를 런타임에 교체하면 해당 타일의 NavMesh가 비동기 재빌드되어(엔진 특성), 에이전트 수·교차로 수가 늘어날수록 GameThread/Navigation Tick 병목 위험이 커진다. 대신 이 볼륨은 "현재 이 구역이 얼마나 혼잡한가"라는 상태값만 들고 있고, 실제 코스트 반영은 각 에이전트의 `AFactoryAIController::ApplyDynamicCongestionCost`가 이동 요청 직전 자신의 `QueryFilterClass` 인스턴스에 AreaCost로 적용한다(`04_Agent_AI.md` 참고). NavMesh 지오메트리/Area 페인팅 자체는 정적으로 유지된다.

- 멤버
  - `TSubclassOf<UNavArea> AffectedAreaClass` (Docs 이탈 사항 — 이번에 반영. 이 존의 혼잡도 배수를 어느 NavArea 클래스에 적용할지 볼륨마다 레벨에서 지정)
  - `int32 BlockerCount`
  - `double LastChangeTimestamp`
  - `float MinHoldTimeSeconds = 0.5f`
  - `float CongestionCostMultiplier = 1.f` (`BlockerCount > 0`일 때 이 구역을 지나는 경로 요청에 적용할 코스트 배수. NavArea 클래스는 변경하지 않음)
- 함수
  - `void RegisterBlocker(AActor* Blocker)`
  - `void UnregisterBlocker(AActor* Blocker)`
  - `void TickPendingReset(float CurrentTime)`
  - `float GetCurrentCostMultiplier() const` (`AFactoryAIController::ApplyDynamicCongestionCost`가 조회)

### 구간별 세분화 컨벤션 (도심 교통 혼잡도 표현 방식)
`AFactoryAIController::ApplyDynamicCongestionCost`의 코스트 오버라이드는 NavArea **클래스** 단위로 걸린다(`FactoryAIController.cpp`의 `ApplyAreaCostOverride`가 `AreaClass` 정확히 일치하는 항목만 갱신). 물리적으로 떨어진 통로 여러 곳이 전부 같은 `UNavArea_MainLane` 클래스로 페인팅돼 있으면, 한 통로의 혼잡이 무관한 다른 통로까지 코스트를 올려버린다.

이를 막으려면 새 C++ 클래스를 추가하는 대신, 통로(물리적 세그먼트)마다 `UNavArea_MainLane`/`UNavArea_SideSpace`를 부모로 하는 **Blueprint 서브클래스**를 하나씩 만들어 그 통로에만 페인팅하고, 해당 통로의 `ACostZoneVolume::AffectedAreaClass`를 같은 서브클래스로 지정한다. 언리얼은 BP 서브클래스도 각각 별도 Area ID로 취급하므로, `TSubclassOf<UNavArea>` 기반 오버라이드가 자연히 그 통로에만 국한된다. 인접한 두 세그먼트의 NavModifierVolume은 경계 폴리곤이 빈틈없이 코스트 태그를 받도록 살짝 겹치게 배치할 것(안 그러면 경계가 코스트 0짜리 지름길이 될 수 있음).

> **정리 후보**: 아틀라스/운송로봇의 주 이동이 §8-B로 넘어가면서 이 레이어는 사실상 NPC 전용(+ 아틀라스 작업구역 내부의 아주 짧은 구간)으로만 쓰인다. 코드는 그대로 두되, §8-B가 PIE에서 충분히 검증된 뒤 삭제 여부를 별도로 논의한다.

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
