// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/FactorySpectatorPawn.h"
#include "Atlas_CyberDepot.h"
#include "Player/FactoryPlayerController.h"
#include "Agent/FactoryNPCHuman.h"
#include "Agent/FactoryAgentBase.h"
#include "Infrastructure/LogisticsItem.h"
#include "EngineUtils.h"
#include "Infrastructure/FactoryKioskTerminal.h"
#include "Visualization/VendorOrderListWidget.h"
#include "Assignment/SmartFactoryManager.h"
#include "Benchmark/ReplayRecorderSubsystem.h"
#include "Benchmark/ReplayPlaybackSubsystem.h"
#include "Benchmark/ReplayVisualizationSubsystem.h"
#include "Benchmark/ReplayControlWidget.h"
#include "Components/SphereComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"

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
	CloseReplayControlWidget();
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

	if (!ToggleReplayAction)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: ToggleReplayAction이 비어있어 바인딩하지 못함 — BP에서 IA_ToggleReplay 할당 필요"), *GetName());
	}
	else
	{
		EnhancedInput->BindAction(ToggleReplayAction, ETriggerEvent::Started, this, &AFactorySpectatorPawn::OnToggleReplayTriggered);
	}

	if (!ReplayLookHoldAction)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: ReplayLookHoldAction이 비어있어 바인딩하지 못함 — BP에서 IA_ReplayLookHold(우클릭) 할당 필요"), *GetName());
	}
	else
	{
		EnhancedInput->BindAction(ReplayLookHoldAction, ETriggerEvent::Started, this, &AFactorySpectatorPawn::OnReplayLookHoldStarted);
		EnhancedInput->BindAction(ReplayLookHoldAction, ETriggerEvent::Completed, this, &AFactorySpectatorPawn::OnReplayLookHoldReleased);
		EnhancedInput->BindAction(ReplayLookHoldAction, ETriggerEvent::Canceled, this, &AFactorySpectatorPawn::OnReplayLookHoldReleased);
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

void AFactorySpectatorPawn::OnToggleReplayTriggered(const FInputActionValue& Value)
{
	UWorld* World = GetWorld();
	UReplayPlaybackSubsystem* Playback = World ? World->GetSubsystem<UReplayPlaybackSubsystem>() : nullptr;
	UReplayVisualizationSubsystem* Visualization = World ? World->GetSubsystem<UReplayVisualizationSubsystem>() : nullptr;
	if (!Playback || !Visualization)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: 리플레이 서브시스템을 못 찾음(Playback=%s, Visualization=%s)"),
			*GetName(), Playback ? TEXT("있음") : TEXT("없음"), Visualization ? TEXT("있음") : TEXT("없음"));
		return;
	}

	// 버그 수정 — 일시정지 기능 추가 전엔 IsPlaying()==false가 항상 "재생 세션 자체가 없음"과 같았지만,
	// 이제 일시정지 중에도 false가 되므로 그 상태에서 이 토글을 누르면 새 재생을 또 시작하려 들었다.
	// LoadedFrames가 있으면(재생 중이든 일시정지든) 세션이 살아있는 것으로 보고 정지시킨다.
	if (Playback->LoadedFrames.Num() > 0)
	{
		Playback->Stop();
		Visualization->StopVisualizing();
		EndReplayCameraView();
		CloseReplayControlWidget();
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 리플레이 정지"), *GetName());
		return;
	}

	UReplayRecorderSubsystem* Recorder = World->GetSubsystem<UReplayRecorderSubsystem>();
	if (!Recorder || Recorder->CurrentRecordingFilePath.IsEmpty())
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: 재생할 기록 파일이 없음(리플레이 기록기가 아직 시작되지 않음)"), *GetName());
		return;
	}

	if (!Playback->LoadRecording(Recorder->CurrentRecordingFilePath))
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: LoadRecording 실패(%s)"), *GetName(), *Recorder->CurrentRecordingFilePath);
		return;
	}

	Playback->Play();
	Visualization->BeginVisualizing();
	BeginReplayCameraView();
	// 사용자 지시(2026-07-23) — 별도 토글 없이 재생 진입과 동시에 컨트롤 패널이 바로 보여야 한다.
	OpenReplayControlWidget();
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 리플레이 재생 시작(%s)"), *GetName(), *Recorder->CurrentRecordingFilePath);
}

void AFactorySpectatorPawn::BeginReplayCameraView()
{
	UWorld* World = GetWorld();
	UReplayPlaybackSubsystem* Playback = World ? World->GetSubsystem<UReplayPlaybackSubsystem>() : nullptr;
	if (!World || !Playback)
	{
		return;
	}

	// 버그 수정(사용자 리포트) — 처음엔 전용 ACameraActor로 시점만 전환하고 이 폰의 이동 입력을 막았는데,
	// 자동 프레이밍이 빗나가 건물 밖 등을 비추면 안을 살펴볼 방법이 없었다. 이후 스냅샷 중심/반경 기준
	// 자동 프레이밍으로 순간이동시켰으나, 그마저도 물류센터 밖 등 엉뚱한 위치로 튀는 문제가 있었다(사용자
	// 리포트, 2026-07-23) — 결국 위치를 전혀 옮기지 않고 자유비행 카메라가 있던 자리 그대로 재생을
	// 시작하는 것으로 정리했다. `PreReplayCameraLocation`/`Rotation`은 재생 도중 사용자가 직접 이동했을
	// 경우를 위해 계속 기억해뒀다가 `EndReplayCameraView`에서 복원한다.
	PreReplayCameraLocation = GetActorLocation();
	PreReplayCameraRotation = GetActorRotation();

	// 버그 수정(사용자 리포트) — "전용 화면"의 의미가 위치만 옮기는 게 아니라, 실시간으로 계속 바뀌는
	// 실물이 고스트와 겹쳐 보이지 않아야 한다는 뜻이었다.
	SetLiveWorldActorsHidden(true);
}

