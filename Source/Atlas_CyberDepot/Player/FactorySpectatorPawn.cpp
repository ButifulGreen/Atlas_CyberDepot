// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/FactorySpectatorPawn.h"
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

void AFactorySpectatorPawn::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PC = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;

	if (Subsystem && DefaultMappingContext)
	{
		Subsystem->AddMappingContext(DefaultMappingContext, 0);
	}
}

void AFactorySpectatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (InteractAction)
		{
			EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &AFactorySpectatorPawn::OnInteractTriggered);
		}
	}
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
		return nullptr;
	}

	AActor* HitActor = Hit.GetActor();
	if (Cast<AFactoryNPCHuman>(HitActor) || Cast<AFactoryKioskTerminal>(HitActor))
	{
		return HitActor;
	}

	return nullptr;
}

void AFactorySpectatorPawn::OnInteractTriggered(const FInputActionValue& Value)
{
	AActor* Candidate = FindInteractableInFrontOfCamera();
	if (!Candidate)
	{
		return;
	}

	AFactoryPlayerController* FactoryPC = Cast<AFactoryPlayerController>(GetController());
	if (!FactoryPC)
	{
		return;
	}

	if (AFactoryNPCHuman* NPC = Cast<AFactoryNPCHuman>(Candidate))
	{
		FactoryPC->Server_RequestPossessNPC(NPC);
		return;
	}

	if (AFactoryKioskTerminal* Kiosk = Cast<AFactoryKioskTerminal>(Candidate))
	{
		// 9단계(Docs/09_Visualization.md) UMG 주문 UI로 교체 예정 — 8단계는 RPC 경로 확인용 로그만 남긴다.
		UE_LOG(LogTemp, Log, TEXT("Kiosk interact candidate: %s"), *Kiosk->GetName());
	}
}
