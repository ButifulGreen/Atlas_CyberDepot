// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/FactoryGameMode.h"
#include "Player/FactoryPlayerController.h"
#include "Player/FactorySpectatorPawn.h"
#include "Assignment/SmartFactoryManager.h"

AFactoryGameMode::AFactoryGameMode()
{
	DefaultPawnClass = AFactorySpectatorPawn::StaticClass();
	PlayerControllerClass = AFactoryPlayerController::StaticClass();
	GameStateClass = AMSmartFactoryManager::StaticClass();
}
