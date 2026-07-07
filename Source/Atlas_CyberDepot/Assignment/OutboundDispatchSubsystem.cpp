// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assignment/OutboundDispatchSubsystem.h"
#include "Assignment/DeliveryOrderSubsystem.h"
#include "Agent/FactoryAtlasRobot.h"
#include "Agent/FactoryTransportRobot.h"
#include "Agent/FactoryAIController.h"
#include "Infrastructure/StorageShelf.h"
#include "EventBus/FactoryEventBusSubsystem.h"
#include "Kismet/GameplayStatics.h"

AStorageShelf* UOutboundDispatchSubsystem::FindShelfForItemType(EItemType ItemType) const
{
	TArray<AActor*> FoundShelves;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AStorageShelf::StaticClass(), FoundShelves);

	for (AActor* Actor : FoundShelves)
	{
		if (AStorageShelf* Shelf = Cast<AStorageShelf>(Actor))
		{
			if (Shelf->BoundItemType == ItemType)
			{
				return Shelf;
			}
		}
	}

	return nullptr;
}

AHorizontalTray* UOutboundDispatchSubsystem::FindTrayForItemType(EItemType ItemType, ETrayDirection Direction) const
{
	TArray<AActor*> FoundTrays;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AHorizontalTray::StaticClass(), FoundTrays);

	for (AActor* Actor : FoundTrays)
	{
		if (AHorizontalTray* Tray = Cast<AHorizontalTray>(Actor))
		{
			if (Tray->Direction == Direction && Tray->BoundItemType == ItemType)
			{
				return Tray;
			}
		}
	}

	return nullptr;
}

void UOutboundDispatchSubsystem::DecomposeOrder(const FDeliveryOrder& Order)
{
	for (const TPair<EItemType, int32>& Pair : Order.RequestedQuantities)
	{
		if (Pair.Value <= 0)
		{
			continue;
		}

		AStorageShelf* Shelf = FindShelfForItemType(Pair.Key);
		if (!Shelf)
		{
			continue;
		}

		// 아틀라스가 선반에서 인출한 물품을 배송로봇에게 넘기면, 그 배송로봇이 향할 Outbound 트레이 쪽에도
		// 별도의 TrayWorkZone 배정(같은 아틀라스가 담당)이 있어야 최종 적재가 된다. 버그 수정 — 원래 이 확인이
		// 선반 배정을 이미 만든 뒤에 있어서, 트레이를 못 찾으면 선반 배정만 덩그러니 남는 문제가 있었다.
		AHorizontalTray* Tray = FindTrayForItemType(Pair.Key, ETrayDirection::Outbound);
		if (!Tray)
		{
			continue;
		}

		FStationAssignment Assignment;
		Assignment.AssignmentID = FGuid::NewGuid();
		Assignment.SourceOrderID = Order.OrderID;
		Assignment.ZoneType = EWorkZoneType::ShelfOutboundZone;
		Assignment.TargetZoneOwner = Shelf;

		FStationAssignment TrayAssignment;
		TrayAssignment.AssignmentID = FGuid::NewGuid();
		TrayAssignment.SourceOrderID = Order.OrderID;
		TrayAssignment.ZoneType = EWorkZoneType::TrayWorkZone;
		TrayAssignment.TargetZoneOwner = Tray;

		// 버그 수정 — 슬롯을 아틀라스가 나중에 즉흥적으로 정하지 않고, 여기서 수량만큼 미리 예약해
		// 아틀라스 배정(ReservedSlots)과 배송로봇의 개별 트립(FloorIndex/SlotIndex) 양쪽에 같이 싣는다.
		// 배송로봇은 짐을 1개씩만 나르므로 트립도 수량만큼 개별 생성. 실제 재고가 요청보다 적으면
		// 예약된 만큼만 진행한다. 버그 수정 — 아틀라스가 배송로봇을 거리로 추정 매칭하지 않도록, Shelf/Tray
		// 두 배정의 ReservedSlots에 같은 TripTaskID(=이 트립의 FTransportTask::TaskID)를 함께 싣는다.
		int32 ReservedCount = 0;
		for (int32 TripIndex = 0; TripIndex < Pair.Value; ++TripIndex)
		{
			int32 FloorIndex = 0;
			int32 SlotIndex = 0;
			ALogisticsItem* Item = nullptr;
			if (!Shelf->TryReserveOldestOccupiedSlot(FloorIndex, SlotIndex, Item))
			{
				break;
			}

			FTransportTask Task;
			Task.TaskID = FGuid::NewGuid();
			Task.SourceOrderID = Order.OrderID;
			Task.PickupPoint = Shelf;
			Task.DropoffPoint = Tray;
			Task.ItemType = Pair.Key;
			Task.FloorIndex = FloorIndex;
			Task.SlotIndex = SlotIndex;
			PendingTransportTasks.Add(Task);

			FReservedSlotEntry ShelfSlotEntry;
			ShelfSlotEntry.SlotCoord = FIntPoint(FloorIndex, SlotIndex);
			ShelfSlotEntry.TripTaskID = Task.TaskID;
			Assignment.ReservedSlots.Add(ShelfSlotEntry);

			FReservedSlotEntry TraySlotEntry;
			TraySlotEntry.TripTaskID = Task.TaskID;
			TrayAssignment.ReservedSlots.Add(TraySlotEntry);

			++ReservedCount;
		}

		if (ReservedCount <= 0)
		{
			continue;
		}

		Assignment.RemainingCount = ReservedCount;
		ActiveStationAssignments.Add(Assignment);

		TrayAssignment.RemainingCount = ReservedCount;
		ActiveStationAssignments.Add(TrayAssignment);
	}

	TryDispatchIdleAgents();
}

