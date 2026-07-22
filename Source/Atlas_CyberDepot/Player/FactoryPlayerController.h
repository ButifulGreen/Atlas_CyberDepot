// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Infrastructure/FactoryKioskTerminal.h"
#include "FactoryPlayerController.generated.h"

class AFactoryNPCHuman;
class UActorComponent;

// Docs/02_Multiplayer_RPC.md §2 — 8단계. Role 검증 없이 전원 동일 권한으로 호출 가능한 RPC 5종.
// 각 _Implementation은 최소 재검증 후 서버측 서브시스템을 호출하는 얇은 위임으로만 구현한다.
UCLASS()
class AFactoryPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	UFUNCTION(Server, Reliable)
	void Server_RequestPossessNPC(AFactoryNPCHuman* TargetNPC);

	// 원안(Docs)에는 없던 RPC — 관전자 복귀(빙의 해제)를 8단계 범위에 포함하기로 확정하며 추가.
	UFUNCTION(Server, Reliable)
	void Server_ReleaseNPC();

	UFUNCTION(Server, Reliable)
	void Server_SubmitKioskOrder(AFactoryKioskTerminal* SourceKiosk, FKioskOrderRequest Request);

	UFUNCTION(Server, Reliable)
	void Server_JoinRepair(UActorComponent* TargetRepairComponent);

	UFUNCTION(Server, Reliable)
	void Server_LeaveRepair(UActorComponent* TargetRepairComponent);

protected:
	virtual void OnPossess(APawn* InPawn) override;

private:
	// 빙의 전 스폰됐던 자신의 관전자 폰 — Server_ReleaseNPC에서 복귀 대상으로 사용
	UPROPERTY()
	TWeakObjectPtr<APawn> OriginalSpectatorPawn;
};
