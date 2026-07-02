// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpectatorPawn.h"
#include "FactorySpectatorPawn.generated.h"

class AFactoryNPCHuman;
class AFactoryKioskTerminal;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

// Docs/02_Multiplayer_RPC.md §2 — 8단계. 접속 시 시작하는 1인칭 관전자 폰.
// 물류센터 외곽(FactoryBoundary 콜리전 채널)만 막고 사물/로봇/NPC는 통과한다.
UCLASS()
class AFactorySpectatorPawn : public ASpectatorPawn
{
	GENERATED_BODY()

public:
	AFactorySpectatorPawn();

	// Enhanced Input 에셋은 코드로 생성할 수 없어 에디터에서 직접 만들어 할당해야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Interact")
	float InteractTraceDistance = 300.f;

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	void OnInteractTriggered(const FInputActionValue& Value);
	AActor* FindInteractableInFrontOfCamera() const;
};
