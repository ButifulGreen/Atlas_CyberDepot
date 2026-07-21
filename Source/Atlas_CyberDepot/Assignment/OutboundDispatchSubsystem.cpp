// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assignment/OutboundDispatchSubsystem.h"
#include "Atlas_CyberDepot.h"
#include "Assignment/DeliveryOrderSubsystem.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/FactoryAtlasRobot.h"
#include "Agent/FactoryTransportRobot.h"
#include "Agent/FactoryAIController.h"
#include "Infrastructure/StorageShelf.h"
#include "Infrastructure/IdleWaitingZone.h"
#include "EventBus/FactoryEventBusSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

void UOutboundDispatchSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 버그 수정 — 이 시점엔 아직 어떤 액터의 BeginPlay도 실행되지 않았다(RunDeferredWorldBeginPlaySetup 주석 참고).
	// 다음 틱으로 미뤄야 AIdleWaitingZone::ParkingMarkers 등이 정상적으로 채워진 뒤 배정할 수 있다.
	InWorld.GetTimerManager().SetTimerForNextTick(this, &UOutboundDispatchSubsystem::RunDeferredWorldBeginPlaySetup);
}

void UOutboundDispatchSubsystem::RunDeferredWorldBeginPlaySetup()
{
	// 7단계 후속 — 로봇마다 대기실 홈 슬롯을 먼저 고정 배정한 뒤, 유휴 로봇 배차를 스윕한다
	// (레벨에 배치된 로봇이 시작부터 유휴 상태면 곧장 자기 홈 슬롯으로 향하게 한다).
	AssignHomeIdleZoneSlots();
	TryDispatchIdleAgents();
}

