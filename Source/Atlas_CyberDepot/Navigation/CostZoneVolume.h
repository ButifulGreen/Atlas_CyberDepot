// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CostZoneVolume.generated.h"

class UBoxComponent;
class UNavArea;

// Docs/08_Navigation.md §8 — 3단계 대상. 미리 배치되는 풀링 대상, 상태값만 들고 있고
// 실제 코스트 반영은 AFactoryAIController::ApplyDynamicCongestionCost가 수행한다.
UCLASS()
class ACostZoneVolume : public AActor
{
	GENERATED_BODY()

public:
	ACostZoneVolume();

	// Docs에는 없는 필드: 이 존의 혼잡도 배수를 어떤 NavArea에 적용할지 레벨에서 지정
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<UNavArea> AffectedAreaClass;

	void RegisterBlocker(AActor* Blocker);
	void UnregisterBlocker(AActor* Blocker);
	void TickPendingReset(float CurrentTime);
	float GetCurrentCostMultiplier() const;

protected:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> BoundsComponent;

	UPROPERTY(BlueprintReadOnly)
	int32 BlockerCount = 0;

	UPROPERTY(BlueprintReadOnly)
	double LastChangeTimestamp = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MinHoldTimeSeconds = 0.5f;

	UPROPERTY(BlueprintReadOnly)
	float CongestionCostMultiplier = 1.f;
};
