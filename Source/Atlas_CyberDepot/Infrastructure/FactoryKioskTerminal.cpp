// Copyright Epic Games, Inc. All Rights Reserved.

#include "Infrastructure/FactoryKioskTerminal.h"
#include "Components/BoxComponent.h"

AFactoryKioskTerminal::AFactoryKioskTerminal()
{
	InteractCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractCollision"));
	InteractCollision->InitBoxExtent(FVector(50.f, 50.f, 100.f));
	SetRootComponent(InteractCollision);
}