void UOutboundDispatchSubsystem::AssignHomeIdleZoneSlots()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	struct FAgentEntry
	{
		AFactoryAgentBase* Agent = nullptr;
		FVector Location = FVector::ZeroVector;
	};

	TArray<AActor*> FoundAgents;
	UGameplayStatics::GetAllActorsOfClass(World, AFactoryAgentBase::StaticClass(), FoundAgents);

	TArray<FAgentEntry> Agents;
	for (AActor* Actor : FoundAgents)
	{
		if (AFactoryAgentBase* Agent = Cast<AFactoryAgentBase>(Actor))
		{
			if (Agent->AgentType == EActorType::AtlasRobot || Agent->AgentType == EActorType::TransportRobot)
			{
				Agents.Add({ Agent, Agent->GetActorLocation() });
			}
		}
	}

	// 실행할 때마다 동일한 결과가 나오도록(거리 동률 시 등) 이름순으로 먼저 정렬 — 레벨에 저장된 액터
	// 이름은 실행마다 바뀌지 않는다.
	Agents.Sort([](const FAgentEntry& A, const FAgentEntry& B) { return A.Agent->GetName() < B.Agent->GetName(); });

	struct FSlotEntry
	{
		AIdleWaitingZone* Zone = nullptr;
		int32 SlotIndex = 0;
		FVector Location = FVector::ZeroVector;
	};

	TArray<AActor*> FoundZones;
	UGameplayStatics::GetAllActorsOfClass(World, AIdleWaitingZone::StaticClass(), FoundZones);

	TArray<FSlotEntry> Slots;
	for (AActor* Actor : FoundZones)
	{
		AIdleWaitingZone* Zone = Cast<AIdleWaitingZone>(Actor);
		if (!Zone)
		{
			continue;
		}

		TArray<TPair<int32, FVector>> ZoneSlots;
		Zone->GetParkingSlotLocations(ZoneSlots);
		for (const TPair<int32, FVector>& SlotPair : ZoneSlots)
		{
			Slots.Add({ Zone, SlotPair.Key, SlotPair.Value });
		}
	}
	Slots.Sort([](const FSlotEntry& A, const FSlotEntry& B)
	{
		if (A.Zone != B.Zone)
		{
			return A.Zone->GetName() < B.Zone->GetName();
		}
		return A.SlotIndex < B.SlotIndex;
	});

	// 버그 수정(사용자 지시) — 예전엔 타입별로 로봇/대기실 풀을 완전히 나눠 이름순으로 앞에서부터
	// 채웠다(물리적 위치 무관). AllowedAgentTypes가 비트마스크가 되면서 한 대기실이 아틀라스/배송로봇을
	// 동시에 받을 수 있어, 타입별 분리 풀 자체가 부적절해졌다. 이제 로봇-슬롯 전체 조합 중 "아직 안
	// 정해졌고 타입이 허용되는" 쌍 중 가장 가까운 것부터 하나씩 그리디하게 확정한다. 대기실 배치(마커
	// 총합)가 항상 로봇 수 이상이라고 가정하므로(사용자가 직접 배치), 로봇이 남는 경우는 고려하지 않는다.
	while (Agents.Num() > 0 && Slots.Num() > 0)
	{
		int32 BestAgentIndex = INDEX_NONE;
		int32 BestSlotIndex = INDEX_NONE;
		float BestDistSq = TNumericLimits<float>::Max();

		for (int32 AgentIndex = 0; AgentIndex < Agents.Num(); ++AgentIndex)
		{
			for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
			{
				if (!Slots[SlotIndex].Zone->IsUsableBy(Agents[AgentIndex].Agent->AgentType))
				{
					continue;
				}

				const float DistSq = FVector::DistSquared(Agents[AgentIndex].Location, Slots[SlotIndex].Location);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestAgentIndex = AgentIndex;
					BestSlotIndex = SlotIndex;
				}
			}
		}

		if (BestAgentIndex == INDEX_NONE)
		{
			// 남은 로봇들의 타입을 허용하는 슬롯이 더 이상 없음 — 레벨 배치 문제이니 조용히 넘기지 않고 남긴다.
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[Dispatch] AssignHomeIdleZoneSlots — 남은 로봇 %d대를 배정할 수 있는 대기실 슬롯이 없음(AllowedAgentTypes 배치 확인 필요)"),
				Agents.Num());
			break;
		}

		AFactoryAgentBase* ChosenAgent = Agents[BestAgentIndex].Agent;
		const FSlotEntry ChosenSlot = Slots[BestSlotIndex];
		ChosenAgent->AssignHomeIdleZoneSlot(ChosenSlot.Zone, ChosenSlot.SlotIndex);

		Agents.RemoveAt(BestAgentIndex);
		Slots.RemoveAt(BestSlotIndex);
	}
}

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
			// 7단계 신규 — 줄 작업이 없으면 대기실로 향한다("유휴 로봇은 항상 대기실로" 규칙).
			if (!TryAssignIdleAtlas(Atlas, Assignment))
			{
				Atlas->TryHeadToIdleZone();
			}
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
			if (!TryAssignIdleTransportRobot(Robot, Task))
			{
				Robot->TryHeadToIdleZone();
			}
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

	// 버그 수정 — 이 함수는 이제(대기열 기능) 아틀라스가 트레이에서 물품을 집는 TransferItem 호출
	// 스택 한가운데서 재진입 호출될 수 있다. 여기서 바로 TryDispatchIdleAgents를 돌리면, 지금 막
	// 트레이 핸드오프를 진행 중인 그 아틀라스/로봇의 배정·상태가 미처 안정되기 전에 새 배차 스윕이
	// 끼어들어 트립·로봇 매칭이 뒤섞였다(대기열 이전엔 이 함수가 플레이어 입력이라는 최상위 호출에서만
	// 실행돼 이 재진입 경로 자체가 없어서 안 드러났다). 현재 호출 스택이 완전히 풀린 다음 틱으로 미뤄서
	// 항상 안정된 상태에서 배차가 돌게 한다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UOutboundDispatchSubsystem::TryDispatchIdleAgents));
	}
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
		// Docs/00_DesignPrinciples.md 스펙 이탈(승인됨) — 단일 점유가 아니라 MaxConcurrentAtlas
		// 기준 카운트 만석 여부로 판정한다.
		return Shelf->IsZoneFull(ZoneType);
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

	// 방어 로그 — 배송로봇 쪽(TryAssignIdleTransportRobot)과 동일한 이유. 이 아틀라스가 이미
	// CurrentAssignment를 갖고 있는 채로 또 배정을 받으면 AcceptStationAssignment가 조건 없이
	// 덮어써서 기존 배정이 정리 없이 사라진다. 정상 경로라면 CurrentState==Idle 확인 후에만
	// 호출되므로 CurrentAssignment도 항상 비어있어야 한다.
	if (Atlas->CurrentAssignment.TargetZoneOwner.IsValid())
	{
		UE_LOG(LogFactoryDispatch, Error, TEXT("[Dispatch] %s에 배정 거부 — 이미 배정(%s) 진행 중인데 새 배정 시도됨. 후보 배정은 그대로 둠(유실 방지)"),
			*Atlas->GetName(), *Atlas->CurrentAssignment.AssignmentID.ToString());
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

		// 버그 수정 — TryAssignIdleTransportRobot은 성공 시 로그를 남기지만 이쪽엔 없어서, 특정 존
		// 배정이 아무에게도 안 잡히는 상황을 로그만으로는 구분할 수 없었다("배정됐는데 안 보임" vs
		// "애초에 아무도 안 잡음"). 같은 스타일로 남긴다.
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Dispatch] %s에 배정(%s) — Zone=%s, 대상=%s, 남은 트립 %d개"),
			*Atlas->GetName(), *Assignment.AssignmentID.ToString(), *UEnum::GetValueAsString(Assignment.ZoneType),
			Assignment.TargetZoneOwner.IsValid() ? *Assignment.TargetZoneOwner->GetName() : TEXT("Invalid"),
			Assignment.RemainingCount);

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

	// 방어 로그 — 이 로봇이 이미 CurrentTask를 갖고 있는 채로 또 배정을 받으면 AcceptTransportTask가
	// CurrentTask를 조건 없이 덮어써서 기존 트립이 정리 없이 통째로 사라진다(그 트립을 기다리던
	// 아틀라스는 영원히 "배정 안 됨"만 반복). 정상 경로라면 CurrentState==Idle 확인 후에만 호출되므로
	// CurrentTask도 항상 비어있어야 한다 — 이 분기가 찍히면 그 전제가 깨진 것이니 호출부를 재검토해야 함.
	if (Robot->CurrentTask.IsValid())
	{
		UE_LOG(LogFactoryDispatch, Error, TEXT("[Dispatch] %s에 트립 배정 거부 — 이미 트립(%s) 진행 중인데 새 배정(%s) 시도됨. 대기 작업은 큐에 그대로 둠(유실 방지)"),
			*Robot->GetName(), *Robot->CurrentTask.TaskID.ToString(), *PendingTransportTasks[0].TaskID.ToString());
		return false;
	}

	// 픽업 대기 물품이 있는 거점을 스캔해 작업을 구성하는 로직은 아직 없고,
	// 지금은 이미 채워져 있는 PendingTransportTasks 큐에서만 꺼내온다.
	OutTask = PendingTransportTasks[0];
	PendingTransportTasks.RemoveAt(0);

	// 디버그 편의 — 대기열 기능 도입 후 배송로봇 수 대비 작업 생성 속도가 빠르면 여기서 큐가 계속 쌓인다.
	// "누가 안 옴" 증상을 볼 때 이 로그로 실제 처리 속도(큐 잔량 추이)를 바로 확인할 수 있다.
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Dispatch] %s에 트립(%s) 배정 — 남은 대기 작업 %d개"),
		*Robot->GetName(), *OutTask.TaskID.ToString(), PendingTransportTasks.Num());

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

