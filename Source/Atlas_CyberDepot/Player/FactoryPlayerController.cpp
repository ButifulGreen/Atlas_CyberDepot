// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/FactoryPlayerController.h"
#include "Atlas_CyberDepot.h"
#include "Player/FactorySpectatorPawn.h"
#include "Infrastructure/FactoryKioskTerminal.h"
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
		// 버그 수정 — 이 세 경로가 로그 없이 조용히 실패해서, BoundKiosk가 비어있는 경우(위젯 바인딩
		// 문제)와 실제 배차 실패를 구분할 수 없었다.
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] Server_SubmitKioskOrder 무시 — SourceKiosk=%s, World=%s, MyPawn=%s"),
			*GetName(), SourceKiosk ? TEXT("있음") : TEXT("없음"), World ? TEXT("있음") : TEXT("없음"), MyPawn ? TEXT("있음") : TEXT("없음"));
		return;
	}

	const float DistSq = FVector::DistSquared(MyPawn->GetActorLocation(), SourceKiosk->GetActorLocation());
	if (DistSq > FMath::Square(KioskInteractRadius))
	{
		// 버그 수정 — 거리 초과도 로그 없이 조용히 실패했다. 상시 노출 UI(대시보드형 위젯)에서 버튼을
		// 누르는 시나리오는 실제 키오스크 물리적 근접과 무관할 수 있어, 이 실패가 흔한 원인일 수 있다.
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] Server_SubmitKioskOrder 무시 — %s와 거리 초과(%.0f > %.0f)"),
			*GetName(), *SourceKiosk->GetName(), FMath::Sqrt(DistSq), KioskInteractRadius);
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
