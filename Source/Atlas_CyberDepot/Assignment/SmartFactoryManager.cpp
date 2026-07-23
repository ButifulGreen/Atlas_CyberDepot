// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assignment/SmartFactoryManager.h"
#include "Atlas_CyberDepot.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/FactoryNPCHuman.h"
#include "Infrastructure/IdleWaitingZone.h"
#include "Repair/RepairProgressComponent.h"
#include "Infrastructure/LogisticsItem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/DataTable.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"

void AMSmartFactoryManager::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	// EItemType 개수(3개)만큼 슬롯을 채우고 Fisher-Yates로 셔플 — 이번 세션 동안 고정.
	ItemTypeToMeshSlot = { 0, 1, 2 };
	for (int32 Index = ItemTypeToMeshSlot.Num() - 1; Index > 0; --Index)
	{
		const int32 SwapIndex = FMath::RandRange(0, Index);
		ItemTypeToMeshSlot.Swap(Index, SwapIndex);
	}
}

int32 AMSmartFactoryManager::AllocateNextAgentDisplayNumber(EActorType ActorType)
{
	int32& Counter = AgentDisplayNameCounters.FindOrAdd(ActorType);
	++Counter;
	return Counter;
}

void AMSmartFactoryManager::AdjustReputation(float Delta, FName Reason)
{
	ReputationScore += Delta;
}

bool AMSmartFactoryManager::TryAdjustFunds(float Delta, FName Reason)
{
	if (SharedFunds + Delta < 0.f)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Economy] 자금 조정 실패(%s) — 잔액 %.0f, 요청 %.0f"), *Reason.ToString(), SharedFunds, Delta);
		return false;
	}

	SharedFunds += Delta;
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Economy] 자금 조정(%s) %+.0f — 잔액 %.0f"), *Reason.ToString(), Delta, SharedFunds);
	return true;
}

void AMSmartFactoryManager::UpdateVendorOrderDisplays(const TArray<FDeliveryOrder>& ActiveOrders)
{
	VendorOrderDisplays.Reset(ActiveOrders.Num());
	for (const FDeliveryOrder& Order : ActiveOrders)
	{
		FVendorOrderDisplay Display;
		Display.VendorName = Order.VendorName;
		Display.OrderID = Order.OrderID;
		Display.QtyA = Order.RequestedQuantities.FindRef(EItemType::ItemA);
		Display.QtyB = Order.RequestedQuantities.FindRef(EItemType::ItemB);
		Display.QtyC = Order.RequestedQuantities.FindRef(EItemType::ItemC);
		Display.bAvailable = (Order.Status == EOrderStatus::Available);
		VendorOrderDisplays.Add(Display);
	}

	// 서버 자신에게는 OnRep이 자동 호출되지 않으므로 명시적으로 호출(SetState/OnRep_CurrentState와 동일 패턴).
	if (HasAuthority())
	{
		OnRep_VendorOrderDisplays();
	}
}

void AMSmartFactoryManager::OnRep_VendorOrderDisplays()
{
	OnVendorOrdersUpdated.Broadcast();
}

const FItemTypeDefinition* AMSmartFactoryManager::FindItemDefinition(EItemType ItemType) const
{
	if (!ItemPriceTable)
	{
		return nullptr;
	}

	TArray<FItemTypeDefinition*> Rows;
	ItemPriceTable->GetAllRows<FItemTypeDefinition>(TEXT("AMSmartFactoryManager::FindItemDefinition"), Rows);
	for (const FItemTypeDefinition* Row : Rows)
	{
		if (Row && Row->Type == ItemType)
		{
			return Row;
		}
	}

	return nullptr;
}

int32 AMSmartFactoryManager::GetUnitPrice(EItemType ItemType) const
{
	const FItemTypeDefinition* Definition = FindItemDefinition(ItemType);
	return Definition ? Definition->UnitPrice : 0;
}

int32 AMSmartFactoryManager::GetSellPrice(EItemType ItemType) const
{
	const FItemTypeDefinition* Definition = FindItemDefinition(ItemType);
	return Definition ? Definition->SellPrice : 0;
}

