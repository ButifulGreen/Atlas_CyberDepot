// Copyright Epic Games, Inc. All Rights Reserved.

#include "Infrastructure/DockingPoint.h"
#include "Agent/FactoryAgentBase.h"
#include "Net/UnrealNetwork.h"

ADockingPoint::ADockingPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
}

bool ADockingPoint::TryReserve(AFactoryAgentBase* Agent)
{
	if (!Agent || bOccupied)
	{
		return false;
	}

	bOccupied = true;
	OccupyingAgent = Agent;
	return true;
}

void ADockingPoint::Release()
{
	bOccupied = false;
	OccupyingAgent.Reset();
}

void ADockingPoint::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADockingPoint, bOccupied);
	DOREPLIFETIME(ADockingPoint, OccupyingAgent);
}