void AFactorySpectatorPawn::EndReplayCameraView()
{
	SetLiveWorldActorsHidden(false);
	SetActorLocationAndRotation(PreReplayCameraLocation, PreReplayCameraRotation);
}

void AFactorySpectatorPawn::SetLiveWorldActorsHidden(bool bShouldHide)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// SetActorHiddenInGame()은 리플리케이트되지 않는 순수 로컬 렌더링 상태라(AActor::
	// GetLifetimeReplicatedProps에 없음), 여기서 숨겨도 다른 플레이어의 화면에는 전혀 영향이 없다 —
	// 각자 독립적으로 리플레이를 본다는 설계 그대로 유지된다. 로봇/NPC는 bHidden이 순수 렌더링 상태라
	// 무차별로 토글해도 안전하다.
	for (TActorIterator<AFactoryAgentBase> It(World); It; ++It)
	{
		It->SetActorHiddenInGame(bShouldHide);
	}

	// 버그 수정(사용자 리포트) — ALogisticsItem은 다르다. bHidden이 ALogisticsItemSpawner::TryAcquireItem이
	// "재고 풀에서 아직 안 쓰인 아이템"을 판정하는 실제 게임 로직 기준(Item->IsHidden())이라, 로봇처럼
	// 무차별로 숨기고/보이면 풀에서 대기 중이던(원래부터 숨겨져 있던) 아이템까지 강제로 보이게 되어
	// 스포너 위치에 물품이 쌓여 보이고, 동시에 모든 아이템이 "숨겨져 있지 않음"이 되어 신규 주문이
	// 아이템을 하나도 못 받게 된다(실제 재현됨). 지금 실제로 보이던(사용 중이던) 것만 숨기고, 그 목록을
	// 기억해뒀다가 되돌릴 때 그것만 정확히 복원한다 — 풀 대기 아이템은 아예 건드리지 않는다.
	if (bShouldHide)
	{
		LogisticsItemsHiddenForReplay.Reset();
		for (TActorIterator<ALogisticsItem> It(World); It; ++It)
		{
			if (!It->IsHidden())
			{
				It->SetActorHiddenInGame(true);
				LogisticsItemsHiddenForReplay.Add(*It);
			}
		}
	}
	else
	{
		for (ALogisticsItem* Item : LogisticsItemsHiddenForReplay)
		{
			if (Item)
			{
				Item->SetActorHiddenInGame(false);
			}
		}
		LogisticsItemsHiddenForReplay.Reset();
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

void AFactorySpectatorPawn::OpenReplayControlWidget()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !ReplayControlWidgetClass)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: 리플레이 컨트롤 위젯을 열 수 없음(PC=%s, ReplayControlWidgetClass=%s)"),
			*GetName(), PC ? TEXT("있음") : TEXT("없음"), ReplayControlWidgetClass ? TEXT("있음") : TEXT("비어있음 — BP에서 ReplayControlWidgetClass 할당 필요"));
		return;
	}

	ActiveReplayControlWidget = CreateWidget<UReplayControlWidget>(PC, ReplayControlWidgetClass);
	if (!ActiveReplayControlWidget)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: CreateWidget(ReplayControlWidgetClass) 실패"), *GetName());
		return;
	}

	ActiveReplayControlWidget->AddToViewport();

	// 키오스크와 달리 이동은 막지 않는다(사용자 지시 — 재생 도중에도 자유롭게 날아다닐 수 있어야 함).
	// 시야 회전만 기본적으로 막아뒀다가, ReplayLookHoldAction(우클릭)을 누르고 있는 동안만 일시적으로
	// 허용한다(OnReplayLookHoldStarted/Released) — 그래야 커서를 스크럽바/버튼 쪽으로 움직여도 카메라가
	// 같이 돌아가지 않는다.
	PC->SetInputMode(FInputModeGameAndUI());
	PC->SetShowMouseCursor(true);
	PC->SetIgnoreLookInput(true);

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 리플레이 컨트롤 위젯 열림"), *GetName());
}

void AFactorySpectatorPawn::CloseReplayControlWidget()
{
	if (!ActiveReplayControlWidget)
	{
		return;
	}

	ActiveReplayControlWidget->RemoveFromParent();
	ActiveReplayControlWidget = nullptr;

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);
	}

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 리플레이 컨트롤 위젯 닫힘"), *GetName());
}

void AFactorySpectatorPawn::OnReplayLookHoldStarted(const FInputActionValue& Value)
{
	// 패널이 닫혀있으면 아무 효과 없음 — 평소 스펙테이팅에는 이 액션이 전혀 관여하지 않는다.
	if (!ActiveReplayControlWidget)
	{
		return;
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// GameOnly로 잠깐 전환해 마우스를 뷰포트에 캡처해야 델타 기반 시야 회전이 제대로 들어온다
		// (GameAndUI는 커서가 화면 가장자리에 막혀 회전 폭이 제한될 수 있음) — 에디터 뷰포트의 우클릭
		// 내비게이션과 동일한 방식.
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
		PC->SetIgnoreLookInput(false);
	}
}

void AFactorySpectatorPawn::OnReplayLookHoldReleased(const FInputActionValue& Value)
{
	if (!ActiveReplayControlWidget)
	{
		return;
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetInputMode(FInputModeGameAndUI());
		PC->SetShowMouseCursor(true);
		PC->SetIgnoreLookInput(true);
	}
}
