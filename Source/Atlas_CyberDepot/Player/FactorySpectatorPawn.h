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
class UVendorOrderListWidget;

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

	// F 전용 — NPC 빙의만 담당(사용자 지시로 정비/키오스크와 분리).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	// 좌클릭 전용 — 키오스크 상호작용(F키와 겹쳐서 빙의 해제가 씹히던 문제 해소, 사용자 지시).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ClickAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Interact")
	float InteractTraceDistance = 300.f;

	// 키오스크 인터렉트 시 띄울 위젯 블루프린트 — UMG 디자이너에서 UVendorOrderListWidget을 상속한 WBP를
	// 만들어 할당해야 한다(에셋이라 코드로 생성 불가, 다른 Input 에셋과 동일한 사유).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TSubclassOf<UVendorOrderListWidget> KioskWidgetClass;

protected:
	// 버그 수정 — 매핑 컨텍스트 추가를 BeginPlay()가 아니라 여기서 한다. BeginPlay()는 GameMode가 기본 폰을
	// 스폰하는 시점에 실행되는데 이때는 아직 컨트롤러가 빙의(Possess)하기 전이라 GetController()가 항상
	// nullptr이었다 — Enhanced Input 액션(IA_Interact)이 전혀 반응하지 않는 원인이었다(레거시 축 입력
	// 기반 자유비행 이동은 이 컨텍스트와 무관해 정상 동작했던 것과 대비됨).
	virtual void PossessedBy(AController* NewController) override;
	// 키오스크 위젯이 열린 채로 NPC를 빙의하면 입력모드/마우스 커서가 GameAndUI로 눌러붙는 문제 방지.
	virtual void UnPossessed() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	void OnInteractTriggered(const FInputActionValue& Value);
	void OnClickTriggered(const FInputActionValue& Value);
	AActor* FindInteractableInFrontOfCamera() const;

	// 좌클릭으로 키오스크를 다시 누르면 토글로 닫힌다.
	void OpenKioskWidget(AFactoryKioskTerminal* Kiosk);
	void CloseKioskWidget();

	UPROPERTY()
	TObjectPtr<UVendorOrderListWidget> ActiveKioskWidget;
};