void UOutboundDispatchSubsystem::TryDispatchIdleAgents()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> FoundAtlases;
	UGameplayStatics::GetAllActorsOfClass(World, AFactoryAtlasRobot::StaticClass(), FoundAtlases);
	for (AActor* Actor : FoundAtlases)
	{
		AFactoryAtlasRobot* Atlas = Cast<AFactoryAtlasRobot>(Actor);
		if (Atlas && Atlas->CurrentState == EAgentState::Idle)
		{
			FStationAssignment Assignment;
			TryAssignIdleAtlas(Atlas, Assignment);
		}
	}

	TArray<AActor*> FoundRobots;
	UGameplayStatics::GetAllActorsOfClass(World, AFactoryTransportRobot::StaticClass(), FoundRobots);
	for (AActor* Actor : FoundRobots)
	{
		AFactoryTransportRobot* Robot = Cast<AFactoryTransportRobot>(Actor);
		if (Robot && Robot->CurrentState == EAgentState::Idle)
		{
			FTransportTask Task;
			TryAssignIdleTransportRobot(Robot, Task);
		}
	}
}

void UOutboundDispatchSubsystem::EnqueueInboundWork(EItemType ItemType, AHorizontalTray* Tray, AStorageShelf* Shelf)
{
	if (!Tray || !Shelf)
	{
		return;
	}

	// 버그 수정 — 슬롯을 아틀라스가 나중에 즉흥적으로 정하지 않고, 여기서 미리 예약해
	// 아틀라스 배정과 배송로봇 트립 양쪽에 같이 싣는다(둘 다 같은 슬롯 위치로 직접 이동).
	int32 FloorIndex = 0;
	int32 SlotIndex = 0;
	if (!Shelf->TryReserveEmptySlot(FloorIndex, SlotIndex))
	{
		// 물리적으로 올릴 빈 슬롯이 없음 — 조용히 스킵(재고 포화는 Code:004로 별도 추적됨).
		return;
	}

	FTransportTask Task;
	Task.TaskID = FGuid::NewGuid();
	Task.PickupPoint = Tray;
	Task.DropoffPoint = Shelf;
	Task.ItemType = ItemType;
	Task.FloorIndex = FloorIndex;
	Task.SlotIndex = SlotIndex;
	PendingTransportTasks.Add(Task);

	// 버그 수정 — Shelf/Tray 양쪽 배정에 같은 TripTaskID(=Task.TaskID)를 실어 아틀라스가 거리 대신
	// 정확히 이 트립을 담당하는 배송로봇을 찾도록 한다.
	FStationAssignment TrayAssignment;
	TrayAssignment.AssignmentID = FGuid::NewGuid();
	TrayAssignment.ZoneType = EWorkZoneType::TrayWorkZone;
	TrayAssignment.TargetZoneOwner = Tray;
	TrayAssignment.RemainingCount = 1;
	FReservedSlotEntry TraySlotEntry;
	TraySlotEntry.TripTaskID = Task.TaskID;
	TrayAssignment.ReservedSlots.Add(TraySlotEntry);
	ActiveStationAssignments.Add(TrayAssignment);

	FStationAssignment ShelfAssignment;
	ShelfAssignment.AssignmentID = FGuid::NewGuid();
	ShelfAssignment.ZoneType = EWorkZoneType::ShelfInboundZone;
	ShelfAssignment.TargetZoneOwner = Shelf;
	ShelfAssignment.RemainingCount = 1;
	FReservedSlotEntry ShelfSlotEntry;
	ShelfSlotEntry.SlotCoord = FIntPoint(FloorIndex, SlotIndex);
	ShelfSlotEntry.TripTaskID = Task.TaskID;
	ShelfAssignment.ReservedSlots.Add(ShelfSlotEntry);
	ActiveStationAssignments.Add(ShelfAssignment);

	TryDispatchIdleAgents();
}

