// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "FactoryNavQueryFilters.generated.h"

// Docs/08_Navigation.md §8 — 3단계 대상. 로봇: MainLane 선호/SideSpace 기피. NPC: 전 구역 동일 코스트.
// bInstantiateForQuerier = true로 설정해 AFactoryAIController::ApplyDynamicCongestionCost가
// 매 이동 요청 직전 CDO의 Areas를 고쳐도 그 값이 캐싱 없이 매번 새로 반영되게 한다.

UCLASS()
class UNavQueryFilter_Robot : public UNavigationQueryFilter
{
	GENERATED_BODY()
public:
	UNavQueryFilter_Robot(const FObjectInitializer& ObjectInitializer);
};

UCLASS()
class UNavQueryFilter_NPC : public UNavigationQueryFilter
{
	GENERATED_BODY()
public:
	UNavQueryFilter_NPC(const FObjectInitializer& ObjectInitializer);
};
