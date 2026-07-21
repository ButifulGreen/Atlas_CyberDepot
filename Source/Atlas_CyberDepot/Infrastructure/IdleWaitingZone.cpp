// Copyright Epic Games, Inc. All Rights Reserved.

#include "Infrastructure/IdleWaitingZone.h"
#include "Atlas_CyberDepot.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/FactoryNPCHuman.h"
#include "Assignment/SmartFactoryManager.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AIdleWaitingZone::AIdleWaitingZone()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
}

void AIdleWaitingZone::BeginPlay()
{
	Super::BeginPlay();

	GetComponents<UParkingSlotMarkerComponent>(ParkingMarkers);

	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(RestDecayTimerHandle, this, &AIdleWaitingZone::OnRestDecayInterval, RestDecayIntervalSeconds, true);
	}
}

bool AIdleWaitingZone::GetHomeSlotTransform(int32 SlotIndex, FTransform& OutSlotTransform) const
{
	for (const UParkingSlotMarkerComponent* Marker : ParkingMarkers)
	{
		if (Marker && Marker->SlotIndex == SlotIndex)
		{
			OutSlotTransform = Marker->GetComponentTransform();
			return true;
		}
	}

	return false;
}

void AIdleWaitingZone::MarkSlotOccupied(AFactoryAgentBase* Agent, int32 SlotIndex)
{
	if (!Agent)
	{
		return;
	}

	SlotOccupancy.Add(SlotIndex, Agent);
	Agent->bIsParkedInIdleZone = true;

	UE_LOG(LogFactoryDispatch, Log, TEXT("[RestDecay] %s: %s 파킹(슬롯 %d, OperationRatio=%.2f)"),
		*GetName(), *Agent->GetName(), SlotIndex, Agent->GetOperationRatio());

	if (AMSmartFactoryManager* Manager = GetWorld() ? GetWorld()->GetGameState<AMSmartFactoryManager>() : nullptr)
	{
		Manager->OnAgentBecameIdle(Agent);
	}
}

void AIdleWaitingZone::GetParkingSlotLocations(TArray<TPair<int32, FVector>>& OutSlots) const
{
	OutSlots.Reset(ParkingMarkers.Num());
	for (const UParkingSlotMarkerComponent* Marker : ParkingMarkers)
	{
		if (Marker)
		{
			OutSlots.Add(TPair<int32, FVector>(Marker->SlotIndex, Marker->GetComponentLocation()));
		}
	}
}

bool AIdleWaitingZone::IsUsableBy(EActorType AgentType) const
{
	return (AllowedAgentTypes & (1 << static_cast<int32>(AgentType))) != 0;
}

void AIdleWaitingZone::ReleaseSlot(AFactoryAgentBase* Agent)
{
	if (!Agent)
	{
		return;
	}

	for (auto It = SlotOccupancy.CreateIterator(); It; ++It)
	{
		if (It.Value().Get() == Agent)
		{
			It.RemoveCurrent();
			break;
		}
	}

	Agent->bIsParkedInIdleZone = false;
	OnBatchTargetLeft(Agent);
}

bool AIdleWaitingZone::IsAgentParked(const AFactoryAgentBase* Agent) const
{
	for (const auto& Pair : SlotOccupancy)
	{
		if (Pair.Value.Get() == Agent)
		{
			return true;
		}
	}

	return false;
}

AFactoryAgentBase* AIdleWaitingZone::FindRestedOccupant() const
{
	for (const auto& Pair : SlotOccupancy)
	{
		AFactoryAgentBase* Agent = Pair.Value.Get();
		if (Agent && Agent->GetOperationRatio() <= FullyRestedThresholdRatio)
		{
			return Agent;
		}
	}

	return nullptr;
}

void AIdleWaitingZone::OnRestDecayInterval()
{
	if (SlotOccupancy.Num() > 0)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[RestDecay] %s: 감쇠 틱 — 파킹 %d대에 -%d 적용"),
			*GetName(), SlotOccupancy.Num(), RestDecayAmountPerInterval);
	}

	for (const auto& Pair : SlotOccupancy)
	{
		if (AFactoryAgentBase* Agent = Pair.Value.Get())
		{
			Agent->ApplyRestDecay(RestDecayAmountPerInterval);
			UE_LOG(LogFactoryDispatch, Log, TEXT("[RestDecay] %s OperationCount=%d (MaintenanceDue=%s, BatchEligible=%s, FullyRested=%s)"),
				*Agent->GetName(), Agent->GetOperationCount(),
				Agent->IsMaintenanceDue() ? TEXT("true") : TEXT("false"),
				Agent->GetOperationCount() >= BatchMaintenanceOperationThreshold ? TEXT("true") : TEXT("false"),
				Agent->GetOperationRatio() <= FullyRestedThresholdRatio ? TEXT("true") : TEXT("false"));
		}
	}

	OnBatchMaintenanceProgress();

	if (ShouldDispatchNPCForMaintenance())
	{
		if (AMSmartFactoryManager* Manager = GetWorld() ? GetWorld()->GetGameState<AMSmartFactoryManager>() : nullptr)
		{
			if (AFactoryNPCHuman* NPC = Manager->FindNearestAvailableNPC(GetActorLocation()))
			{
				BeginBatchMaintenance(NPC);
			}
			else
			{
				UE_LOG(LogFactoryDispatch, Warning, TEXT("[RestDecay] %s: 배치 정비 조건 충족했으나 가용 NPC 없음"), *GetName());
			}
		}
	}
}

