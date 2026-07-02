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
	}

	// PendingTransportTasks는 아직 DecomposeOrder가 채우지 않아(Docs/14_OpenIssues.md 참고) SourceOrderID 연결 대상이 없다.
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
