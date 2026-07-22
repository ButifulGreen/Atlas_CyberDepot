// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/FactoryPlayerController.h"
#include "Atlas_CyberDepot.h"
#include "Player/FactorySpectatorPawn.h"
#include "Infrastructure/FactoryKioskTerminal.h"
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
	// 버그 수정(로깅 규율) — 이 RPC가 서버에 실제로 도착했는지조차 로그가 없어 확인할 수 없었다(클라이언트
	// 쪽 IA_Interact/트레이스 문제와 서버 쪽 빙의 거부를 구분 불가).
	if (!TargetNPC)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] Server_RequestPossessNPC 무시 — TargetNPC가 없음(RPC는 도착함)"), *GetName());
		return;
	}

	if (!TargetNPC->CanBePossessedBy(this))
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] Server_RequestPossessNPC 무시 — %s는 이미 다른 플레이어가 빙의 중"), *GetName(), *TargetNPC->GetName());
		return;
	}

	UE_LOG(LogFactoryDispatch, Log, TEXT("[%s] %s 빙의 성공"), *GetName(), *TargetNPC->GetName());
	Possess(TargetNPC);
}

void AFactoryPlayerController::Server_ReleaseNPC_Implementation()
{
	AFactoryNPCHuman* PossessedNPC = Cast<AFactoryNPCHuman>(GetPawn());
	APawn* TargetSpectatorPawn = OriginalSpectatorPawn.Get();
	if (!PossessedNPC || !TargetSpectatorPawn)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] Server_ReleaseNPC 무시 — PossessedNPC=%s, OriginalSpectatorPawn=%s"),
			*GetName(), PossessedNPC ? TEXT("있음") : TEXT("없음"), TargetSpectatorPawn ? TEXT("있음") : TEXT("없음"));
		return;
	}

	PossessedNPC->ReleasePossession();
	Possess(TargetSpectatorPawn);
}

void AFactoryPlayerController::Server_SubmitKioskOrder_Implementation(AFactoryKioskTerminal* SourceKiosk, FKioskOrderRequest Request)
{
	UWorld* World = GetWorld();
	if (!SourceKiosk || !World)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] Server_SubmitKioskOrder 무시 — SourceKiosk=%s, World=%s"),
			*GetName(), SourceKiosk ? TEXT("있음") : TEXT("없음"), World ? TEXT("있음") : TEXT("없음"));
		return;
	}

	// 버그 수정(사용자 지시) — 거리 재검증(KioskInteractRadius) 개념 삭제. 위젯을 여는 시점(인터렉트 사거리)에
	// 이미 근접을 확인했으므로 여기서 다시 재는 건 이중 체크였고, 두 사거리 값이 서로 어긋나면 "위젯은
	// 열리는데 버튼만 안 먹는" 간극이 생겼다. 위젯이 열려있다는 사실 자체가 유효한 주문 자격이다.
	ApplyKioskOrderRequest(World, Request);
}

void AFactoryPlayerController::Server_JoinRepair_Implementation(UActorComponent* TargetRepairComponent)
{
	URepairProgressComponent* RepairComponent = Cast<URepairProgressComponent>(TargetRepairComponent);
	AFactoryNPCHuman* PossessedNPC = Cast<AFactoryNPCHuman>(GetPawn());
	if (!RepairComponent || !PossessedNPC)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] Server_JoinRepair 무시 — RepairComponent=%s, PossessedNPC=%s"),
			*GetName(), RepairComponent ? TEXT("있음") : TEXT("없음"), PossessedNPC ? TEXT("있음") : TEXT("없음"));
		return;
	}

	PossessedNPC->JoinRepairAsPlayer(RepairComponent);
}

void AFactoryPlayerController::Server_LeaveRepair_Implementation(UActorComponent* TargetRepairComponent)
{
	// 버그 수정(8단계) — 인자로 받은 컴포넌트를 그대로 신뢰하지 않고, NPC 자신이 서버 권위로 들고 있는
	// JoinedRepairComponent를 이탈시킨다(클라이언트가 지연된/잘못된 값을 보내도 안전).
	if (AFactoryNPCHuman* PossessedNPC = Cast<AFactoryNPCHuman>(GetPawn()))
	{
		PossessedNPC->LeaveRepairAsPlayer();
	}
}
