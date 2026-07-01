// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/CostZoneVolume.h"
#include "Components/BoxComponent.h"

ACostZoneVolume::ACostZoneVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	BoundsComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsComponent"));
	BoundsComponent->SetBoxExtent(FVector(200.f, 200.f, 100.f));
	BoundsComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = BoundsComponent;
}

void ACostZoneVolume::RegisterBlocker(AActor* Blocker)
{
	if (!Blocker)
	{
		return;
	}

	++BlockerCount;
	LastChangeTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : LastChangeTimestamp;
	// 블로커 1개당 코스트 배수 1씩 선형 증가 (문서에 정확한 공식 명시 없어 임의 채택)
	CongestionCostMultiplier = 1.f + static_cast<float>(BlockerCount);
}

void ACostZoneVolume::UnregisterBlocker(AActor* Blocker)
{
	if (!Blocker || BlockerCount <= 0)
	{
		return;
	}

	--BlockerCount;
	LastChangeTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : LastChangeTimestamp;

	if (BlockerCount > 0)
	{
		CongestionCostMultiplier = 1.f + static_cast<float>(BlockerCount);
	}
	// BlockerCount == 0이면 TickPendingReset이 MinHoldTimeSeconds 경과 후 1.f로 되돌린다 (히스테리시스)
}

void ACostZoneVolume::TickPendingReset(float CurrentTime)
{
	if (BlockerCount == 0 && CongestionCostMultiplier > 1.f && (CurrentTime - LastChangeTimestamp) >= MinHoldTimeSeconds)
	{
		CongestionCostMultiplier = 1.f;
	}
}

float ACostZoneVolume::GetCurrentCostMultiplier() const
{
	return CongestionCostMultiplier;
}