AFactoryNPCHuman* AMSmartFactoryManager::FindNearestAvailableNPC(const FVector& Location) const
{
	TArray<AActor*> FoundNPCs;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFactoryNPCHuman::StaticClass(), FoundNPCs);

	AFactoryNPCHuman* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	for (AActor* Actor : FoundNPCs)
	{
		AFactoryNPCHuman* NPC = Cast<AFactoryNPCHuman>(Actor);
		// 이미 정비 중인 NPC와, 플레이어가 빙의 중인 NPC는 배정 후보에서 제외한다(8단계 멀티플레이어 정교화,
		// 사용자 지시 2026-07-22) — 빙의 중엔 컨트롤러가 AFactoryPlayerController라 AssignMaintenance의 이동
		// 요청이 조용히 무시되면서도 상태 전환/정비 참여는 그대로 진행돼, 플레이어가 조작 중인 NPC가 본인도
		// 모르게 다른 로봇의 정비 인원으로 잡히는 문제가 있었다.
		if (!NPC || NPC->CurrentState == EAgentState::UnderRepair || Cast<APlayerController>(NPC->GetController()))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(NPC->GetActorLocation(), Location);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = NPC;
		}
	}

	return Best;
}

void AMSmartFactoryManager::RequestMaintenance(AFactoryAgentBase* Agent, ERepairType RepairType)
{
	if (!Agent)
	{
		return;
	}

	// 이미 참여 중인 정비자(AI든 빙의 플레이어든)가 있으면 추가 AI NPC를 배정하지 않는다.
	if (URepairProgressComponent* ExistingRepair = Agent->GetRepairComponent())
	{
		if (ExistingRepair->GetValidRepairerCount() > 0)
		{
			UE_LOG(LogFactoryDispatch, Log, TEXT("[Repair] %s는 이미 정비 인원이 있어 추가 배정을 건너뜀"), *Agent->GetName());
			return;
		}
	}

	AFactoryNPCHuman* NPC = FindNearestAvailableNPC(Agent->GetActorLocation());
	if (!NPC)
	{
		// 버그 수정 — 지금 당장 가용 NPC가 없다고 조용히 포기하면 이 로봇은 배정자 없이 영구히 Broken으로
		// 남는다. 나중에 다른 NPC가 정비를 마치고 자유로워질 때(TryAssignNextPendingMaintenance) 재시도할
		// 수 있도록 대기열에 등록해둔다.
		bool bAlreadyQueued = false;
		for (const TWeakObjectPtr<AFactoryAgentBase>& Queued : PendingMaintenanceQueue)
		{
			if (Queued.Get() == Agent)
			{
				bAlreadyQueued = true;
				break;
			}
		}

		if (!bAlreadyQueued)
		{
			PendingMaintenanceQueue.Add(Agent);
		}

		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Repair] %s 정비 요청 — 가용 NPC 없어 대기열에 등록(대기 %d건)"),
			*Agent->GetName(), PendingMaintenanceQueue.Num());
		return;
	}

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Repair] %s를 %s 정비(%s)에 배정"), *NPC->GetName(), *Agent->GetName(),
		RepairType == ERepairType::FullRepair ? TEXT("FullRepair") : TEXT("QuickCheck"));

	NPC->AssignMaintenance(Agent, RepairType);
}

void AMSmartFactoryManager::OnAgentBecameIdle(AFactoryAgentBase* Agent)
{
	if (!Agent)
	{
		return;
	}

	if (!Agent->IsMaintenanceDue())
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[RestDecay] %s 파킹 — 아직 정비 임계치 미도달(OperationRatio=%.2f), QuickCheck 요청 안 함"),
			*Agent->GetName(), Agent->GetOperationRatio());
		return;
	}

	UE_LOG(LogFactoryDispatch, Log, TEXT("[RestDecay] %s 파킹 즉시 정비 임계치 도달(OperationRatio=%.2f) — 개별 QuickCheck 요청"),
		*Agent->GetName(), Agent->GetOperationRatio());
	RequestMaintenance(Agent, ERepairType::QuickCheck);
}

void AMSmartFactoryManager::OnRepairCompleted(AFactoryAgentBase* Agent)
{
	TArray<AActor*> FoundZones;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AIdleWaitingZone::StaticClass(), FoundZones);

	for (AActor* ZoneActor : FoundZones)
	{
		AIdleWaitingZone* Zone = Cast<AIdleWaitingZone>(ZoneActor);
		if (!Zone || !Zone->ShouldDispatchNPCForMaintenance())
		{
			continue;
		}

		if (AFactoryNPCHuman* NPC = FindNearestAvailableNPC(Zone->GetActorLocation()))
		{
			Zone->BeginBatchMaintenance(NPC);
		}
	}
}

