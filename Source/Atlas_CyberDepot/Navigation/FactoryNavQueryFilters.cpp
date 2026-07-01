// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/FactoryNavQueryFilters.h"
#include "Navigation/FactoryNavAreas.h"

UNavQueryFilter_Robot::UNavQueryFilter_Robot(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bInstantiateForQuerier = true;
	// MainLane(1) 선호, SideSpace(10) 기피는 각 NavArea 클래스의 DefaultCost를 그대로 사용해서 이미 반영됨
}

UNavQueryFilter_NPC::UNavQueryFilter_NPC(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bInstantiateForQuerier = true;
	AddTravelCostOverride(UNavArea_MainLane::StaticClass(), 1.f);
	AddTravelCostOverride(UNavArea_SideSpace::StaticClass(), 1.f);
	AddTravelCostOverride(UNavArea_Critical::StaticClass(), 1.f);
}