void UOutboundDispatchSubsystem::RequeueStationAssignment(FStationAssignment Assignment)
{
	// 새 AssignmentID 발급 — 이 값을 참조하는 PendingHandoffs 등이 죽은 배정을 잘못 가리키지 않게 한다.
	Assignment.AssignmentID = FGuid::NewGuid();
	Assignment.AssignedAtlas.Reset();
	ActiveStationAssignments.Add(Assignment);

	UE_LOG(LogFactoryDispatch, Warning, TEXT("[Dispatch] 배정 재큐잉(%s) — Zone=%s, 대상=%s, 남은 트립 %d개"),
		*Assignment.AssignmentID.ToString(), *UEnum::GetValueAsString(Assignment.ZoneType),
		Assignment.TargetZoneOwner.IsValid() ? *Assignment.TargetZoneOwner->GetName() : TEXT("Invalid"),
		Assignment.RemainingCount);
}

void UOutboundDispatchSubsystem::RequeueTransportTask(const FTransportTask& Task)
{
	PendingTransportTasks.Add(Task);
	UE_LOG(LogFactoryDispatch, Warning, TEXT("[Dispatch] 트립(%s) 재큐잉 — 대기 작업 %d개"),
		*Task.TaskID.ToString(), PendingTransportTasks.Num());
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
