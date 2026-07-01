// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/FactoryNavAreas.h"

UNavArea_MainLane::UNavArea_MainLane(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	DefaultCost = 1.f;
}

UNavArea_SideSpace::UNavArea_SideSpace(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	DefaultCost = 10.f;
}

UNavArea_Critical::UNavArea_Critical(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	DefaultCost = 100.f;
}
