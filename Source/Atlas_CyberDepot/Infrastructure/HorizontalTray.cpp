// Copyright Epic Games, Inc. All Rights Reserved.

#include "Infrastructure/HorizontalTray.h"
#include "Infrastructure/LogisticsItem.h"
#include "Agent/FactoryAgentBase.h"
#include "Net/UnrealNetwork.h"

AHorizontalTray::AHorizontalTray()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
}

void AHorizontalTray::BeginPlay()
{
	Super::BeginPlay();
}

void AHorizontalTray::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TickConveyance(DeltaTime);
}

bool AHorizontalTray::TryReserveWorkZone(AFactoryAgentBase* Agent)
{
	if (!Agent || WorkZoneOccupant.IsValid())
	{
		return false;
	}

	WorkZoneOccupant = Agent;
	return true;
}

void AHorizontalTray::ReleaseWorkZone()
{
	WorkZoneOccupant.Reset();
}

void AHorizontalTray::OnItemSpawnedAtStart(ALogisticsItem* Item)
{
	if (!Item)
	{
		return;
	}

	Item->SetActorLocation(GetActorLocation());
	CurrentItem = Item;
	bIsHaltedAtEnd = false;
}

void AHorizontalTray::OnItemPlacedByAtlas(ALogisticsItem* Item)
{
	if (!Item)
	{
		return;
	}

	CurrentItem = Item;
	bIsHaltedAtEnd = false;
}

void AHorizontalTray::TickConveyance(float DeltaTime)
{
	if (!HasAuthority() || bIsHaltedAtEnd || !CurrentItem.IsValid())
	{
		return;
	}

	ALogisticsItem* Item = CurrentItem.Get();
	const FVector EndLocation = GetActorLocation() + GetActorForwardVector() * TrayLength;
	const FVector NewLocation = FMath::VInterpConstantTo(Item->GetActorLocation(), EndLocation, DeltaTime, ConveySpeedUnitsPerSecond);
	Item->SetActorLocation(NewLocation);

	if (FVector::DistSquared(NewLocation, EndLocation) <= KINDA_SMALL_NUMBER)
	{
		OnItemReachedEnd();
	}
}

void AHorizontalTray::OnItemReachedEnd()
{
	bIsHaltedAtEnd = true;
}

void AHorizontalTray::OnItemCleared()
{
	CurrentItem.Reset();
	bIsHaltedAtEnd = false;
}

void AHorizontalTray::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHorizontalTray, CurrentItem);
	DOREPLIFETIME(AHorizontalTray, bIsHaltedAtEnd);
	DOREPLIFETIME(AHorizontalTray, WorkZoneOccupant);
}
