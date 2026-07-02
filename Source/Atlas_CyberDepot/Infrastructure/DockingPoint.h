// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DockingPoint.generated.h"

class AFactoryAgentBase;

// Docs/06_Infrastructure.md §6 — 4단계 대상.
// 문서의 FGridIndex는 어디에도 정의돼 있지 않고, 물류센터는 층 구분이 없어(선반만 층 개념 존재)
// 09_Visualization.md에서 이미 쓰는 언리얼 내장 FIntPoint(X,Y)로 대체한다.
UCLASS()
class ADockingPoint : public AActor
{
	GENERATED_BODY()

public:
	ADockingPoint();

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FIntPoint GridIndex = FIntPoint::ZeroValue;

	UPROPERTY(Replicated, BlueprintReadOnly)
	bool bOccupied = false;

	UPROPERTY(Replicated, BlueprintReadOnly)
	TWeakObjectPtr<AFactoryAgentBase> OccupyingAgent;

	bool TryReserve(AFactoryAgentBase* Agent);
	void Release();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
