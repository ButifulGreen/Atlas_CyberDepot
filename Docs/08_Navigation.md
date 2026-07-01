# 8. 내비게이션 / 코스트 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §8. 구현 3단계 대상. `AIModule`, `NavigationSystem` 모듈이 Build.cs Private Dependency에 필요하다(`CLAUDE.md` 참고).

### `UNavArea_MainLane` / `UNavArea_SideSpace` / `UNavArea_Critical` (UNavArea 상속)
- `DefaultCost` 오버라이드만 다르게 설정 (1 / 10 / 100). 런타임에 액터가 이 클래스 자체를 교체하는 방식은 사용하지 않는다(아래 `ACostZoneVolume` 참고).

### `UNavQueryFilter_Robot` / `UNavQueryFilter_NPC` (UNavigationQueryFilter 상속)
- 로봇: MainLane 선호 / SideSpace 기피. NPC: 전 구역 동일 코스트.

### `ACostZoneVolume` (AActor, 미리 배치되는 풀링 대상)
런타임 NavArea 클래스 스왑 방식을 사용하지 않고, NavQueryFilter 런타임 코스트 조정 방식을 사용한다. Area Class를 런타임에 교체하면 해당 타일의 NavMesh가 비동기 재빌드되어(엔진 특성), 에이전트 수·교차로 수가 늘어날수록 GameThread/Navigation Tick 병목 위험이 커진다. 대신 이 볼륨은 "현재 이 구역이 얼마나 혼잡한가"라는 상태값만 들고 있고, 실제 코스트 반영은 각 에이전트의 `AFactoryAIController::ApplyDynamicCongestionCost`가 이동 요청 직전 자신의 `QueryFilterClass` 인스턴스에 AreaCost로 적용한다(`04_Agent_AI.md` 참고). NavMesh 지오메트리/Area 페인팅 자체는 정적으로 유지된다.

- 멤버
  - `int32 BlockerCount`
  - `double LastChangeTimestamp`
  - `float MinHoldTimeSeconds = 0.5f`
  - `float CongestionCostMultiplier = 1.f` (`BlockerCount > 0`일 때 이 구역을 지나는 경로 요청에 적용할 코스트 배수. NavArea 클래스는 변경하지 않음)
- 함수
  - `void RegisterBlocker(AActor* Blocker)`
  - `void UnregisterBlocker(AActor* Blocker)`
  - `void TickPendingReset(float CurrentTime)`
  - `float GetCurrentCostMultiplier() const` (`AFactoryAIController::ApplyDynamicCongestionCost`가 조회)
