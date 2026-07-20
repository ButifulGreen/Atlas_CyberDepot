// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/FactoryWaypointNavigationSubsystem.h"
#include "Atlas_CyberDepot.h"
#include "Navigation/FactoryNavWaypoint.h"
#include "Agent/FactoryAgentBase.h"
#include "Kismet/GameplayStatics.h"

void UFactoryWaypointNavigationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	TArray<AActor*> FoundWaypoints;
	UGameplayStatics::GetAllActorsOfClass(&InWorld, AFactoryNavWaypoint::StaticClass(), FoundWaypoints);

	AllWaypoints.Reset(FoundWaypoints.Num());
	for (AActor* Actor : FoundWaypoints)
	{
		if (AFactoryNavWaypoint* Waypoint = Cast<AFactoryNavWaypoint>(Actor))
		{
			AllWaypoints.Add(Waypoint);
		}
	}
}

AFactoryNavWaypoint* UFactoryWaypointNavigationSubsystem::FindNearestWaypoint(const FVector& Location, EActorType AgentType) const
{
	AFactoryNavWaypoint* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();

	for (AFactoryNavWaypoint* Waypoint : AllWaypoints)
	{
		if (!Waypoint || !Waypoint->IsUsableBy(AgentType))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(Waypoint->GetActorLocation(), Location);
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = Waypoint;
		}
	}

	return Nearest;
}

bool UFactoryWaypointNavigationSubsystem::FindPathFromNearestReachable(const FVector& Location, AFactoryNavWaypoint* Target, AFactoryAgentBase* RequestingAgent, TArray<AFactoryNavWaypoint*>& OutRoute, bool bIgnoreOccupancy) const
{
	OutRoute.Reset();
	if (!Target || !RequestingAgent)
	{
		return false;
	}

	const EActorType AgentType = RequestingAgent->AgentType;

	// 버그 수정(사용자 리포트) — Location에 실제로 가장 가까운 사용 가능 노드가 Target 자신일 때만
	// "이미 도착" 1노드 경로를 허용한다. 아래 후보 루프가 이 확인 없이 Target 자신도 시작점 후보에
	// 포함시키면, 다른 모든 후보가(Inbound류는 설계상 나가는 연결이 없는 막다른 노드라 대부분 실패)
	// 실패했을 때 마지막으로 Target 자신이 걸려 FindPath의 Start==Target 트리비얼 케이스가 발동한다 —
	// Location이 Target과 전혀 무관하게 멀리 떨어져 있어도 "이미 도착"으로 오판해 그래프 전체를
	// 건너뛰고 FinalHop으로 직행하는 버그였다(대기실→트레이 이동 시 100% 가까운 확률로 재현 보고됨).
	AFactoryNavWaypoint* NearestToLocation = FindNearestWaypoint(Location, AgentType);
	if (NearestToLocation == Target)
	{
		OutRoute.Add(Target);
		return true;
	}

	// 버그 수정(사용자 지시) — 한때 "폴백 후보가 Location에서 너무 멀면 제외"하는 거리 가드를 넣었었다.
	// 그런데 정상적인 후보(예: 대기실 옆에서 백본까지의 첫 홉)도 실제 그래프 간격상 그 임계값보다 먼 경우가
	// 흔해서, 정상 후보가 전부 걸러지고 루프가 끝까지 밀려 결국 Target이 NearestToLocation 자신이 되는
	// 지점까지 도달 — 위의 "이미 도착" 트리비얼 케이스가 엉뚱하게 발동해 대기실 옆 노드를 목표로 오판하고
	// 그래프 전체를 건너뛰는 심각한 회귀를 냈다(모든 로봇이 대기실에서 동시 출발할 때 100% 재현). 거리
	// 가드는 완전히 제거한다 — "가까운 후보가 막혀서 더 먼 후보로 폴백"하는 문제 자체는, 이 가드가 아니라
	// (1) TryHandleWaypointRouteArrival의 다음 홉 예약 단계에서 "막힌 노드를 기다렸다 재시도"하는 방식과
	// (2) 아래 후보 루프 자체가 "점유 경합이면 다음 후보로 안 넘어가고 기다림"으로 판단하는 방식, 둘로
	// 대신 해결한다 — 두 경우 모두 다른 후보로 갈아타지 않고 원래 노드를 그대로 기다리므로, 엉뚱한
	// 후보로 빠질 여지 자체가 없다.
	TArray<AFactoryNavWaypoint*> Candidates;
	for (AFactoryNavWaypoint* Waypoint : AllWaypoints)
	{
		if (Waypoint && Waypoint != Target && Waypoint->IsUsableBy(AgentType))
		{
			Candidates.Add(Waypoint);
		}
	}

	// 버그 수정(사용자 지시) — 단순 거리순만 쓰면 레벨 배치상 우연히 Common 백본 노드가 Inbound/Outbound
	// 전용 레인보다 마커에 더 가까울 때 공용 백본에서 마커로 바로 붙는(혹은 그 반대) 결과가 나올 수
	// 있었다. Common이 아닌 후보를 항상 먼저 시도하고, 그런 후보가 하나도 없거나 전부 도달 불가능할
	// 때만 Common으로 폴백한다.
	// 버그 수정(사용자 지시, 확정 재현) — 여기(Location 기준 시작점 탐색)는 언제나 "지금 있는 자리에서
	// 그래프로 이탈"하는 목적이라 Outbound를 우선해야 하는데, 기존 IsPureCommon() 2단계 정렬은 Inbound/
	// Outbound를 동일 취급해서 마커 근처에 Inbound가 Outbound보다 가까우면 그쪽으로 잘못 이탈했다(배송
	// 로봇이 가까운 Outbound 대신 Inbound/Common으로 이탈하는 증상으로 재현). GetDirectionalTier(true)로
	// Outbound를 0순위로 강제한다.
	Candidates.Sort([&Location](const AFactoryNavWaypoint& A, const AFactoryNavWaypoint& B)
	{
		const int32 TierA = A.GetDirectionalTier(true);
		const int32 TierB = B.GetDirectionalTier(true);
		if (TierA != TierB)
		{
			return TierA < TierB;
		}
		return FVector::DistSquared(A.GetActorLocation(), Location) < FVector::DistSquared(B.GetActorLocation(), Location);
	});

	// 가장 가까운 후보가 마커 진입 전용(나가는 연결 없음)이라 이번 Target으로는 막다른 길일 수 있다 —
	// 실패하면 다음으로 가까운 후보로 넘어가 실제로 뚫리는 시작점을 찾는다.
	for (AFactoryNavWaypoint* Candidate : Candidates)
	{
		if (FindPath(Candidate, Target, RequestingAgent, OutRoute, bIgnoreOccupancy))
		{
			return true;
		}

		// 버그 수정(사용자 지시) — 방금 실패가 "점유 경합 때문"인지 "설계상 진짜 막다른 길"인지 구분한다.
		// 이미 점유를 무시하는 중(bIgnoreOccupancy — 재귀적으로 넘어온 순수 위상 확인 모드)이면 이 구분
		// 자체가 의미 없으니 그냥 다음 후보로. 아니라면 점유만 무시하고 한 번 더 확인 — 그래도 실패하면
		// 진짜 막다른 길이니 다음 후보로 넘어가고, 성공하면(점유만 아니었다면 뚫렸을 길) 더 가까운 이
		// 후보를 포기하지 않고 정직하게 실패를 반환한다 — 호출부의 기존 재시도가 매번 이 후보를 다시
		// 1순위로 시도하므로, 결과적으로 "이 후보의 점유가 풀릴 때까지 계속 기다림"이 된다.
		if (!bIgnoreOccupancy)
		{
			TArray<AFactoryNavWaypoint*> StructuralCheckRoute;
			if (FindPath(Candidate, Target, RequestingAgent, StructuralCheckRoute, /*bIgnoreOccupancy=*/true))
			{
				OutRoute.Reset();
				return false;
			}
		}
	}

	return false;
}

