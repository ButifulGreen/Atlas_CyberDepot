// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/FactorySpectatorPawn.h"
#include "Atlas_CyberDepot.h"
#include "Player/FactoryPlayerController.h"
#include "Agent/FactoryNPCHuman.h"
#include "Infrastructure/FactoryKioskTerminal.h"
#include "Components/SphereComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"

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
		return;
	}

	EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &AFactorySpectatorPawn::OnInteractTriggered);
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: IA_Interact 바인딩 완료"), *GetName());
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
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: IA_Interact 트리거됨"), *GetName());

	AActor* Candidate = FindInteractableInFrontOfCamera();
	if (!Candidate)
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

	if (AFactoryNPCHuman* NPC = Cast<AFactoryNPCHuman>(Candidate))
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: %s 빙의 요청(Server_RequestPossessNPC)"), *GetName(), *NPC->GetName());
		FactoryPC->Server_RequestPossessNPC(NPC);
		return;
	}

	if (AFactoryKioskTerminal* Kiosk = Cast<AFactoryKioskTerminal>(Candidate))
	{
		// 9단계(Docs/09_Visualization.md) UMG 주문 UI로 교체 예정 — 8단계는 RPC 경로 확인용 로그만 남긴다.
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 키오스크(%s) 후보 감지 — 9단계 UMG 연결 전까지 로그만"), *GetName(), *Kiosk->GetName());
	}
}
