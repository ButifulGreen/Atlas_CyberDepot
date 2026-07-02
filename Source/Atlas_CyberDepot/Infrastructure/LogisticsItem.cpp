// Copyright Epic Games, Inc. All Rights Reserved.

#include "Infrastructure/LogisticsItem.h"
#include "Net/UnrealNetwork.h"

ALogisticsItem::ALogisticsItem()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
}

void ALogisticsItem::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (!ItemID.IsValid())
		{
			ItemID = FGuid::NewGuid();
		}
		CreatedTimestamp = FDateTime::UtcNow();
	}
}

float ALogisticsItem::GetAgeSeconds() const
{
	return static_cast<float>((FDateTime::UtcNow() - CreatedTimestamp).GetTotalSeconds());
}

void ALogisticsItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ALogisticsItem, ItemID);
	DOREPLIFETIME(ALogisticsItem, ItemType);
}
