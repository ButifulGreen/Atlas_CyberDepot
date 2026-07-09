// Copyright Epic Games, Inc. All Rights Reserved.

#include "Repair/RepairProgressComponent.h"
#include "Atlas_CyberDepot.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/FactoryNPCHuman.h"
#include "Agent/FactoryAIController.h"
#include "Assignment/SmartFactoryManager.h"
#include "EventBus/FactoryEventBusSubsystem.h"
#include "Net/UnrealNetwork.h"

URepairProgressComponent::URepairProgressComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void URepairProgressComponent::BeginPlay()
{
	Super::BeginPlay();
}

void URepairProgressComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickRepairProgress(DeltaTime);
}

void URepairProgressComponent::Server_JoinRepair(AFactoryAgentBase* Repairer)
{
	if (!Repairer)
	{
		return;
	}

	for (const TWeakObjectPtr<AFactoryAgentBase>& Existing : ActiveRepairers)
	{
		if (Existing.Get() == Repairer)
		{
			return;
		}
	}

	ActiveRepairers.Add(Repairer);
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Repair] %s가 %s 정비에 합류 — 진행 시작(참여 인원 %d명)"),
		*Repairer->GetName(), GetOwner() ? *GetOwner()->GetName() : TEXT("?"), GetValidRepairerCount());
}

void URepairProgressComponent::Server_LeaveRepair(AFactoryAgentBase* Repairer)
{
	if (!Repairer)
	{
		return;
	}

	ActiveRepairers.RemoveAll([Repairer](const TWeakObjectPtr<AFactoryAgentBase>& Entry)
	{
		return Entry.Get() == Repairer;
	});

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Repair] %s가 %s 정비에서 이탈(잔여 참여 인원 %d명)"),
		*Repairer->GetName(), GetOwner() ? *GetOwner()->GetName() : TEXT("?"), GetValidRepairerCount());
}

int32 URepairProgressComponent::GetValidRepairerCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AFactoryAgentBase>& Repairer : ActiveRepairers)
	{
		if (Repairer.IsValid())
		{
			++Count;
		}
	}

	return Count;
}

float URepairProgressComponent::GetTargetDurationSeconds() const
{
	return CurrentRepairType == ERepairType::FullRepair ? FullRepairDurationSeconds : QuickCheckDurationSeconds;
}

void URepairProgressComponent::TickRepairProgress(float DeltaTime)
{
	AFactoryAgentBase* Owner = Cast<AFactoryAgentBase>(GetOwner());
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	const int32 RepairerCount = GetValidRepairerCount();
	if (RepairerCount <= 0)
	{
		// ActiveRepairers가 0명이 되어도 RepairProgress는 보존되고 진행 속도만 0이 된다.
		return;
	}

	const float TargetDuration = GetTargetDurationSeconds();
	const float PreviousProgress = RepairProgress;
	RepairProgress = FMath::Min(RepairProgress + BaseRepairRate * RepairerCount * DeltaTime, TargetDuration);

	// 매 틱 로그는 스팸이라 25% 문턱값을 넘을 때만 진행 상황을 남긴다 — 진행이 실제로 이뤄지고 있다는 증거용.
	if (TargetDuration > 0.f)
	{
		const int32 PrevStep = FMath::FloorToInt(PreviousProgress / TargetDuration * 4.f);
		const int32 NewStep = FMath::FloorToInt(RepairProgress / TargetDuration * 4.f);
		if (NewStep > PrevStep)
		{
			UE_LOG(LogFactoryDispatch, Log, TEXT("[Repair] %s 수리 진행률 %.0f%% (참여 인원 %d명)"),
				*Owner->GetName(), RepairProgress / TargetDuration * 100.f, RepairerCount);
		}
	}

	if (RepairProgress >= TargetDuration)
	{
		OnRepairCompleted();
	}
}

void URepairProgressComponent::OnRepairCompleted()
{
	AFactoryAgentBase* Owner = Cast<AFactoryAgentBase>(GetOwner());
	if (!Owner)
	{
		return;
	}

	// OperationCount를 넉넉한 값만큼 감쇠시켜 0으로 리셋 (INT32_MAX를 직접 빼면 부호있는 정수 오버플로 위험이 있어
	// 실제로 도달할 수 없는 값이지만 안전한 큰 상수를 사용)
	Owner->ApplyRestDecay(1000000);
	Owner->ResumeAfterRepair();

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Repair] %s 정비 완료 — Broken 해제, CurrentState=%s로 복귀"),
		*Owner->GetName(), *UEnum::GetValueAsString(Owner->CurrentState));

	AMSmartFactoryManager* Manager = Owner->GetWorld() ? Owner->GetWorld()->GetGameState<AMSmartFactoryManager>() : nullptr;

	// 정비자를 통보 없이 방치하면 AI NPC는 UnderRepair에 영구히 멈춰 FindNearestAvailableNPC 후보에서
	// 계속 빠진다. Reset 전에 스냅샷을 떠서(재귀적으로 Server_LeaveRepair가 같은 배열을 건드리는 걸 피함)
	// AI 제어 중인 NPC만 처리한다 — 빙의 중인 플레이어는 8단계 몫이라 여기서 움직이지 않는다.
	const TArray<TWeakObjectPtr<AFactoryAgentBase>> RepairersSnapshot = ActiveRepairers;

	RepairProgress = 0.f;
	ActiveRepairers.Reset();

	for (const TWeakObjectPtr<AFactoryAgentBase>& RepairerPtr : RepairersSnapshot)
	{
		AFactoryNPCHuman* NPC = Cast<AFactoryNPCHuman>(RepairerPtr.Get());
		if (!NPC || !Cast<AFactoryAIController>(NPC->GetController()))
		{
			continue;
		}

		// 버그 수정 — 정비자 없이 대기열에 쌓여있던 다른 Broken 로봇이 있으면 그쪽을 우선 배정하고,
		// 없을 때만 사무실로 복귀시킨다(예전엔 무조건 복귀시켜 대기열이 영원히 안 풀렸다).
		if (Manager && Manager->TryAssignNextPendingMaintenance(NPC))
		{
			continue;
		}

		UE_LOG(LogFactoryDispatch, Log, TEXT("[Repair] %s 정비 종료 — 사무실로 복귀"), *NPC->GetName());
		NPC->ReturnToOfficeRoom();
	}

	if (UGameInstance* GI = Owner->GetGameInstance())
	{
		if (UFactoryEventBusSubsystem* EventBus = GI->GetSubsystem<UFactoryEventBusSubsystem>())
		{
			FAnomalyEvent Event;
			Event.Timestamp = FDateTime::UtcNow();
			Event.LogID = FGuid::NewGuid();
			Event.Severity = EEventSeverity::Info;
			Event.ActorID = Owner->AgentID;
			Event.ActorType = Owner->AgentType;
			Event.AnomalyCode = TEXT("Code:006");
			Event.Location = Owner->GetActorLocation();
			EventBus->PublishAnomaly(Event);
		}
	}

	if (Manager)
	{
		Manager->OnRepairCompleted(Owner);
	}
}

void URepairProgressComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(URepairProgressComponent, RepairProgress);
}
