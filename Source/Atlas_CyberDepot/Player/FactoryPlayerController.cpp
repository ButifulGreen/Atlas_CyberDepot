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
