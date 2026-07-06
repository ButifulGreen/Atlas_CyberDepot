# 8. 내비게이션 / 코스트 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §8. 구현 3단계 대상. `AIModule`, `NavigationSystem` 모듈이 Build.cs Private Dependency에 필요하다(`CLAUDE.md` 참고).

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
