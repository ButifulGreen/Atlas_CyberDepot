// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "FactoryGameMode.generated.h"

// Docs/02_Multiplayer_RPC.md §2 — 8단계. AMSmartFactoryManager를 GameStateClass로 처음 연결한다.
UCLASS()
class AFactoryGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	AFactoryGameMode();
};