bool AMSmartFactoryManager::TryAssignNextPendingMaintenance(AFactoryNPCHuman* NPC)
{
	if (!NPC)
	{
		return false;
	}

	while (PendingMaintenanceQueue.Num() > 0)
	{
		AFactoryAgentBase* Agent = PendingMaintenanceQueue[0].Get();
		PendingMaintenanceQueue.RemoveAt(0);

		if (!Agent)
		{
			// 이 사이 파괴됨 — 폐기하고 다음 항목 확인.
			continue;
		}

		// 버그 수정 — 대기열엔 Broken(FullRepair) 요청뿐 아니라 QuickCheck 요청(파킹된 채 Idle 상태로
		// 임계치를 넘긴 로봇, OnAgentBecameIdle 경유)도 들어온다. Broken만 통과시키던 필터가 QuickCheck
		// 항목을 전부 조용히 버려서 그 로봇들이 영영 정비를 못 받는 문제가 있었다.
		const bool bStillNeedsRepair = Agent->CurrentState == EAgentState::Broken || Agent->IsMaintenanceDue();
		if (!bStillNeedsRepair)
		{
			// 대기 중 패시브 감쇠로 이미 임계치 아래로 내려갔다 — 더 이상 정비가 필요 없다.
			continue;
		}

		if (URepairProgressComponent* Repair = Agent->GetRepairComponent())
		{
			if (Repair->GetValidRepairerCount() > 0)
			{
				// 이미 다른 정비자가 붙었다(예: 플레이어 빙의) — 폐기하고 다음 항목 확인.
				continue;
			}
		}

		const ERepairType RepairType = (Agent->CurrentState == EAgentState::Broken) ? ERepairType::FullRepair : ERepairType::QuickCheck;

		UE_LOG(LogFactoryDispatch, Log, TEXT("[Repair] 대기열에서 %s를 %s 정비(%s)로 재배정(잔여 대기 %d건)"),
			*NPC->GetName(), *Agent->GetName(), RepairType == ERepairType::FullRepair ? TEXT("FullRepair") : TEXT("QuickCheck"), PendingMaintenanceQueue.Num());
		NPC->AssignMaintenance(Agent, RepairType);
		return true;
	}

	return false;
}

void AMSmartFactoryManager::DebugForceRandomBreakdown()
{
	if (!HasAuthority())
	{
		return;
	}

	TArray<AActor*> FoundAgents;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFactoryAgentBase::StaticClass(), FoundAgents);

	TArray<AFactoryAgentBase*> Candidates;
	for (AActor* Actor : FoundAgents)
	{
		AFactoryAgentBase* Agent = Cast<AFactoryAgentBase>(Actor);
		// GetRepairComponent()가 nullptr이 아닌 것만 Atlas/TransportRobot(정비 대상) — NPCHuman은 자연히 제외된다.
		// Idle로 한정하는 이유: 실제 EvaluateRotationOrContinue도 Working 진입 시점에만 고장을 롤링해,
		// 이동/작업 중인 로봇이 강제로 멈추는 경우는 원본 설계에 없다 — 디버그 훅도 동일한 전제를 유지한다.
		if (Agent && Agent->GetRepairComponent() && Agent->CurrentState == EAgentState::Idle)
		{
			Candidates.Add(Agent);
		}
	}

	if (Candidates.Num() == 0)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Repair][Debug] 강제 고장 실패 — Idle 상태인 Atlas/TransportRobot 후보가 없음"));
		return;
	}

	AFactoryAgentBase* Target = Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Repair][Debug] %s를 강제 고장 대상으로 선택(Idle 후보 %d개 중)"), *Target->GetName(), Candidates.Num());
	Target->DebugForceBreakdown();
}

void AMSmartFactoryManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMSmartFactoryManager, ReputationScore);
	DOREPLIFETIME(AMSmartFactoryManager, ItemTypeToMeshSlot);
	DOREPLIFETIME(AMSmartFactoryManager, SharedFunds);
	DOREPLIFETIME(AMSmartFactoryManager, VendorOrderDisplays);
}