bool AIdleWaitingZone::ShouldDispatchNPCForMaintenance() const
{
	if (SlotOccupancy.Num() == 0)
	{
		return false;
	}

	if (MaintenanceState != EZoneMaintenanceState::Idle)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[RestDecay] %s: 이미 배치 정비 진행 중이라 재판정 건너뜀"), *GetName());
		return false;
	}

	for (const auto& Pair : SlotOccupancy)
	{
		const AFactoryAgentBase* Agent = Pair.Value.Get();
		if (!Agent || Agent->GetOperationCount() < BatchMaintenanceOperationThreshold)
		{
			return false;
		}
	}

	UE_LOG(LogFactoryDispatch, Log, TEXT("[RestDecay] %s: 파킹된 %d대 전원 배치 정비 임계치(%d) 도달 — 배치 정비 디스패치"),
		*GetName(), SlotOccupancy.Num(), BatchMaintenanceOperationThreshold);
	return true;
}

void AIdleWaitingZone::BeginBatchMaintenance(AFactoryNPCHuman* NPC)
{
	BatchMaintenanceTargetSet.Reset();
	for (const auto& Pair : SlotOccupancy)
	{
		if (Pair.Value.IsValid())
		{
			BatchMaintenanceTargetSet.Add(Pair.Value);
		}
	}

	MaintenanceState = EZoneMaintenanceState::Active;
	BatchMaintenanceNPC = NPC;

	UE_LOG(LogFactoryDispatch, Log, TEXT("[RestDecay] %s: 배치 정비 시작 — NPC=%s, 대상 %d대"),
		*GetName(), NPC ? *NPC->GetName() : TEXT("?"), BatchMaintenanceTargetSet.Num());

	if (NPC && BatchMaintenanceTargetSet.Num() > 0)
	{
		if (AFactoryAgentBase* FirstTarget = BatchMaintenanceTargetSet[0].Get())
		{
			UE_LOG(LogFactoryDispatch, Log, TEXT("[RestDecay] %s: %s가 첫 대상 %s에게 QuickCheck 시작(나머지 %d대는 패시브 감쇠로 대기)"),
				*GetName(), *NPC->GetName(), *FirstTarget->GetName(), BatchMaintenanceTargetSet.Num() - 1);
			NPC->AssignMaintenance(FirstTarget, ERepairType::QuickCheck);
		}
	}
}

void AIdleWaitingZone::OnBatchTargetLeft(AFactoryAgentBase* Agent)
{
	if (!Agent || BatchMaintenanceTargetSet.Num() == 0)
	{
		return;
	}

	const int32 RemovedCount = BatchMaintenanceTargetSet.RemoveAll([Agent](const TWeakObjectPtr<AFactoryAgentBase>& Entry)
	{
		return Entry.Get() == Agent;
	});

	if (RemovedCount > 0)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[RestDecay] %s: %s가 대기실을 벗어나 배치 정비 대상에서 제외(잔여 %d대)"),
			*GetName(), *Agent->GetName(), BatchMaintenanceTargetSet.Num());
	}

	if (BatchMaintenanceTargetSet.Num() == 0 && MaintenanceState == EZoneMaintenanceState::Active)
	{
		EndBatchMaintenance();
	}
}

void AIdleWaitingZone::OnBatchMaintenanceProgress()
{
	if (MaintenanceState != EZoneMaintenanceState::Active)
	{
		return;
	}

	// 진입 조건(BatchMaintenanceOperationThreshold)과 동일한 기준으로 제외 판정한다 — IsMaintenanceDue()(더 낮은
	// MaintenanceThreshold)를 쓰면 들어올 때와 나갈 때 기준이 달라 대상이 비정상적으로 오래 남는다.
	const int32 RemovedCount = BatchMaintenanceTargetSet.RemoveAll([this](const TWeakObjectPtr<AFactoryAgentBase>& Entry)
	{
		const AFactoryAgentBase* Agent = Entry.Get();
		return !Agent || Agent->GetOperationCount() < BatchMaintenanceOperationThreshold;
	});

	if (RemovedCount > 0)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[RestDecay] %s: 감쇠로 배치 정비 임계치 아래로 내려간 %d대를 대상에서 제외(잔여 %d대)"),
			*GetName(), RemovedCount, BatchMaintenanceTargetSet.Num());
	}

	if (BatchMaintenanceTargetSet.Num() == 0 && MaintenanceState == EZoneMaintenanceState::Active)
	{
		EndBatchMaintenance();
	}
}

void AIdleWaitingZone::EndBatchMaintenance()
{
	UE_LOG(LogFactoryDispatch, Log, TEXT("[RestDecay] %s: 배치 정비 종료 — 파킹 전원 임계치 아래로 복귀, NPC=%s 사무실로 복귀"),
		*GetName(), BatchMaintenanceNPC.IsValid() ? *BatchMaintenanceNPC->GetName() : TEXT("?"));

	if (AFactoryNPCHuman* NPC = BatchMaintenanceNPC.Get())
	{
		NPC->ReturnToOfficeRoom();
	}

	BatchMaintenanceTargetSet.Reset();
	MaintenanceState = EZoneMaintenanceState::Idle;
	BatchMaintenanceNPC.Reset();
}

void AIdleWaitingZone::OnBatchMaintenanceInterrupted()
{
	BatchMaintenanceTargetSet.Reset();
	MaintenanceState = EZoneMaintenanceState::Idle;
	BatchMaintenanceNPC.Reset();
}

void AIdleWaitingZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AIdleWaitingZone, MaintenanceState);
}
