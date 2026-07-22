// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/FactorySpectatorPawn.h"
#include "Atlas_CyberDepot.h"
#include "Player/FactoryPlayerController.h"
#include "Agent/FactoryNPCHuman.h"
#include "Infrastructure/FactoryKioskTerminal.h"
#include "Visualization/VendorOrderListWidget.h"
#include "Assignment/SmartFactoryManager.h"
#include "Components/SphereComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Blueprint/UserWidget.h"

// 프로젝트 전용 관전자 경계 채널 — Config/DefaultEngine.ini에서 "FactoryBoundary"로 이름 부여
static constexpr ECollisionChannel FactoryBoundaryChannel = ECC_GameTraceChannel1;

AFactorySpectatorPawn::AFactorySpectatorPawn()
{
	if (USphereComponent* Collision = GetCollisionComponent())
	{
		Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
		Collision->SetCollisionResponseToChannel(FactoryBoundaryChannel, ECR_Block);
	}
}

void AFactorySpectatorPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	APlayerController* PC = Cast<APlayerController>(NewController);
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Subsystem)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: EnhancedInputLocalPlayerSubsystem을 못 찾음(PC=%s)"), *GetName(), PC ? TEXT("있음") : TEXT("없음"));
		return;
	}

	if (!DefaultMappingContext)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: DefaultMappingContext가 비어있음 — BP에서 IMC_Default 할당 필요"), *GetName());
		return;
	}

	Subsystem->AddMappingContext(DefaultMappingContext, 0);
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: DefaultMappingContext 추가 완료"), *GetName());
}

void AFactorySpectatorPawn::UnPossessed()
{
	// NPC 빙의 등으로 컨트롤러가 넘어가기 전에 정리 — Super::UnPossessed()가 Controller를 비우므로 먼저 처리한다.
	CloseKioskWidget();
	Super::UnPossessed();
}

void AFactorySpectatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 버그 수정(로깅 규율) — 바인딩 성공/실패를 조용히 넘기면 IA_Interact/DefaultMappingContext 할당 누락과
	// 다른 원인(콜리전, 트리거 설정 등)을 구분할 방법이 없다.
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: PlayerInputComponent가 UEnhancedInputComponent가 아님(Enhanced Input 플러그인/프로젝트 설정 확인)"), *GetName());
		return;
	}

	if (!InteractAction)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: InteractAction이 비어있어 바인딩하지 못함 — BP에서 IA_Interact 할당 필요"), *GetName());
	}
	else
	{
		EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &AFactorySpectatorPawn::OnInteractTriggered);
	}

	if (!ClickAction)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: ClickAction이 비어있어 바인딩하지 못함 — BP에서 IA_Click 할당 필요"), *GetName());
	}
	else
	{
		EnhancedInput->BindAction(ClickAction, ETriggerEvent::Started, this, &AFactorySpectatorPawn::OnClickTriggered);
	}

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 입력 바인딩 완료"), *GetName());
}

AActor* AFactorySpectatorPawn::FindInteractableInFrontOfCamera() const
{
	FVector ViewLocation;
	FRotator ViewRotation;
	GetActorEyesViewPoint(ViewLocation, ViewRotation);

	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * InteractTraceDistance;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, QueryParams))
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 트레이스 히트 없음(%.0f 거리 내 Visibility 채널에 아무것도 안 맞음)"), *GetName(), InteractTraceDistance);
		return nullptr;
	}

	AActor* HitActor = Hit.GetActor();
	if (Cast<AFactoryNPCHuman>(HitActor) || Cast<AFactoryKioskTerminal>(HitActor))
	{
		return HitActor;
	}

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 트레이스가 %s에 맞았지만 NPC/키오스크가 아님"), *GetName(), HitActor ? *HitActor->GetName() : TEXT("Invalid"));
	return nullptr;
}

void AFactorySpectatorPawn::OnInteractTriggered(const FInputActionValue& Value)
{
	// 사용자 지시 — F는 NPC 빙의 전용(정비/키오스크는 좌클릭, OnClickTriggered 참고).
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: IA_Interact 트리거됨"), *GetName());

	AFactoryNPCHuman* NPC = Cast<AFactoryNPCHuman>(FindInteractableInFrontOfCamera());
	if (!NPC)
	{
		return;
	}

	AFactoryPlayerController* FactoryPC = Cast<AFactoryPlayerController>(GetController());
	if (!FactoryPC)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: AFactoryPlayerController 캐스트 실패(GetController()=%s)"),
			*GetName(), GetController() ? *GetController()->GetClass()->GetName() : TEXT("None"));
		return;
	}

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: %s 빙의 요청(Server_RequestPossessNPC)"), *GetName(), *NPC->GetName());
	FactoryPC->Server_RequestPossessNPC(NPC);
}

void AFactorySpectatorPawn::OnClickTriggered(const FInputActionValue& Value)
{
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: IA_Click 트리거됨"), *GetName());

	// 위젯이 열려있으면 이 클릭은 무조건 닫기 전용 — 열린 동안은 이동/시점이 멈춰 있어 새로 다른 상호작용을
	// 시작할 상황 자체가 아니다(사용자 지시).
	if (ActiveKioskWidget)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 키오스크 위젯 닫기(토글)"), *GetName());
		CloseKioskWidget();
		return;
	}

	if (AFactoryKioskTerminal* Kiosk = Cast<AFactoryKioskTerminal>(FindInteractableInFrontOfCamera()))
	{
		OpenKioskWidget(Kiosk);
	}
}

void AFactorySpectatorPawn::OpenKioskWidget(AFactoryKioskTerminal* Kiosk)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !KioskWidgetClass)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: 키오스크 위젯을 열 수 없음(PC=%s, KioskWidgetClass=%s)"),
			*GetName(), PC ? TEXT("있음") : TEXT("없음"), KioskWidgetClass ? TEXT("있음") : TEXT("비어있음 — BP에서 KioskWidgetClass 할당 필요"));
		return;
	}

	ActiveKioskWidget = CreateWidget<UVendorOrderListWidget>(PC, KioskWidgetClass);
	if (!ActiveKioskWidget)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: CreateWidget(KioskWidgetClass) 실패"), *GetName());
		return;
	}

	if (AMSmartFactoryManager* Manager = GetWorld()->GetGameState<AMSmartFactoryManager>())
	{
		ActiveKioskWidget->BindToManager(Manager);
	}
	else
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: AMSmartFactoryManager(GameState)를 못 찾음 — 위젯이 주문 목록을 못 받음"), *GetName());
	}
	ActiveKioskWidget->BindToKiosk(Kiosk);
	ActiveKioskWidget->AddToViewport();

	// 사용자 지시 — 위젯이 열린 동안은 캐릭터 이동/시야 회전 없이 마우스 커서만 움직일 수 있어야 한다.
	PC->SetInputMode(FInputModeGameAndUI());
	PC->SetShowMouseCursor(true);
	PC->SetIgnoreMoveInput(true);
	PC->SetIgnoreLookInput(true);

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 키오스크(%s) 위젯 열림"), *GetName(), *Kiosk->GetName());
}

void AFactorySpectatorPawn::CloseKioskWidget()
{
	if (!ActiveKioskWidget)
	{
		return;
	}

	ActiveKioskWidget->RemoveFromParent();
	ActiveKioskWidget = nullptr;

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);
	}

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 키오스크 위젯 닫힘"), *GetName());
}
