// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Infrastructure/FactoryKioskTerminal.h"
#include "FactoryPlayerController.generated.h"

class AFactoryNPCHuman;
class UActorComponent;

// Docs/02_Multiplayer_RPC.md §2 — 원래 8단계용으로 Role 검증 없이 전원 동일 권한으로 호출 가능한 RPC
// 5종이었으나, 기획 변경(2026-07-24 — "AI 학습 데이터 시뮬레이터"로 목적 재정의, 실제 2인 동시 협업이
// 불필요하다고 판단, `Docs/14_OpenIssues.md` 참고)으로 RPC 자체를 제거하고 평범한 함수 호출로 바꿨다.
// 함수 이름/시그니처/내부 로직은 그대로 — 호출부(AFactorySpectatorPawn 등)는 전혀 안 건드려도 그대로
// 동작한다(리슨 서버 호스트 입장에서 RPC 호출과 직접 호출의 실행 결과가 동일하기 때문). 원래 RPC 선언은
// 각 함수 위에 주석으로 남겨둔다.
UCLASS()
class AFactoryPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// UFUNCTION(Server, Reliable)
	void Server_RequestPossessNPC(AFactoryNPCHuman* TargetNPC);

	// 원안(Docs)에는 없던 RPC — 관전자 복귀(빙의 해제)를 8단계 범위에 포함하기로 확정하며 추가.
	// UFUNCTION(Server, Reliable)
	void Server_ReleaseNPC();

	// UFUNCTION(Server, Reliable)
	void Server_SubmitKioskOrder(AFactoryKioskTerminal* SourceKiosk, FKioskOrderRequest Request);

	// UFUNCTION(Server, Reliable)
	void Server_JoinRepair(UActorComponent* TargetRepairComponent);

	// UFUNCTION(Server, Reliable)
	void Server_LeaveRepair(UActorComponent* TargetRepairComponent);

protected:
	virtual void OnPossess(APawn* InPawn) override;

private:
	// 빙의 전 스폰됐던 자신의 관전자 폰 — Server_ReleaseNPC에서 복귀 대상으로 사용
	UPROPERTY()
	TWeakObjectPtr<APawn> OriginalSpectatorPawn;
};
