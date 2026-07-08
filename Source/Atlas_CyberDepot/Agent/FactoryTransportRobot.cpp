// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryTransportRobot.h"
#include "Agent/FactoryAIController.h"
#include "Infrastructure/LogisticsItem.h"
#include "Infrastructure/StorageShelf.h"
#include "Infrastructure/HorizontalTray.h"
#include "Infrastructure/IdleWaitingZone.h"
#include "Navigation/CostZoneVolume.h"
#include "Assignment/OutboundDispatchSubsystem.h"
#include "Assignment/SmartFactoryManager.h"
#include "Repair/RepairProgressComponent.h"
#include "EventBus/FactoryEventBusSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

AFactoryTransportRobot::AFactoryTransportRobot()
{
	AgentType = EActorType::TransportRobot;
	RepairComponent = CreateDefaultSubobject<URepairProgressComponent>(TEXT("RepairComponent"));
}

void AFactoryTransportRobot::BeginPlay()
{
	Super::BeginPlay();

	// 버그 수정 — 이 로봇은 스켈레탈 메시가 없어 GetMesh()는 항상 빈 컴포넌트다. 실제 바디 겸 물품
	// 소켓("ItemSocket")은 BP에 추가된 스태틱 메시 컴포넌트에 있으므로, 그 소켓을 실제로 가진
	// 컴포넌트를 찾아 캐싱한다(못 찾으면 첫 스태틱 메시 컴포넌트로 대체).
	TArray<UStaticMeshComponent*> StaticMeshComponents;
	GetComponents<UStaticMeshComponent>(StaticMeshComponents);

	for (UStaticMeshComponent* Component : StaticMeshComponents)
	{
		if (Component && Component->DoesSocketExist(PayloadItemSocketName))
		{
			BodyMeshComponent = Component;
			break;
		}
	}

	if (!BodyMeshComponent && StaticMeshComponents.Num() > 0)
	{
		BodyMeshComponent = StaticMeshComponents[0];
	}
}

bool AFactoryTransportRobot::IsMaintenanceDue() const
{
	return OperationCount >= MaintenanceThreshold;
}

float AFactoryTransportRobot::GetOperationRatio() const
{
	return MaintenanceThreshold > 0 ? static_cast<float>(OperationCount) / MaintenanceThreshold : 0.f;
}

void AFactoryTransportRobot::ApplyRestDecay(int32 Amount)
{
	OperationCount = FMath::Max(0, OperationCount - Amount);
}

bool AFactoryTransportRobot::IsEligibleForQuickCheck() const
{
	return CurrentState == EAgentState::Idle && bIsParkedInIdleZone && IsMaintenanceDue();
}

float AFactoryTransportRobot::ComputeCurrentBreakdownChance() const
{
	if (OperationCount < MaintenanceThreshold)
	{
		return 0.f;
	}

	const int32 OverageUnits = (OperationCount - MaintenanceThreshold) / OverageOperationsPerStep;
	const float Chance = BreakdownChanceBase + static_cast<float>(OverageUnits) * BreakdownChanceOverageMultiplier;
	return FMath::Min(Chance, MaxBreakdownChanceCap);
}

void AFactoryTransportRobot::AcceptTransportTask(const FTransportTask& Task)
{
	LeaveIdleZoneIfParked();

	CurrentTask = Task;

	if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
	{
		SetState(EAgentState::Moving);
		AIController->RequestMoveWithFilter(GetTaskPointLocation(Task.PickupPoint.Get(), true));
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
		{
			FTaskLifecycleEvent Event;
			Event.Timestamp = FDateTime::UtcNow();
			Event.EventID = FGuid::NewGuid();
			Event.TaskOrAssignmentID = Task.TaskID;
			Event.EventType = ETaskLifecycleEventType::Assigned;
			Event.ActorID = AgentID;
			Event.ActorType = AgentType;
			Event.ItemType = Task.ItemType;
			EventBus->PublishTaskLifecycle(Event);
		}
	}
}

