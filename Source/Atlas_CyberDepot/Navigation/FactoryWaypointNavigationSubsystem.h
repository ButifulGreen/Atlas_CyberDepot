// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EventBus/FactoryEventTypes.h"
#include "FactoryWaypointNavigationSubsystem.generated.h"

class AFactoryNavWaypoint;
class AFactoryAgentBase;

// Docs/08_Navigation.md — 웨이포인트 그래프 탐색 전용 유틸리티(상태 없음). 예약 상태는
// AFactoryNavWaypoint 자신이 들고 있으므로, 이 서브시스템은 레벨의 웨이포인트 목록 캐싱 +
// 탐색 함수만 제공한다(그래프를 중앙에서 관리하는 매니저가 아니다).
UCLASS()
class UFactoryWaypointNavigationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	AFactoryNavWaypoint* FindNearestWaypoint(const FVector& Location, EActorType AgentType) const;

	// 점유 중이거나(RequestingAgent 자신이 점유한 노드는 예외) AgentType이 못 쓰는 노드는
	// 후보에서 제외한 다익스트라. 경로 없으면 false.
	// 버그 수정(사용자 지시) — bIgnoreOccupancy=true면 점유 체크를 건너뛴다. "지금 당장 갈 수 있는지"가
	// 아니라 "점유만 아니었다면 구조적으로 도달 가능한지"를 묻는 순수 위상 확인 용도(아래 두 함수의
	// "가장 가까운 후보가 막혀도 더 먼 후보로 갈아타지 않고 기다리기" 판단에 사용).
	bool FindPath(AFactoryNavWaypoint* Start, AFactoryNavWaypoint* Target, AFactoryAgentBase* RequestingAgent, TArray<AFactoryNavWaypoint*>& OutRoute, bool bIgnoreOccupancy = false) const;

	// 버그 수정 — 최근접 웨이포인트 단 하나만 시작점으로 시도하면, 마커 진입 전용으로 나가는 연결이 없는
	// 노드(Docs/08_Navigation.md의 Inbound "탈출 지점")가 우연히 가장 가까울 때 경로 탐색이 영구 실패했다.
	// 가까운 순서대로 후보를 넘어가며 실제로 Target까지 경로가 나오는 시작점을 찾는다.
	// 버그 수정(사용자 지시) — 가장 가까운 후보(Outbound 우선)가 점유 경합으로만 막혀 있으면(점유를
	// 무시하면 구조적으로 뚫려있으면) 더 먼 후보로 갈아타지 않고 정직하게 실패를 반환한다 — 호출부의
	// 기존 1초 재시도가 매번 이 후보를 다시 1순위로 시도하므로 결과적으로 "이 후보가 풀릴 때까지 기다림"이
	// 된다. 설계상 진짜 막다른 길(점유를 무시해도 도달 불가)일 때만 다음 후보로 넘어간다.
	// bIgnoreOccupancy=true(재귀적으로 넘어오는 순수 위상 확인 모드)일 때는 이 "기다림" 판단 자체가
	// 의미 없으므로(이미 점유를 무시하고 있음) 원래의 단순 폴백(실패 시 다음 후보)으로 동작한다.
	bool FindPathFromNearestReachable(const FVector& Location, AFactoryNavWaypoint* Target, AFactoryAgentBase* RequestingAgent, TArray<AFactoryNavWaypoint*>& OutRoute, bool bIgnoreOccupancy = false) const;

	// 버그 수정 — 목표 쪽도 대칭이 필요하다. 선반/트레이 슬롯마다 사람이 미리 지정해둔 고정 도킹 참조는
	// 슬롯 27개 중 몇 개만 배선되면 나머지가 전부 조용히 직행하는 등 근본적으로 취약했다. MarkerLocation에
	// 가까운 순서대로 웨이포인트를 목표 후보로 시도해(FindPathFromNearestReachable을 그대로 재사용),
	// StartLocation에서 실제로 도달 가능한 것을 찾는다.
	// 버그 수정(사용자 지시) — FindPathFromNearestReachable과 대칭으로, 가장 가까운 마커측 후보(Inbound
	// 우선)가 점유 경합으로만 막혀 있으면 더 먼 후보로 갈아타지 않고 기다린다.
	bool FindPathToNearestMarkerWaypoint(const FVector& StartLocation, const FVector& MarkerLocation, AFactoryAgentBase* RequestingAgent, TArray<AFactoryNavWaypoint*>& OutRoute) const;

protected:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	UPROPERTY()
	TArray<TObjectPtr<AFactoryNavWaypoint>> AllWaypoints;
};
