// Copyright Epic Games, Inc. All Rights Reserved.

#include "Infrastructure/StorageShelf.h"
#include "Infrastructure/LogisticsItem.h"
#include "Agent/FactoryAgentBase.h"
#include "Net/UnrealNetwork.h"

AStorageShelf::AStorageShelf()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Slots.SetNum(NumFloors * NumSlotsPerFloor);
}

void AStorageShelf::BeginPlay()
{
	Super::BeginPlay();

	GetComponents<UStorageSlotMarkerComponent>(SlotMarkers);
}

FTransform AStorageShelf::GetSlotMarkerTransform(int32 FloorIndex, int32 SlotIndex) const
{
	for (const UStorageSlotMarkerComponent* Marker : SlotMarkers)
	{
		if (Marker && Marker->FloorIndex == FloorIndex && Marker->SlotIndex == SlotIndex)
		{
			return Marker->GetComponentTransform();
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("AStorageShelf::GetSlotMarkerTransform: Floor %d Slot %d 마커를 찾지 못해 선반 자신의 트랜스폼으로 대체합니다 (%s)"), FloorIndex, SlotIndex, *GetName());
	return GetActorTransform();
}

FVector AStorageShelf::ComputeWorkLocation(const FVector& MarkerLocation, EWorkZoneType ZoneType, float DepthOffset) const
{
	const float Sign = (ZoneType == EWorkZoneType::ShelfInboundZone) ? 1.f : -1.f;
	return MarkerLocation + GetActorForwardVector() * (Sign * DepthOffset);
}

FVector AStorageShelf::GetAtlasWorkLocation(int32 FloorIndex, int32 SlotIndex, EWorkZoneType ZoneType) const
{
	return ComputeWorkLocation(GetSlotMarkerTransform(FloorIndex, SlotIndex).GetLocation(), ZoneType, AtlasWorkDistance);
}

FVector AStorageShelf::GetTransportRobotWorkLocation(int32 FloorIndex, int32 SlotIndex, EWorkZoneType ZoneType) const
{
	return ComputeWorkLocation(GetSlotMarkerTransform(FloorIndex, SlotIndex).GetLocation(), ZoneType, TransportRobotWorkDistance);
}

int32 AStorageShelf::ToSlotArrayIndex(int32 FloorIndex, int32 SlotIndex)
{
	return FloorIndex * NumSlotsPerFloor + SlotIndex;
}

bool AStorageShelf::TryReserveInboundZone(AFactoryAgentBase* Atlas)
{
	if (!Atlas || InboundZoneOccupant.IsValid())
	{
		return false;
	}

	InboundZoneOccupant = Atlas;
	return true;
}

void AStorageShelf::ReleaseInboundZone()
{
	InboundZoneOccupant.Reset();
}

bool AStorageShelf::TryReserveOutboundZone(AFactoryAgentBase* Atlas)
{
	if (!Atlas || OutboundZoneOccupant.IsValid())
	{
		return false;
	}

	OutboundZoneOccupant = Atlas;
	return true;
}

void AStorageShelf::ReleaseOutboundZone()
{
	OutboundZoneOccupant.Reset();
}

bool AStorageShelf::TryReserveEmptySlot(int32& OutFloorIndex, int32& OutSlotIndex)
{
	for (int32 FloorIndex = 0; FloorIndex < NumFloors; ++FloorIndex)
	{
		for (int32 SlotIndex = 0; SlotIndex < NumSlotsPerFloor; ++SlotIndex)
		{
			FShelfSlot& Slot = Slots[ToSlotArrayIndex(FloorIndex, SlotIndex)];
			if (!Slot.OccupyingItem.IsValid() && !Slot.bReservedForInbound && !Slot.bReservedForOutbound)
			{
				Slot.bReservedForInbound = true;
				OutFloorIndex = FloorIndex;
				OutSlotIndex = SlotIndex;
				return true;
			}
		}
	}

	return false;
}

bool AStorageShelf::TryReserveOldestOccupiedSlot(int32& OutFloorIndex, int32& OutSlotIndex, ALogisticsItem*& OutItem)
{
	int32 BestIndex = INDEX_NONE;
	FDateTime OldestTimestamp = FDateTime::MaxValue();

	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FShelfSlot& Slot = Slots[Index];
		if (Slot.OccupyingItem.IsValid() && !Slot.bReservedForOutbound && Slot.EnteredTimestamp < OldestTimestamp)
		{
			OldestTimestamp = Slot.EnteredTimestamp;
			BestIndex = Index;
		}
	}

	if (BestIndex == INDEX_NONE)
	{
		return false;
	}

	Slots[BestIndex].bReservedForOutbound = true;
	OutFloorIndex = BestIndex / NumSlotsPerFloor;
	OutSlotIndex = BestIndex % NumSlotsPerFloor;
	OutItem = Slots[BestIndex].OccupyingItem.Get();
	return true;
}

void AStorageShelf::ConfirmInbound(int32 FloorIndex, int32 SlotIndex, ALogisticsItem* Item)
{
	FShelfSlot& Slot = Slots[ToSlotArrayIndex(FloorIndex, SlotIndex)];
	Slot.OccupyingItem = Item;
	Slot.EnteredTimestamp = FDateTime::UtcNow();
	Slot.bReservedForInbound = false;
}

void AStorageShelf::ConfirmOutboundRemoved(int32 FloorIndex, int32 SlotIndex)
{
	FShelfSlot& Slot = Slots[ToSlotArrayIndex(FloorIndex, SlotIndex)];
	Slot.OccupyingItem.Reset();
	Slot.bReservedForOutbound = false;
}

ALogisticsItem* AStorageShelf::GetItemAt(int32 FloorIndex, int32 SlotIndex) const
{
	const int32 ArrayIndex = ToSlotArrayIndex(FloorIndex, SlotIndex);
	return Slots.IsValidIndex(ArrayIndex) ? Slots[ArrayIndex].OccupyingItem.Get() : nullptr;
}

bool AStorageShelf::IsFull() const
{
	return GetOccupiedCount() >= Slots.Num();
}

int32 AStorageShelf::GetOccupiedCount() const
{
	int32 Count = 0;
	for (const FShelfSlot& Slot : Slots)
	{
		if (Slot.OccupyingItem.IsValid())
		{
			++Count;
		}
	}
	return Count;
}

void AStorageShelf::TransferZoneOccupancy(EWorkZoneType ZoneType, AFactoryAgentBase* From, AFactoryAgentBase* To)
{
	TWeakObjectPtr<AFactoryAgentBase>* TargetOccupant = nullptr;
	switch (ZoneType)
	{
	case EWorkZoneType::ShelfInboundZone:
		TargetOccupant = &InboundZoneOccupant;
		break;
	case EWorkZoneType::ShelfOutboundZone:
		TargetOccupant = &OutboundZoneOccupant;
		break;
	default:
		return;
	}

	if (TargetOccupant->Get() == From)
	{
		*TargetOccupant = To;
	}
}

void AStorageShelf::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AStorageShelf, Slots);
	DOREPLIFETIME(AStorageShelf, InboundZoneOccupant);
	DOREPLIFETIME(AStorageShelf, OutboundZoneOccupant);
}