bool UFactoryWaypointNavigationSubsystem::FindPathToNearestMarkerWaypoint(const FVector& StartLocation, const FVector& MarkerLocation, AFactoryAgentBase* RequestingAgent, TArray<AFactoryNavWaypoint*>& OutRoute) const
{
	OutRoute.Reset();
	if (!RequestingAgent)
	{
		return false;
	}

	const EActorType AgentType = RequestingAgent->AgentType;

	TArray<AFactoryNavWaypoint*> Candidates;
	for (AFactoryNavWaypoint* Waypoint : AllWaypoints)
	{
		if (Waypoint && Waypoint->IsUsableBy(AgentType))
		{
			Candidates.Add(Waypoint);
		}
	}

	// 버그 수정(사용자 지시) — Common보다 전용 레인을 항상 먼저 시도한다(마커 코앞에서 공용 백본으로
	// 바로 붙는 걸 방지). 버그 수정(사용자 지시, 확정 재현) — 여기(MarkerLocation 기준 도착지 탐색)는
	// 언제나 "마커로 진입"하는 목적이라 Inbound를 우선해야 한다 — FindPathFromNearestReachable과
	// 대칭으로 GetDirectionalTier(false)를 써서 Inbound를 0순위로 강제한다(Outbound가 더 가까워도
	// 진입용으로 쓰지 않음).
	Candidates.Sort([&MarkerLocation](const AFactoryNavWaypoint& A, const AFactoryNavWaypoint& B)
	{
		const int32 TierA = A.GetDirectionalTier(false);
		const int32 TierB = B.GetDirectionalTier(false);
		if (TierA != TierB)
		{
			return TierA < TierB;
		}
		return FVector::DistSquared(A.GetActorLocation(), MarkerLocation) < FVector::DistSquared(B.GetActorLocation(), MarkerLocation);
	});

	// 마커에 가까운 순서대로 그래프 목표 후보를 시도해, 출발지에서 실제로 도달 가능한 것을 찾는다
	// (시작점 쪽 견고성은 FindPathFromNearestReachable을 그대로 재사용).
	for (AFactoryNavWaypoint* Candidate : Candidates)
	{
		if (FindPathFromNearestReachable(StartLocation, Candidate, RequestingAgent, OutRoute))
		{
			return true;
		}

		// 버그 수정(사용자 지시) — FindPathFromNearestReachable의 "점유 경합이면 다음 후보로 안 넘어가고
		// 기다림" 판단과 대칭. 이 마커측 후보가 점유만 아니었다면(위상적으로) StartLocation에서 도달
		// 가능했는지 확인 — 가능했으면 더 먼 마커측 후보로 넘어가지 않고 정직하게 실패를 반환해 이
		// 후보를 계속 기다린다. 진짜 도달 불가(설계상 끊긴 경우)일 때만 다음 후보로.
		TArray<AFactoryNavWaypoint*> StructuralCheckRoute;
		if (FindPathFromNearestReachable(StartLocation, Candidate, RequestingAgent, StructuralCheckRoute, /*bIgnoreOccupancy=*/true))
		{
			OutRoute.Reset();
			return false;
		}
	}

	return false;
}