bool UOutboundDispatchSubsystem::TryCancelAssignmentsForOrder(const FGuid& OrderID)
{
	bool bHasMatch = false;
	for (const FStationAssignment& Assignment : ActiveStationAssignments)
	{
		if (Assignment.SourceOrderID != OrderID)
		{
			continue;
		}

		bHasMatch = true;
		if (Assignment.AssignedAtlas.IsValid())
		{
			return false;
		}
	}

	if (!bHasMatch)
	{
		// 해당 주문에서 파생된 작업이 없다(예: 대상 선반 없음으로 전부 스킵됨) — 취소할 것도 없으므로 성공 처리.
		return true;
	}

	ActiveStationAssignments.RemoveAll([&OrderID](const FStationAssignment& A)
	{
		return A.SourceOrderID == OrderID;
	});

	return true;
}

bool UOutboundDispatchSubsystem::IsZoneOccupied(EWorkZoneType ZoneType, AActor* TargetZoneOwner) const
{
	if (const AStorageShelf* Shelf = Cast<AStorageShelf>(TargetZoneOwner))
	{
		return (ZoneType == EWorkZoneType::ShelfInboundZone)
			? Shelf->InboundZoneOccupant.IsValid()
			: Shelf->OutboundZoneOccupant.IsValid();
	}

	if (const AHorizontalTray* Tray = Cast<AHorizontalTray>(TargetZoneOwner))
	{
		return Tray->WorkZoneOccupant.IsValid();
	}

	return false;
}

bool UOutboundDispatchSubsystem::TryAssignIdleAtlas(AFactoryAtlasRobot* Atlas, FStationAssignment& OutAssignment)
{
	if (!Atlas)
	{
		return false;
	}

	for (FStationAssignment& Assignment : ActiveStationAssignments)
	{
		if (Assignment.AssignedAtlas.IsValid() || Assignment.RemainingCount <= 0)
		{
			continue;
		}

		// 같은 선반/트레이를 겨냥한 다른 배정이 이미 그 존을 점유 중이면 이번엔 건너뛴다 —
		// StartCurrentAssignment의 TryReserve*Zone 실패로 배정이 미아가 되는 것을 방지.
		if (IsZoneOccupied(Assignment.ZoneType, Assignment.TargetZoneOwner.Get()))
		{
			continue;
		}

		Assignment.AssignedAtlas = Atlas;
		OutAssignment = Assignment;
		Atlas->AcceptStationAssignment(Assignment);
		return true;
	}

	return false;
}

bool UOutboundDispatchSubsystem::TryAssignIdleTransportRobot(AFactoryTransportRobot* Robot, FTransportTask& OutTask)
{
	if (!Robot || PendingTransportTasks.Num() == 0)
	{
		return false;
	}

	// 픽업 대기 물품이 있는 거점을 스캔해 작업을 구성하는 로직은 아직 없고,
	// 지금은 이미 채워져 있는 PendingTransportTasks 큐에서만 꺼내온다.
	OutTask = PendingTransportTasks[0];
	PendingTransportTasks.RemoveAt(0);
	Robot->AcceptTransportTask(OutTask);
	return true;
}

