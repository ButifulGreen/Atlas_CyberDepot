// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryAgentBase.h"
#include "Net/UnrealNetwork.h"

AFactoryAgentBase::AFactoryAgentBase()
{
	bReplicates = true;
}

void AFactoryAgentBase::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && !AgentID.IsValid())
	{
		AgentID = FGuid::NewGuid();
	}
}

void AFactoryAgentBase::SetState(EAgentState NewState)
{
	if (CurrentState == NewState)
	{
		return;
	}

	CurrentState = NewState;

	// 서버 자신에게는 OnRep이 자동 호출되지 않으므로 명시적으로 호출
	if (HasAuthority())
	{
		OnRep_CurrentState();
	}
}

void AFactoryAgentBase::OnBlockedTick(float DeltaTime)
{
}

void AFactoryAgentBase::OnUnblocked()
{
}

FStateSnapshot AFactoryAgentBase::ToSnapshot() const
{
	FStateSnapshot Snapshot;
	Snapshot.Timestamp = FDateTime::UtcNow();
	Snapshot.ActorID = AgentID;
	Snapshot.ActorType = AgentType;
	Snapshot.CurrentState = CurrentState;
	Snapshot.Location = GetActorLocation();
	Snapshot.Rotation = GetActorRotation();
	Snapshot.Velocity = GetVelocity();
	return Snapshot;
}

void AFactoryAgentBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFactoryAgentBase, AgentID);
	DOREPLIFETIME(AFactoryAgentBase, CurrentState);
}

void AFactoryAgentBase::OnRep_CurrentState()
{
	// 하위 클래스에서 상태 전환 시각/애니메이션 반응을 위해 오버라이드
}
