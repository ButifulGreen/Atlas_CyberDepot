// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NavAreas/NavArea.h"
#include "FactoryNavAreas.generated.h"

// Docs/08_Navigation.md §8 — 3단계 대상. DefaultCost만 다른 3종 NavArea.
// 런타임에 이 클래스 자체를 교체하지 않는다(00_DesignPrinciples.md) — 코스트 조정은 NavQueryFilter로만 처리.

UCLASS()
class UNavArea_MainLane : public UNavArea
{
	GENERATED_BODY()
public:
	UNavArea_MainLane(const FObjectInitializer& ObjectInitializer);
};

UCLASS()
class UNavArea_SideSpace : public UNavArea
{
	GENERATED_BODY()
public:
	UNavArea_SideSpace(const FObjectInitializer& ObjectInitializer);
};

UCLASS()
class UNavArea_Critical : public UNavArea
{
	GENERATED_BODY()
public:
	UNavArea_Critical(const FObjectInitializer& ObjectInitializer);
};
