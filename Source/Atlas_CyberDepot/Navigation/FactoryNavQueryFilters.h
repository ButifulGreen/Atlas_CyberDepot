// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "FactoryNavQueryFilters.generated.h"

// Docs/08_Navigation.md §8 — 3단계 대상. 로봇: MainLane 선호/SideSpace 기피. NPC: 전 구역 동일 코스트.
// 정적 페인팅 값(DefaultCost)만 쓰는 고정 코스트 필터 — 런타임 코스트 조정(동적 혼잡 코스트)은 없다.

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