bool UFactoryWaypointNavigationSubsystem::FindPath(AFactoryNavWaypoint* Start, AFactoryNavWaypoint* Target, AFactoryAgentBase* RequestingAgent, TArray<AFactoryNavWaypoint*>& OutRoute, bool bIgnoreOccupancy) const
{
	OutRoute.Reset();
	if (!Start || !Target || !RequestingAgent)
	{
		return false;
	}
	if (Start == Target)
	{
		OutRoute.Add(Start);
		return true;
	}

	const EActorType AgentType = RequestingAgent->AgentType;

	TMap<AFactoryNavWaypoint*, float> BestCost;
	TMap<AFactoryNavWaypoint*, AFactoryNavWaypoint*> CameFrom;
	TArray<AFactoryNavWaypoint*> Frontier;

	BestCost.Add(Start, 0.f);
	Frontier.Add(Start);

	while (Frontier.Num() > 0)
	{
		// 선형 탐색으로 최소 비용 노드 선택 — 웨이포인트 수가 공장 규모라 힙이 불필요할 만큼 적다.
		int32 BestIndex = 0;
		float BestNodeCost = BestCost[Frontier[0]];
		for (int32 Index = 1; Index < Frontier.Num(); ++Index)
		{
			const float Cost = BestCost[Frontier[Index]];
			if (Cost < BestNodeCost)
			{
				BestNodeCost = Cost;
				BestIndex = Index;
			}
		}

		AFactoryNavWaypoint* Current = Frontier[BestIndex];
		Frontier.RemoveAtSwap(BestIndex);

		if (Current == Target)
		{
			break;
		}

		for (const TObjectPtr<AFactoryNavWaypoint>& NeighborPtr : Current->ConnectedWaypoints)
		{
			AFactoryNavWaypoint* Neighbor = NeighborPtr;
			if (!Neighbor || !Neighbor->IsUsableBy(AgentType))
			{
				continue;
			}
			if (!bIgnoreOccupancy && Neighbor->IsOccupied() && !Neighbor->IsOccupiedBy(RequestingAgent))
			{
				continue;
			}

			const float NewCost = BestCost[Current] + FVector::Dist(Current->GetActorLocation(), Neighbor->GetActorLocation());
			const float* ExistingCost = BestCost.Find(Neighbor);
			if (!ExistingCost || NewCost < *ExistingCost)
			{
				BestCost.Add(Neighbor, NewCost);
				CameFrom.Add(Neighbor, Current);
				Frontier.AddUnique(Neighbor);
			}
		}
	}

	if (!CameFrom.Contains(Target))
	{
		// 버그 수정(2차) — 실패한 노드마다 도달 가능했던 전체 노드를 비용과 함께 문자열로 join해 한 줄
		// 로그로 남기던 방식이, 혼잡으로 실패가 잦아지는 시점(점유로 막힌 이웃이 많아짐)에 한 프레임
		// 안에서 여러 번 겹쳐 발생하며 대형 문자열 조립+로그 출력 비용으로 에디터가 순간적으로 멈췄다.
		// 그래프가 실제로 끊긴 경우는 도달 노드 수만으로도 이상 신호를 알 수 있어(전체 대비 지나치게
		// 적으면 끊긴 것) 개수만 남긴다.
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Nav] FindPath 실패 — %s에서 %s까지 경로 없음(도달 가능 노드 %d개)"),
			*Start->GetName(), *Target->GetName(), BestCost.Num());
		return false;
	}

	TArray<AFactoryNavWaypoint*> Reversed;
	AFactoryNavWaypoint* Walker = Target;
	Reversed.Add(Walker);
	while (Walker != Start)
	{
		AFactoryNavWaypoint** Prev = CameFrom.Find(Walker);
		if (!Prev)
		{
			return false;
		}
		Walker = *Prev;
		Reversed.Add(Walker);
	}

	OutRoute.Reserve(Reversed.Num());
	for (int32 Index = Reversed.Num() - 1; Index >= 0; --Index)
	{
		OutRoute.Add(Reversed[Index]);
	}
	return true;
}