void AFactoryTransportRobot::OnItemGivenByAtlas(ALogisticsItem* Item)
{
	PayloadItem = Item;
	if (Item && BodyMeshComponent)
	{
		// 버그 수정 — 물품 콜리전이 켜진 채로 부착되면 배송로봇 콜리전과 겹쳐 물리 디페네트레이션이
		// 발생할 수 있다(FactoryAtlasRobot::AttachHeldItem 참고). 들고 있는 동안은 콜리전이 필요 없다.
		Item->SetActorEnableCollision(false);

		// 버그 수정 — 이 로봇은 스켈레탈 메시가 없어 GetMesh()에 붙이면 소켓을 절대 못 찾아 매 틱
		// "No SkeletalMesh for Component" 경고가 반복 출력됐다. 실제 소켓이 있는 BodyMeshComponent
		// (BeginPlay에서 캐싱한 스태틱 메시 컴포넌트)에 부착한다.
		const FName SocketName = BodyMeshComponent->DoesSocketExist(PayloadItemSocketName) ? PayloadItemSocketName : NAME_None;
		Item->AttachToComponent(BodyMeshComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
		{
			FTaskLifecycleEvent Event;
			Event.Timestamp = FDateTime::UtcNow();
			Event.EventID = FGuid::NewGuid();
			Event.TaskOrAssignmentID = CurrentTask.TaskID;
			Event.EventType = ETaskLifecycleEventType::PickedUp;
			Event.ActorID = AgentID;
			Event.ActorType = AgentType;
			Event.ItemType = CurrentTask.ItemType;
			EventBus->PublishTaskLifecycle(Event);
		}
	}

	if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
	{
		SetState(EAgentState::Moving);
		AIController->RequestMoveWithFilter(GetTaskPointLocation(CurrentTask.DropoffPoint.Get(), false));
	}
}

void AFactoryTransportRobot::OnItemCollectedByAtlas()
{
	if (PayloadItem)
	{
		PayloadItem->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		PayloadItem = nullptr;
	}

	OnTaskCompleted();
}

void AFactoryTransportRobot::OnArrivedAtDestination()
{
	if (TryHandleIdleZoneArrival())
	{
		return;
	}

	// 도착 후 스스로 트레이/선반을 건드리지 않고 파킹 — 아틀라스가 TransferItem으로 다가와야 다음 단계로 넘어간다.
	SetState(EAgentState::Working);
	EvaluateRotationOrContinue();
}

FVector AFactoryTransportRobot::GetTaskPointLocation(AActor* PointActor, bool bIsPickupSide) const
{
	if (const AHorizontalTray* Tray = Cast<AHorizontalTray>(PointActor))
	{
		return Tray->GetTransportRobotWorkLocation();
	}

	if (const AStorageShelf* Shelf = Cast<AStorageShelf>(PointActor))
	{
		// 버그 수정 — 별도 스테이징 지점이 아니라, 이번 트립이 실려 온 정확한 슬롯의 (X,Y) 위치로
		// 아틀라스와 직접 만난다(층 높이는 ComputeWorkLocation이 지상으로 고정해서 무시됨).
		const EWorkZoneType ZoneType = bIsPickupSide ? EWorkZoneType::ShelfOutboundZone : EWorkZoneType::ShelfInboundZone;
		return Shelf->GetTransportRobotWorkLocation(CurrentTask.FloorIndex, CurrentTask.SlotIndex, ZoneType);
	}

	return PointActor ? PointActor->GetActorLocation() : FVector::ZeroVector;
}

void AFactoryTransportRobot::EvaluateRotationOrContinue()
{
	if (FMath::FRand() < ComputeCurrentBreakdownChance())
	{
		SetState(EAgentState::Broken);

		if (UGameInstance* GI = GetGameInstance())
		{
			if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
			{
				FAnomalyEvent Event;
				Event.Timestamp = FDateTime::UtcNow();
				Event.LogID = FGuid::NewGuid();
				Event.Severity = EEventSeverity::Critical;
				Event.ActorID = AgentID;
				Event.ActorType = AgentType;
				Event.AnomalyCode = TEXT("Code:005");
				Event.Location = GetActorLocation();
				Event.RiskValue = ComputeCurrentBreakdownChance();
				EventBus->PublishAnomaly(Event);
			}
		}

		if (UWorld* World = GetWorld())
		{
			if (AMSmartFactoryManager* Manager = World->GetGameState<AMSmartFactoryManager>())
			{
				Manager->RequestMaintenance(this, ERepairType::FullRepair);
			}
		}
		return;
	}

	if (!IsMaintenanceDue() || PayloadItem)
	{
		return;
	}

	// 교대를 대신할 로봇이 대기실에 있을 때만 자리를 비운다 — 없으면 계속 작업을 이어간다.
	if (!HasRestedTransportRobotAvailable())
	{
		return;
	}

	// 7단계 후속 — 대기실 검색 대신 자신의 고정 홈 슬롯(HomeIdleZone/HomeSlotIndex)으로 바로 이동한다
	// (선입선출 없음). 이동/도착 처리는 TryHeadToIdleZone/TryHandleIdleZoneArrival과 완전히 동일한 경로.
	TryHeadToIdleZone();
}

bool AFactoryTransportRobot::HasRestedTransportRobotAvailable() const
{
	TArray<AActor*> FoundZones;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AIdleWaitingZone::StaticClass(), FoundZones);

	for (AActor* ZoneActor : FoundZones)
	{
		AIdleWaitingZone* Zone = Cast<AIdleWaitingZone>(ZoneActor);
		if (Zone && Zone->AllowedAgentType == EActorType::TransportRobot && Zone->FindRestedOccupant())
		{
			return true;
		}
	}

	return false;
}

