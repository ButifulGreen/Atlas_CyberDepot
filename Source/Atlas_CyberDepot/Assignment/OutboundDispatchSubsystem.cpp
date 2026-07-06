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

		FStationAssignment Assignment;
		Assignment.AssignmentID = FGuid::NewGuid();
		Assignment.SourceOrderID = Order.OrderID;
		Assignment.ZoneType = EWorkZoneType::ShelfOutboundZone;
		Assignment.TargetZoneOwner = Shelf;
		Assignment.RemainingCount = Pair.Value;
		ActiveStationAssignments.Add(Assignment);

		// 6단계 신규 — 아틀라스가 선반에서 인출한 물품을 배송로봇에게 넘기면, 그 배송로봇이 향할
		// Outbound 트레이 쪽에도 별도의 TrayWorkZone 배정(같은 아틀라스가 담당)이 있어야 최종 적재가 된다.
		AHorizontalTray* Tray = FindTrayForItemType(Pair.Key, ETrayDirection::Outbound);
		if (!Tray)
		{
			continue;
		}

		FStationAssignment TrayAssignment;
		TrayAssignment.AssignmentID = FGuid::NewGuid();
		TrayAssignment.SourceOrderID = Order.OrderID;
		TrayAssignment.ZoneType = EWorkZoneType::TrayWorkZone;
		TrayAssignment.TargetZoneOwner = Tray;
		TrayAssignment.RemainingCount = Pair.Value;
		ActiveStationAssignments.Add(TrayAssignment);

		// 배송로봇은 짐을 1개씩만 나르므로, 수량만큼 개별 트립을 큐에 넣는다.
		for (int32 TripIndex = 0; TripIndex < Pair.Value; ++TripIndex)
		{
			FTransportTask Task;
			Task.TaskID = FGuid::NewGuid();
			Task.SourceOrderID = Order.OrderID;
			Task.PickupPoint = Shelf;
			Task.DropoffPoint = Tray;
			Task.ItemType = Pair.Key;
			PendingTransportTasks.Add(Task);
		}
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

	FStationAssignment TrayAssignment;
	TrayAssignment.AssignmentID = FGuid::NewGuid();
	TrayAssignment.ZoneType = EWorkZoneType::TrayWorkZone;
	TrayAssignment.TargetZoneOwner = Tray;
	TrayAssignment.RemainingCount = 1;
	ActiveStationAssignments.Add(TrayAssignment);

	FStationAssignment ShelfAssignment;
	ShelfAssignment.AssignmentID = FGuid::NewGuid();
	ShelfAssignment.ZoneType = EWorkZoneType::ShelfInboundZone;
	ShelfAssignment.TargetZoneOwner = Shelf;
	ShelfAssignment.RemainingCount = 1;
	ActiveStationAssignments.Add(ShelfAssignment);

	FTransportTask Task;
	Task.TaskID = FGuid::NewGuid();
	Task.PickupPoint = Tray;
	Task.DropoffPoint = Shelf;
	Task.ItemType = ItemType;
	PendingTransportTasks.Add(Task);

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

bool UOutboundDispatchSubsystem::TryAssignIdleAtlas(AFactoryAtlasRobot* Atlas, FStationAssignment& OutAssignment)
{
	if (!Atlas)
	{
		return false;
	}

	for (FStationAssignment& Assignment : ActiveStationAssignments)
	{
		if (!Assignment.AssignedAtlas.IsValid() && Assignment.RemainingCount > 0)
		{
			Assignment.AssignedAtlas = Atlas;
			OutAssignment = Assignment;
			Atlas->AcceptStationAssignment(Assignment);
			return true;
		}
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
		const FTransform StagingTransform = (Assignment->ZoneType == EWorkZoneType::ShelfInboundZone)
			? Shelf->InboundStagingTransform
			: Shelf->OutboundStagingTransform;

		if (AFactoryAIController* AIController = Cast<AFactoryAIController>(To->GetController()))
		{
			AIController->RequestMoveWithFilter(StagingTransform.GetLocation());
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
