// Copyright Epic Games, Inc. All Rights Reserved.

#include "Repair/RepairProgressComponent.h"
#include "Agent/FactoryAgentBase.h"
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
	RepairProgress = FMath::Min(RepairProgress + BaseRepairRate * RepairerCount * DeltaTime, TargetDuration);

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

	RepairProgress = 0.f;
	ActiveRepairers.Reset();

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

	if (UWorld* World = Owner->GetWorld())
	{
		if (AMSmartFactoryManager* Manager = World->GetGameState<AMSmartFactoryManager>())
		{
			Manager->OnRepairCompleted(Owner);
		}
	}
}

void URepairProgressComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(URepairProgressComponent, RepairProgress);
}