void AFactoryTransportRobot::OnEnterBlockedState()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> FoundZones;
	UGameplayStatics::GetAllActorsOfClass(World, ACostZoneVolume::StaticClass(), FoundZones);

	const FVector MyLocation = GetActorLocation();
	for (AActor* ZoneActor : FoundZones)
	{
		if (ACostZoneVolume* Zone = Cast<ACostZoneVolume>(ZoneActor))
		{
			if (FVector::DistSquared(Zone->GetActorLocation(), MyLocation) <= FMath::Square(BlockedZoneRegisterRadius))
			{
				Zone->RegisterBlocker(this);
				RegisteredBlockedZones.Add(Zone);
			}
		}
	}
}

void AFactoryTransportRobot::OnBlockedTick(float DeltaTime)
{
	Super::OnBlockedTick(DeltaTime);

	if (!bHasRegisteredBlocker)
	{
		bHasRegisteredBlocker = true;
		OnEnterBlockedState();
	}
}

void AFactoryTransportRobot::OnUnblocked()
{
	Super::OnUnblocked();

	if (!bHasRegisteredBlocker)
	{
		return;
	}

	for (const TWeakObjectPtr<ACostZoneVolume>& ZoneRef : RegisteredBlockedZones)
	{
		if (ACostZoneVolume* Zone = ZoneRef.Get())
		{
			Zone->UnregisterBlocker(this);
		}
	}

	RegisteredBlockedZones.Reset();
	bHasRegisteredBlocker = false;
}

void AFactoryTransportRobot::OnTaskCompleted()
{
	SetState(EAgentState::Idle);
	++OperationCount;

	const FGuid CompletedTaskID = CurrentTask.TaskID;
	UOutboundDispatchSubsystem* Dispatch = GetWorld()->GetSubsystem<UOutboundDispatchSubsystem>();
	if (Dispatch)
	{
		Dispatch->OnTransportTaskCompleted(CompletedTaskID, this);
	}

	CurrentTask = FTransportTask();

	// 유휴 전환 즉시 다음 트립을 스스로 이어받는다(Pull 방식 재배정). 다음 트립이 없으면 대기실로 향한다
	// (7단계 신규 — "유휴 로봇은 항상 대기실로" 규칙).
	if (Dispatch)
	{
		FTransportTask NewTask;
		if (!Dispatch->TryAssignIdleTransportRobot(this, NewTask))
		{
			TryHeadToIdleZone();
		}
	}

	if (OperationCount < MaintenanceThreshold)
	{
		return;
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
		{
			FAnomalyEvent Event;
			Event.Timestamp = FDateTime::UtcNow();
			Event.LogID = FGuid::NewGuid();
			Event.Severity = EEventSeverity::Warning;
			Event.ActorID = AgentID;
			Event.ActorType = AgentType;
			Event.AnomalyCode = TEXT("Code:003");
			Event.Location = GetActorLocation();
			Event.RiskValue = ComputeCurrentBreakdownChance();
			EventBus->PublishAnomaly(Event);
		}
	}
}

void AFactoryTransportRobot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFactoryTransportRobot, PayloadItem);
}
