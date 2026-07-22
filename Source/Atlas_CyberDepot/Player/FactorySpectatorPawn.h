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
	// 버그 수정 — 매핑 컨텍스트 추가를 BeginPlay()가 아니라 여기서 한다. BeginPlay()는 GameMode가 기본 폰을
	// 스폰하는 시점에 실행되는데 이때는 아직 컨트롤러가 빙의(Possess)하기 전이라 GetController()가 항상
	// nullptr이었다 — Enhanced Input 액션(IA_Interact)이 전혀 반응하지 않는 원인이었다(레거시 축 입력
	// 기반 자유비행 이동은 이 컨텍스트와 무관해 정상 동작했던 것과 대비됨).
	virtual void PossessedBy(AController* NewController) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	void OnInteractTriggered(const FInputActionValue& Value);
	AActor* FindInteractableInFrontOfCamera() const;
};
