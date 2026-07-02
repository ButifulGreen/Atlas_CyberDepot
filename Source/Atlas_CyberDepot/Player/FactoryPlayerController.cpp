// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/FactoryPlayerController.h"
#include "Player/FactorySpectatorPawn.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/FactoryNPCHuman.h"
#include "Repair/RepairProgressComponent.h"

void AFactoryPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!OriginalSpectatorPawn.IsValid() && Cast<AFactorySpectatorPawn>(InPawn))
	{
		OriginalSpectatorPawn = InPawn;
	}
}

void AFactoryPlayerController::Server_RequestPossessNPC_Implementation(AFactoryNPCHuman* TargetNPC)
{
	if (!TargetNPC || !TargetNPC->CanBePossessedBy(this))
	{
		return;
	}

	Possess(TargetNPC);
}

void AFactoryPlayerController::Server_ReleaseNPC_Implementation()
{
	AFactoryNPCHuman* PossessedNPC = Cast<AFactoryNPCHuman>(GetPawn());
	APawn* TargetSpectatorPawn = OriginalSpectatorPawn.Get();
	if (!PossessedNPC || !TargetSpectatorPawn)
	{
		return;
	}

	PossessedNPC->ReleasePossession();
	Possess(TargetSpectatorPawn);
}

void AFactoryPlayerController::Server_SubmitKioskOrder_Implementation(AFactoryKioskTerminal* SourceKiosk, FKioskOrderRequest Request)
{
	UWorld* World = GetWorld();
	APawn* MyPawn = GetPawn();
	if (!SourceKiosk || !World || !MyPawn)
	{
		return;
	}

	const float DistSq = FVector::DistSquared(MyPawn->GetActorLocation(), SourceKiosk->GetActorLocation());
	if (DistSq > FMath::Square(KioskInteractRadius))
	{
		return;
	}

	ApplyKioskOrderRequest(World, Request);
}

void AFactoryPlayerController::Server_JoinRepair_Implementation(UActorComponent* TargetRepairComponent)
{
	URepairProgressComponent* RepairComponent = Cast<URepairProgressComponent>(TargetRepairComponent);
	AFactoryAgentBase* PossessedAgent = Cast<AFactoryAgentBase>(GetPawn());
	if (!RepairComponent || !PossessedAgent)
	{
		return;
	}

	RepairComponent->Server_JoinRepair(PossessedAgent);
}

void AFactoryPlayerController::Server_LeaveRepair_Implementation(UActorComponent* TargetRepairComponent)
{
	URepairProgressComponent* RepairComponent = Cast<URepairProgressComponent>(TargetRepairComponent);
	AFactoryAgentBase* PossessedAgent = Cast<AFactoryAgentBase>(GetPawn());
	if (!RepairComponent || !PossessedAgent)
	{
		return;
	}

	RepairComponent->Server_LeaveRepair(PossessedAgent);
}
