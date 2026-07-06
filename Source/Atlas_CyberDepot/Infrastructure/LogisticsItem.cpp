// Copyright Epic Games, Inc. All Rights Reserved.

#include "Infrastructure/LogisticsItem.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Assignment/SmartFactoryManager.h"
#include "Net/UnrealNetwork.h"

ALogisticsItem::ALogisticsItem()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	ItemRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ItemRoot"));
	RootComponent = ItemRoot;

	MeshItemA = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshItemA"));
	MeshItemA->SetupAttachment(ItemRoot);

	MeshItemB = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshItemB"));
	MeshItemB->SetupAttachment(ItemRoot);

	MeshItemC = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshItemC"));
	MeshItemC->SetupAttachment(ItemRoot);
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

	UpdateItemMesh();
}

void ALogisticsItem::OnRep_ItemType()
{
	UpdateItemMesh();
}

void ALogisticsItem::UpdateItemMesh()
{
	// 어떤 슬롯(0/1/2)이 이 ItemType을 대표하는지는 AMSmartFactoryManager가 세션 시작 시 셔플해서 정한다.
	// GameState가 아직 없거나 매핑을 못 받은 초기 순간엔 ItemType 값 그대로를 슬롯 번호로 대체 사용한다.
	uint8 MeshSlot = static_cast<uint8>(ItemType);
	if (const AMSmartFactoryManager* Manager = GetWorld() ? GetWorld()->GetGameState<AMSmartFactoryManager>() : nullptr)
	{
		if (Manager->ItemTypeToMeshSlot.IsValidIndex(static_cast<int32>(ItemType)))
		{
			MeshSlot = Manager->ItemTypeToMeshSlot[static_cast<int32>(ItemType)];
		}
	}

	MeshItemA->SetVisibility(MeshSlot == 0);
	MeshItemB->SetVisibility(MeshSlot == 1);
	MeshItemC->SetVisibility(MeshSlot == 2);
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
