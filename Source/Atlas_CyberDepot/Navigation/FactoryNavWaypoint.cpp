// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/FactoryNavWaypoint.h"
#include "Agent/FactoryAgentBase.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"

AFactoryNavWaypoint::AFactoryNavWaypoint()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AFactoryNavWaypoint::BeginPlay()
{
	Super::BeginPlay();

	GetWorldTimerManager().SetTimer(DebugDrawTimerHandle, this, &AFactoryNavWaypoint::DrawDebugVisualization, DebugDrawIntervalSeconds, true);
}

bool AFactoryNavWaypoint::TryReserve(AFactoryAgentBase* Agent)
{
	if (!Agent)
	{
		return false;
	}
	if (Occupant.IsValid() && Occupant.Get() != Agent)
	{
		return false;
	}
	Occupant = Agent;
	return true;
}

void AFactoryNavWaypoint::Release(AFactoryAgentBase* Agent)
{
	if (Occupant.Get() == Agent)
	{
		Occupant = nullptr;
	}
}

bool AFactoryNavWaypoint::IsOccupied() const
{
	return Occupant.IsValid();
}

bool AFactoryNavWaypoint::IsOccupiedBy(const AFactoryAgentBase* Agent) const
{
	return Occupant.Get() == Agent;
}

bool AFactoryNavWaypoint::IsUsableBy(EActorType AgentType) const
{
	return (AllowedAgentTypes & (1 << static_cast<int32>(AgentType))) != 0;
}

bool AFactoryNavWaypoint::HasAccessFlag(EWaypointAccess Flag) const
{
	return (AccessFlags & (1 << static_cast<int32>(Flag))) != 0;
}

bool AFactoryNavWaypoint::IsPureCommon() const
{
	constexpr int32 SpecializedMask =
		(1 << static_cast<int32>(EWaypointAccess::Inbound)) |
		(1 << static_cast<int32>(EWaypointAccess::Outbound)) |
		(1 << static_cast<int32>(EWaypointAccess::Waitbound));
	return (AccessFlags & SpecializedMask) == 0;
}

int32 AFactoryNavWaypoint::GetDirectionalTier(bool bWantOutbound) const
{
	const bool bMatchesDirection = HasAccessFlag(bWantOutbound ? EWaypointAccess::Outbound : EWaypointAccess::Inbound);
	if (bMatchesDirection)
	{
		return 0;
	}
	if (!IsPureCommon())
	{
		return 1;
	}
	return 2;
}

void AFactoryNavWaypoint::DrawDebugVisualization()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 버그 수정(사용자 지시) — 여러 플래그가 동시에 켜질 수 있어 switch 대신 우선순위 판정으로 바꿨다
	// (Waitbound > Inbound+Outbound 동시 > 단일). 색상은 디버그 표시 전용이라 게임플레이 로직에 영향 없음.
	FColor NodeColor = FColor::Black;
	const bool bInbound = HasAccessFlag(EWaypointAccess::Inbound);
	const bool bOutbound = HasAccessFlag(EWaypointAccess::Outbound);
	if (HasAccessFlag(EWaypointAccess::Waitbound))
	{
		NodeColor = FColor::Yellow;
	}
	else if (bInbound && bOutbound)
	{
		NodeColor = FColor::Purple;
	}
	else if (bInbound)
	{
		NodeColor = FColor::Blue;
	}
	else if (bOutbound)
	{
		NodeColor = FColor::Red;
	}

	if (IsOccupied())
	{
		NodeColor = FColor::Green;
	}

	const FVector Location = GetActorLocation();
	const float Lifetime = DebugDrawIntervalSeconds * 1.1f;
	DrawDebugSphere(World, Location, 30.f, 8, NodeColor, false, Lifetime);

	for (const TObjectPtr<AFactoryNavWaypoint>& Neighbor : ConnectedWaypoints)
	{
		if (Neighbor)
		{
			DrawDebugDirectionalArrow(World, Location, Neighbor->GetActorLocation(), 60.f, NodeColor, false, Lifetime, 0, 3.f);
		}
	}
}
