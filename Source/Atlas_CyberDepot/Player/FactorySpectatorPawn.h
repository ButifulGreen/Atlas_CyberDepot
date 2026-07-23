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
class ALogisticsItem;
class UReplayControlWidget;

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

	// Docs/10_Benchmark_Replay.md — 리플레이 재생 토글(사용자 지시). 재생 중이 아니면 방금까지 기록된
	// 세션을 불러와 재생을 시작하고, 재생 중이면 정지+고스트 정리. 관전자 전용(빙의 중엔 미지원).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ToggleReplayAction;

	// 우클릭을 누르고 있는 동안만 시야 회전을 허용하는 홀드 입력(사용자 지시, 2026-07-23 — 리플레이
	// 컨트롤 패널이 열려있는 동안에도 자유이동/시야회전을 계속 쓸 수 있어야 해서, 에디터 뷰포트와 동일한
	// 방식으로 도입). 패널이 닫혀있을 땐 아무 효과가 없다(OnReplayLookHoldStarted/Released 참고).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ReplayLookHoldAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Interact")
	float InteractTraceDistance = 300.f;

	// 키오스크 인터렉트 시 띄울 위젯 블루프린트 — UMG 디자이너에서 UVendorOrderListWidget을 상속한 WBP를
	// 만들어 할당해야 한다(에셋이라 코드로 생성 불가, 다른 Input 에셋과 동일한 사유).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TSubclassOf<UVendorOrderListWidget> KioskWidgetClass;

	// 재생 컨트롤(일시정지/탐색/배속) 패널 위젯 블루프린트 — UMG 디자이너에서 UReplayControlWidget을
	// 상속한 WBP를 만들어 할당해야 한다(다른 위젯 클래스 슬롯과 동일한 사유로 코드로 생성 불가).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TSubclassOf<UReplayControlWidget> ReplayControlWidgetClass;


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
	void OnToggleReplayTriggered(const FInputActionValue& Value);
	AActor* FindInteractableInFrontOfCamera() const;

	// 좌클릭으로 키오스크를 다시 누르면 토글로 닫힌다.
	void OpenKioskWidget(AFactoryKioskTerminal* Kiosk);
	void CloseKioskWidget();

	// 재생 시작/정지에 맞춰 AFactorySpectatorPawn::OnToggleReplayTriggered가 자동으로 열고 닫는다(사용자
	// 지시 — 별도 토글 없이 재생 진입 즉시 보여야 함). 키오스크와 달리 열려있는 동안에도 이동은 계속
	// 가능하고, 시야 회전만 기본적으로 막았다가 ReplayLookHoldAction(우클릭) 홀드 중에만 허용한다.
	void OpenReplayControlWidget();
	void CloseReplayControlWidget();
	void OnReplayLookHoldStarted(const FInputActionValue& Value);
	void OnReplayLookHoldReleased(const FInputActionValue& Value);

	// 재생 시작 시 호출. 처음엔 스냅샷 중심/반경 기준 자동 프레이밍으로 순간이동시켰으나 건물 밖 등으로
	// 튀는 문제가 있어(사용자 리포트, 2026-07-23) 폐기 — 지금은 위치를 옮기지 않고 자유비행 카메라가 있던
	// 자리 그대로 재생을 시작한다.
	void BeginReplayCameraView();
	// 재생 종료 시 재생 시작 전 위치/시점으로 되돌린다.
	void EndReplayCameraView();
	// 버그 수정(사용자 리포트) — 재생 중엔 실시간으로 계속 바뀌는 실물(로봇/NPC뿐 아니라 선반 위 물류
	// 아이템 등)이 고스트와 겹쳐 보이면 안 된다. 새로운 종류가 더 필요해지면 이 함수 안에서만 늘리면 된다.
	// 버그 수정(사용자 리포트, 2차) — ALogisticsItem은 bHidden이 순수 렌더링 상태가 아니라
	// ALogisticsItemSpawner::TryAcquireItem이 "재고 풀에서 아직 안 쓰인 아이템"을 판정하는 실제 게임
	// 로직 기준(Item->IsHidden())이다. 그래서 const로 두고 무차별로 숨기고/보이면, 풀에서 대기 중이던
	// (원래부터 숨겨져 있던) 아이템까지 강제로 보이게 되어 스포너 위치에 물품이 쌓여 보이고, 동시에 모든
	// 아이템이 "숨겨져 있지 않음"이 되어 신규 주문이 아이템을 하나도 못 받는 문제가 있었다. 더 이상 const가
	// 아님 — 실제로 화면에 보이던(사용 중이던) 것만 숨기고 그 목록을 기억해뒀다가, 되돌릴 때 그것만 정확히
	// 복원한다(풀 대기 아이템은 아예 건드리지 않음).
	void SetLiveWorldActorsHidden(bool bShouldHide);

	UPROPERTY()
	TObjectPtr<UVendorOrderListWidget> ActiveKioskWidget;

	UPROPERTY()
	TObjectPtr<UReplayControlWidget> ActiveReplayControlWidget;

	// EndReplayCameraView가 복귀시킬 재생 시작 직전 위치/시점.
	FVector PreReplayCameraLocation = FVector::ZeroVector;
	FRotator PreReplayCameraRotation = FRotator::ZeroRotator;

	// SetLiveWorldActorsHidden(true)가 실제로 숨긴(=재생 시작 시점에 이미 보이고 있던) 물류 아이템만
	// 여기 기억해뒀다가 false 호출 때 이것만 되돌린다.
	UPROPERTY()
	TArray<TObjectPtr<ALogisticsItem>> LogisticsItemsHiddenForReplay;
};
