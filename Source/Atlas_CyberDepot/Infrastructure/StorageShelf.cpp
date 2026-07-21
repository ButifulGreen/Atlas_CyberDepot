// Copyright Epic Games, Inc. All Rights Reserved.

#include "Infrastructure/StorageShelf.h"
#include "Atlas_CyberDepot.h"
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
	InboundStagingMarker = FindComponentByClass<UInboundStagingMarkerComponent>();
	OutboundStagingMarker = FindComponentByClass<UOutboundStagingMarkerComponent>();
}

FVector AStorageShelf::GetInboundStagingLocation() const
{
	if (InboundStagingMarker)
	{
		return InboundStagingMarker->GetComponentLocation();
	}

	UE_LOG(LogTemp, Warning, TEXT("AStorageShelf::GetInboundStagingLocation: InboundStagingMarker 컴포넌트가 없어 선반 자신의 위치로 대체합니다 (%s)"), *GetName());
	return GetActorLocation();
}

FVector AStorageShelf::GetOutboundStagingLocation() const
{
	if (OutboundStagingMarker)
	{
		return OutboundStagingMarker->GetComponentLocation();
	}

	UE_LOG(LogTemp, Warning, TEXT("AStorageShelf::GetOutboundStagingLocation: OutboundStagingMarker 컴포넌트가 없어 선반 자신의 위치로 대체합니다 (%s)"), *GetName());
	return GetActorLocation();
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
	// 버그 수정 — 이동 목적지는 층(Z)과 무관해야 한다. 1층이든 2층이든 3층이든 로봇은 같은 지상
	// 높이에 서고, IK(CurrentIKHandTarget, GetSlotMarkerTransform 그대로 사용)만 실제 층 높이까지 뻗는다.
	FVector GroundLocation = MarkerLocation;
	GroundLocation.Z = GetActorLocation().Z;

	const float Sign = (ZoneType == EWorkZoneType::ShelfInboundZone) ? 1.f : -1.f;
	return GroundLocation + GetActorForwardVector() * (Sign * DepthOffset);
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
	// 버그 수정 — 레벨의 UStorageSlotMarkerComponent가 1부터 시작하는 Floor/SlotIndex로 배치되어 있어
	// (0이 아님), 외부에 노출되는 Floor/SlotIndex는 전부 1-based로 통일하고 내부 배열 인덱스로 변환할 때만
	// 여기서 -1 보정한다.
	return (FloorIndex - 1) * NumSlotsPerFloor + (SlotIndex - 1);
}

bool AStorageShelf::TryReserveInboundZone(AFactoryAgentBase* Atlas)
{
	if (!Atlas)
	{
		return false;
	}

	if (InboundZoneOccupants.Num() >= MaxConcurrentAtlas)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Nav] %s InboundZone 예약 실패 — %s 만석(%d/%d)"),
			*Atlas->GetName(), *GetName(), InboundZoneOccupants.Num(), MaxConcurrentAtlas);
		return false;
	}

	InboundZoneOccupants.Add(Atlas);
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Nav] %s InboundZone 예약 성공 — %s(%d/%d)"),
		*Atlas->GetName(), *GetName(), InboundZoneOccupants.Num(), MaxConcurrentAtlas);
	return true;
}

void AStorageShelf::ReleaseInboundZone(AFactoryAgentBase* Atlas)
{
	InboundZoneOccupants.RemoveAll([Atlas](const TWeakObjectPtr<AFactoryAgentBase>& Occupant)
	{
		return !Occupant.IsValid() || Occupant.Get() == Atlas;
	});
}

bool AStorageShelf::TryReserveOutboundZone(AFactoryAgentBase* Atlas)
{
	if (!Atlas)
	{
		return false;
	}

	if (OutboundZoneOccupants.Num() >= MaxConcurrentAtlas)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Nav] %s OutboundZone 예약 실패 — %s 만석(%d/%d)"),
			*Atlas->GetName(), *GetName(), OutboundZoneOccupants.Num(), MaxConcurrentAtlas);
		return false;
	}

	OutboundZoneOccupants.Add(Atlas);
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Nav] %s OutboundZone 예약 성공 — %s(%d/%d)"),
		*Atlas->GetName(), *GetName(), OutboundZoneOccupants.Num(), MaxConcurrentAtlas);
	return true;
}

void AStorageShelf::ReleaseOutboundZone(AFactoryAgentBase* Atlas)
{
	OutboundZoneOccupants.RemoveAll([Atlas](const TWeakObjectPtr<AFactoryAgentBase>& Occupant)
	{
		return !Occupant.IsValid() || Occupant.Get() == Atlas;
	});
}

bool AStorageShelf::TryReserveEmptySlot(int32& OutFloorIndex, int32& OutSlotIndex)
{
	// FloorIndex/SlotIndex는 1-based(레벨 마커 컨벤션과 동일)로 순회한다.
	for (int32 FloorIndex = 1; FloorIndex <= NumFloors; ++FloorIndex)
	{
		for (int32 SlotIndex = 1; SlotIndex <= NumSlotsPerFloor; ++SlotIndex)
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
	// 0-based 배열 인덱스를 1-based FloorIndex/SlotIndex로 변환(ToSlotArrayIndex와 대칭).
	OutFloorIndex = BestIndex / NumSlotsPerFloor + 1;
	OutSlotIndex = BestIndex % NumSlotsPerFloor + 1;
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

void AStorageShelf::ReleaseSlotReservation(int32 FloorIndex, int32 SlotIndex, bool bWasInbound)
{
	FShelfSlot& Slot = Slots[ToSlotArrayIndex(FloorIndex, SlotIndex)];
	if (bWasInbound)
	{
		Slot.bReservedForInbound = false;
	}
	else
	{
		Slot.bReservedForOutbound = false;
	}
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
	TArray<TWeakObjectPtr<AFactoryAgentBase>>* TargetOccupants = nullptr;
	switch (ZoneType)
	{
	case EWorkZoneType::ShelfInboundZone:
		TargetOccupants = &InboundZoneOccupants;
		break;
	case EWorkZoneType::ShelfOutboundZone:
		TargetOccupants = &OutboundZoneOccupants;
		break;
	default:
		return;
	}

	for (TWeakObjectPtr<AFactoryAgentBase>& Occupant : *TargetOccupants)
	{
		if (Occupant.Get() == From)
		{
			Occupant = To;
			return;
		}
	}
}

bool AStorageShelf::IsZoneFull(EWorkZoneType ZoneType) const
{
	const int32 OccupantCount = (ZoneType == EWorkZoneType::ShelfInboundZone)
		? InboundZoneOccupants.Num()
		: OutboundZoneOccupants.Num();
	return OccupantCount >= MaxConcurrentAtlas;
}

void AStorageShelf::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AStorageShelf, Slots);
	DOREPLIFETIME(AStorageShelf, InboundZoneOccupants);
	DOREPLIFETIME(AStorageShelf, OutboundZoneOccupants);
}
