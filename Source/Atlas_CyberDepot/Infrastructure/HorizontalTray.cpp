// Copyright Epic Games, Inc. All Rights Reserved.

#include "Infrastructure/HorizontalTray.h"
#include "Infrastructure/LogisticsItem.h"
#include "Agent/FactoryAgentBase.h"
#include "Net/UnrealNetwork.h"

AHorizontalTray::AHorizontalTray()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	USceneComponent* TrayRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TrayRoot"));
	RootComponent = TrayRoot;

	ItemStartMarker = CreateDefaultSubobject<USceneComponent>(TEXT("ItemStartMarker"));
	ItemStartMarker->SetupAttachment(TrayRoot);

	ItemEndMarker = CreateDefaultSubobject<USceneComponent>(TEXT("ItemEndMarker"));
	ItemEndMarker->SetupAttachment(TrayRoot);
}

void AHorizontalTray::BeginPlay()
{
	Super::BeginPlay();
}

FVector AHorizontalTray::ComputeWorkLocation(float DepthOffset) const
{
	// 버그 수정 — 아틀라스/배송로봇이 실제로 물품과 상호작용하는 지점은 항상 ItemEndMarker다
	// (Inbound는 여기서 집고, Outbound는 여기에 놓는다 — OnItemPlacedByAtlas 참고).
	return ItemEndMarker->GetComponentLocation() + GetActorForwardVector() * DepthOffset;
}

FVector AHorizontalTray::GetAtlasWorkLocation() const
{
	return ComputeWorkLocation(AtlasWorkDistance);
}

FVector AHorizontalTray::GetTransportRobotWorkLocation() const
{
	return ComputeWorkLocation(TransportRobotWorkDistance);
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

	Item->SetActorLocation(ItemStartMarker->GetComponentLocation());
	CurrentItem = Item;
	bIsHaltedAtEnd = false;
}

void AHorizontalTray::OnItemPlacedByAtlas(ALogisticsItem* Item)
{
	if (!Item)
	{
		return;
	}

	// Outbound 전용 — 로봇 손에서 분리된 자리 그대로 두지 않고 컨베이어 종점(End)에 명시적으로 스냅한다.
	Item->SetActorLocation(ItemEndMarker->GetComponentLocation());
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

	// Inbound는 Start에서 생성돼 End로, Outbound는 End에서 놓여 Start로 — 서로 반대 방향으로 흘러간다.
	const FVector TargetLocation = (Direction == ETrayDirection::Inbound)
		? ItemEndMarker->GetComponentLocation()
		: ItemStartMarker->GetComponentLocation();

	const FVector NewLocation = FMath::VInterpConstantTo(Item->GetActorLocation(), TargetLocation, DeltaTime, ConveySpeedUnitsPerSecond);
	Item->SetActorLocation(NewLocation);

	if (FVector::DistSquared(NewLocation, TargetLocation) <= KINDA_SMALL_NUMBER)
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