void UOutboundDispatchSubsystem::HandoffStationAssignment(const FGuid& AssignmentID, AFactoryAtlasRobot* From, AFactoryAtlasRobot* To)
{
	if (!From || !To)
	{
		return;
	}

	FStationAssignment* Assignment = ActiveStationAssignments.FindByPredicate([&AssignmentID](const FStationAssignment& A)
	{
		return A.AssignmentID == AssignmentID;
	});

	if (!Assignment)
	{
		return;
	}

	FPendingHandoff Handoff;
	Handoff.AssignmentID = AssignmentID;
	Handoff.From = From;
	Handoff.To = To;
	Handoff.ZoneType = Assignment->ZoneType;
	PendingHandoffs.Add(AssignmentID, Handoff);

	if (AStorageShelf* Shelf = Cast<AStorageShelf>(Assignment->TargetZoneOwner.Get()))
	{
		const FVector StagingLocation = (Assignment->ZoneType == EWorkZoneType::ShelfInboundZone)
			? Shelf->GetInboundStagingLocation()
			: Shelf->GetOutboundStagingLocation();

		if (AFactoryAIController* AIController = Cast<AFactoryAIController>(To->GetController()))
		{
			AIController->RequestMoveWithFilter(StagingLocation);
		}
	}
}

void UOutboundDispatchSubsystem::OnHandoffAtlasArrivedAtStagingPoint(const FGuid& AssignmentID)
{
	FPendingHandoff* Handoff = PendingHandoffs.Find(AssignmentID);
	if (!Handoff)
	{
		return;
	}

	FStationAssignment* Assignment = ActiveStationAssignments.FindByPredicate([&AssignmentID](const FStationAssignment& A)
	{
		return A.AssignmentID == AssignmentID;
	});

	if (Assignment)
	{
		if (AStorageShelf* Shelf = Cast<AStorageShelf>(Assignment->TargetZoneOwner.Get()))
		{
			Shelf->TransferZoneOccupancy(Assignment->ZoneType, Handoff->From.Get(), Handoff->To.Get());
		}

		Assignment->AssignedAtlas = Handoff->To;

		if (AFactoryAtlasRobot* To = Handoff->To.Get())
		{
			To->AcceptStationAssignment(*Assignment, true);
		}
	}

	// From을 대기실로 이동시키는 처리(대기실 탐색/슬롯 예약)는 배차 로직이 정교화되는 이후 단계에서 연결한다.
	PendingHandoffs.Remove(AssignmentID);
}

void UOutboundDispatchSubsystem::OnStationAssignmentCompleted(const FGuid& AssignmentID)
{
	const int32 Index = ActiveStationAssignments.IndexOfByPredicate([&AssignmentID](const FStationAssignment& A)
	{
		return A.AssignmentID == AssignmentID;
	});

	if (Index == INDEX_NONE)
	{
		return;
	}

	const FStationAssignment Assignment = ActiveStationAssignments[Index];
	ActiveStationAssignments.RemoveAt(Index);

	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UFactoryEventBusSubsystem* EventBus = GI ? GI->GetSubsystem<UFactoryEventBusSubsystem>() : nullptr;
	if (!EventBus)
	{
		return;
	}

	FTaskLifecycleEvent Event;
	Event.Timestamp = FDateTime::UtcNow();
	Event.EventID = FGuid::NewGuid();
	Event.TaskOrAssignmentID = AssignmentID;
	Event.EventType = ETaskLifecycleEventType::Completed;

	if (AFactoryAtlasRobot* Atlas = Assignment.AssignedAtlas.Get())
	{
		Event.ActorID = Atlas->AgentID;
		Event.ActorType = Atlas->AgentType;
	}

	EventBus->PublishTaskLifecycle(Event);
}

void UOutboundDispatchSubsystem::OnTransportTaskCompleted(const FGuid& TaskID, AFactoryTransportRobot* Robot)
{
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UFactoryEventBusSubsystem* EventBus = GI ? GI->GetSubsystem<UFactoryEventBusSubsystem>() : nullptr;
	if (!EventBus)
	{
		return;
	}

	FTaskLifecycleEvent Event;
	Event.Timestamp = FDateTime::UtcNow();
	Event.EventID = FGuid::NewGuid();
	Event.TaskOrAssignmentID = TaskID;
	Event.EventType = ETaskLifecycleEventType::Completed;

	if (Robot)
	{
		Event.ActorID = Robot->AgentID;
		Event.ActorType = Robot->AgentType;
	}

	EventBus->PublishTaskLifecycle(Event);
}
