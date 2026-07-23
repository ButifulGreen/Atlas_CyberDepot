// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryNPCHuman.h"
#include "Atlas_CyberDepot.h"
#include "Agent/FactoryAIController.h"
#include "Player/FactoryPlayerController.h"
#include "Repair/RepairProgressComponent.h"
#include "Infrastructure/FactoryKioskTerminal.h"
#include "Visualization/VendorOrderListWidget.h"
#include "Assignment/SmartFactoryManager.h"
#include "NavigationSystem.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Blueprint/UserWidget.h"
#include "Net/UnrealNetwork.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"

AFactoryNPCHuman::AFactoryNPCHuman()
{
	AgentType = EActorType::NPCHuman;
}

void AFactoryNPCHuman::StartPatrol()
{
	PatrolState = EPatrolState::Patrolling;
	PatrolStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	SetState(EAgentState::Patrolling);

	AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController());
	UNavigationSystemV1* NavSys = GetWorld() ? UNavigationSystemV1::GetCurrent(GetWorld()) : nullptr;
	if (!AIController || !NavSys)
	{
		return;
	}

	FNavLocation RandomLocation;
	if (NavSys->GetRandomReachablePointInRadius(GetActorLocation(), PatrolRadius, RandomLocation))
	{
		AIController->RequestMoveWithFilter(RandomLocation.Location);
	}
}

void AFactoryNPCHuman::AssignMaintenance(AFactoryAgentBase* Target, ERepairType RepairType)
{
	if (!Target)
	{
		return;
	}

	// 버그 수정(사용자 지시) — 사무실에서 랜덤 대기 중(OfficeWaitTimerHandle)에 새 정비가 배정되면,
	// 대기 종료 후 StartPatrol()이 뒤늦게 불려 지금 진행 중인 정비를 무시하고 순찰로 튀어버릴 수 있다.
	GetWorldTimerManager().ClearTimer(OfficeWaitTimerHandle);

	AssignedMaintenanceTarget = Target;
	SetState(EAgentState::UnderRepair);

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Repair] %s: %s를 향해 이동 시작(RepairType=%s)"), *DisplayName, *Target->DisplayName,
		RepairType == ERepairType::FullRepair ? TEXT("FullRepair") : TEXT("QuickCheck"));

	if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
	{
		AIController->SetAvoidanceIgnoreActor(Target, true);
		AIController->RequestMoveWithFilter(Target->GetActorLocation());
	}

	// 정확한 도착 판정(이동 완료 콜백)은 아직 없어, 배정 시점에 바로 참여시키는 것으로 단순화했다.
	if (URepairProgressComponent* RepairComponent = Target->GetRepairComponent())
	{
		RepairComponent->CurrentRepairType = RepairType;
		RepairComponent->Server_JoinRepair(this);
	}
}

void AFactoryNPCHuman::ReturnToOfficeRoom()
{
	PatrolState = EPatrolState::ReturningToOffice;

	if (AssignedMaintenanceTarget)
	{
		if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
		{
			AIController->SetAvoidanceIgnoreActor(AssignedMaintenanceTarget, false);
		}

		if (URepairProgressComponent* RepairComponent = AssignedMaintenanceTarget->GetRepairComponent())
		{
			RepairComponent->Server_LeaveRepair(this);
		}

		AssignedMaintenanceTarget = nullptr;
	}

	if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
	{
		AIController->RequestMoveWithFilter(OfficeRoomTransform.GetLocation());
	}

	SetState(EAgentState::Moving);
}

void AFactoryNPCHuman::OnArrivedAtDestination()
{
	// 버그 수정(사용자 지시) — 기본 구현(AFactoryAgentBase::OnArrivedAtDestination, 빈 함수)이라 사무실
	// 도착이 감지되지 않아 CurrentState/PatrolState가 Moving/ReturningToOffice에 영구히 눌러붙었다.
	// 순찰 중 도착(PatrolState==Patrolling)이나 정비 대상 도착(AssignMaintenance가 이동 시작 시점에
	// 이미 UnderRepair로 전환해둠)은 여기서 다룰 대상이 아니다 — 사무실 복귀 도착만 처리한다.
	if (PatrolState != EPatrolState::ReturningToOffice)
	{
		return;
	}

	PatrolState = EPatrolState::InOffice;
	SetState(EAgentState::Idle);

	const int32 WaitSeconds = FMath::RandRange(0, MaxOfficeWaitSeconds);
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Patrol] %s 사무실 도착 — %d초 대기 후 순찰 재개"), *DisplayName, WaitSeconds);

	if (WaitSeconds <= 0)
	{
		StartPatrol();
		return;
	}

	GetWorldTimerManager().SetTimer(OfficeWaitTimerHandle, this, &AFactoryNPCHuman::StartPatrol, static_cast<float>(WaitSeconds), false);
}

bool AFactoryNPCHuman::TryPossessByPlayer(APlayerController* RequestingController)
{
	if (!RequestingController || RequestingController->GetPawn() == this)
	{
		return false;
	}

	RequestingController->Possess(this);
	return true;
}

void AFactoryNPCHuman::CallToOfficeExit()
{
	PatrolState = EPatrolState::InOffice;
	StartPatrol();
}

bool AFactoryNPCHuman::CanBePossessedBy(APlayerController* RequestingController) const
{
	const APlayerController* CurrentPlayerController = Cast<APlayerController>(GetController());
	return !CurrentPlayerController || CurrentPlayerController == RequestingController;
}

void AFactoryNPCHuman::ReleasePossession()
{
	// AutoPossessAI/AIControllerClass 설정에 따라 AI 제어를 되찾는다(레벨 배치 시 설정 필요 — 8단계 범위 밖).
	// SpawnDefaultController()가 내부적으로 새 AI 컨트롤러의 Possess()를 호출하는 과정에서, 지금 이 폰을
	// 쥐고 있던 기존(플레이어) 컨트롤러의 UnPossess()가 먼저 자동으로 불린다 — 아래 UnPossessed()가
	// 그 경로로 자연스럽게 실행되어 정비 참여 정리를 별도 호출 없이 처리한다.
	SpawnDefaultController();

	// 버그 수정(사용자 리포트) — StartPatrol()은 레벨 시작 시 BP BeginPlay에서 1회만 호출되는 구조라,
	// 빙의→해제를 거치면 순찰을 다시 시작시킬 트리거가 전혀 없어 CurrentState==Idle에 영구히 멈춰있었다.
	// CallToOfficeExit()로 PatrolState를 InOffice로 리셋하고 강제로 순찰을 재개시킨다.
	CallToOfficeExit();
}

void AFactoryNPCHuman::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// 버그 수정(로깅 오탐) — AI 컨트롤러 빙의(레벨 시작 시 정상 동작)에서도 매번 경고가 찍혔다.
	// 플레이어가 아닌 빙의는 Enhanced Input 설정 대상이 아니므로 조용히 지나간다.
	APlayerController* PC = Cast<APlayerController>(NewController);
	if (!PC)
	{
		BoundInputPlayerController = nullptr;
		return;
	}

	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Subsystem)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: EnhancedInputLocalPlayerSubsystem을 못 찾음"), *DisplayName);
	}
	else
	{
		if (DefaultMappingContext)
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
		else
		{
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: DefaultMappingContext가 비어있음 — BP_FactoryNPCHuman에서 IMC_Default 할당 필요"), *DisplayName);
		}

		if (MouseLookMappingContext)
		{
			Subsystem->AddMappingContext(MouseLookMappingContext, 0);
		}
		else
		{
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: MouseLookMappingContext가 비어있음 — BP_FactoryNPCHuman에서 IMC_MouseLook 할당 필요"), *DisplayName);
		}

		UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 빙의 중 매핑 컨텍스트 추가 완료"), *DisplayName);
	}

	BoundInputPlayerController = PC;

	// 로컬로 조작 중인 클라이언트 화면에만 정비 참여 상태를 계속 표시(다른 클라이언트 화면엔 안 뜸).
	if (PC->IsLocalController())
	{
		GetWorldTimerManager().SetTimer(RepairStatusDebugTimerHandle, this, &AFactoryNPCHuman::DrawRepairStatusDebugMessage, 0.5f, true);
	}
}

void AFactoryNPCHuman::UnPossessed()
{
	GetWorldTimerManager().ClearTimer(RepairStatusDebugTimerHandle);

	// 버그 수정 — 키오스크 위젯이 열린 채로 빙의 해제되면 입력모드/마우스 커서/이동잠금이 눌러붙는다.
	CloseKioskWidget();

	// 버그 수정(8단계) — 빙의 해제(명시적 이탈/접속 끊김 등 전부 포함)를 정비 참여 종료 시점으로도 겸한다.
	if (HasAuthority())
	{
		LeaveRepairAsPlayer();
	}

	if (APlayerController* PC = BoundInputPlayerController.Get())
	{
		if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (DefaultMappingContext)
				{
					Subsystem->RemoveMappingContext(DefaultMappingContext);
				}
				if (MouseLookMappingContext)
				{
					Subsystem->RemoveMappingContext(MouseLookMappingContext);
				}
			}
		}
	}
	BoundInputPlayerController = nullptr;

	Super::UnPossessed();
}

void AFactoryNPCHuman::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 버그 수정(로깅 규율) — 프로퍼티 미할당을 조용히 넘기면 BP 할당 누락과 다른 원인을 구분할 방법이 없다.
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: PlayerInputComponent가 UEnhancedInputComponent가 아님"), *DisplayName);
		return;
	}

	if (MoveAction)
	{
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AFactoryNPCHuman::OnMoveTriggered);
	}
	else
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: MoveAction이 비어있어 바인딩하지 못함 — BP_FactoryNPCHuman에서 할당 필요"), *DisplayName);
	}

	if (LookAction)
	{
		EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFactoryNPCHuman::OnLookTriggered);
	}
	else
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: LookAction이 비어있어 바인딩하지 못함 — BP_FactoryNPCHuman에서 할당 필요"), *DisplayName);
	}

	if (MouseLookAction)
	{
		EnhancedInput->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AFactoryNPCHuman::OnLookTriggered);
	}
	else
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: MouseLookAction이 비어있어 바인딩하지 못함 — BP_FactoryNPCHuman에서 할당 필요"), *DisplayName);
	}

	if (JumpAction)
	{
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	}
	else
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: JumpAction이 비어있어 바인딩하지 못함 — BP_FactoryNPCHuman에서 할당 필요"), *DisplayName);
	}

	if (InteractAction)
	{
		EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &AFactoryNPCHuman::OnInteractTriggered);
	}
	else
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: InteractAction이 비어있어 바인딩하지 못함 — BP_FactoryNPCHuman에서 할당 필요"), *DisplayName);
	}

	if (ClickAction)
	{
		EnhancedInput->BindAction(ClickAction, ETriggerEvent::Started, this, &AFactoryNPCHuman::OnClickTriggered);
	}
	else
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: ClickAction이 비어있어 바인딩하지 못함 — BP_FactoryNPCHuman에서 할당 필요"), *DisplayName);
	}

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 빙의 입력 바인딩 완료"), *DisplayName);
}

void AFactoryNPCHuman::OnMoveTriggered(const FInputActionValue& Value)
{
	// 사용자 지시 — 키오스크 위젯이 열린 동안은 이동 입력을 완전히 무시한다(마우스만 움직일 수 있어야 함).
	if (ActiveKioskWidget)
	{
		return;
	}

	const FVector2D MoveInput = Value.Get<FVector2D>();
	if (!Controller)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: OnMoveTriggered — Controller가 없음"), *DisplayName);
		return;
	}

	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), MoveInput.Y);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), MoveInput.X);
}

void AFactoryNPCHuman::OnLookTriggered(const FInputActionValue& Value)
{
	// 사용자 지시 — 키오스크 위젯이 열린 동안은 시점 회전도 멈춘다(마우스는 커서로만 동작).
	if (ActiveKioskWidget)
	{
		return;
	}

	const FVector2D LookInput = Value.Get<FVector2D>();
	AddControllerYawInput(LookInput.X);
	AddControllerPitchInput(LookInput.Y);
}

AFactoryAgentBase* AFactoryNPCHuman::FindRepairableInFrontOfCamera() const
{
	// 버그 수정(사용자 리포트) — GetActorEyesViewPoint()는 액터 자신의 회전(몸통이 향한 방향)을 쓰는데,
	// 이 캐릭터는 3인칭 카메라가 몸통 방향과 독립적으로 움직인다 — 화면상 조준과 무관하게 몸통이 향한
	// 방향(주로 지형 쪽)으로 나가 실기 테스트에서 매번 Landscape에 맞는 문제가 있었다. 실제 카메라 시점
	// (PlayerCameraManager 반영)의 Yaw만 쓴다 — Pitch(위아래 시선)까지 반영하면 아래를 볼 때 박스가
	// 지면 쪽으로 기울어 저상 대상(배송로봇)을 오히려 놓치기 쉬워, 캐릭터 위치 기준 수평으로 스윕한다.
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC)
	{
		return nullptr;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	// 버그 수정(사용자 지시) — 라인트레이스는 배송로봇처럼 NPC 무릎보다 낮은 대상을 시선 각도에 따라
	// 놓칠 수 있어 박스 스윕으로 전환. 발밑부터 머리 위까지 넉넉히 덮도록 캐릭터 위치를 기준으로 삼는다.
	const FRotator LevelRotation(0.f, ViewRotation.Yaw, 0.f);
	const FVector Direction = LevelRotation.Vector();
	const FVector Start = GetActorLocation();
	const FVector BoxCenter = Start + Direction * (RepairInteractTraceDistance * 0.5f);
	const FQuat BoxRotation = LevelRotation.Quaternion();
	const FCollisionShape Box = FCollisionShape::MakeBox(FVector(RepairInteractTraceDistance * 0.5f, RepairInteractBoxHalfWidth, RepairInteractBoxHalfHeight));

	// 사용자 지시 — F를 누를 때마다 실제로 검사한 범위를 눈으로 볼 수 있게 항상 그린다(결과와 무관).
	DrawDebugBox(GetWorld(), BoxCenter, Box.GetExtent(), BoxRotation, FColor::Cyan, false, 1.f, 0, 3.f);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByChannel(Overlaps, BoxCenter, BoxRotation, ECC_Pawn, Box, QueryParams);

	AFactoryAgentBase* BestAgent = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AFactoryAgentBase* HitAgent = Cast<AFactoryAgentBase>(Overlap.GetActor());
		if (!HitAgent || HitAgent->CurrentState != EAgentState::Broken || !HitAgent->GetRepairComponent())
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(Start, HitAgent->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestAgent = HitAgent;
		}
	}

	if (!BestAgent)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 감지 범위 내 Broken 정비 대상 없음(%d개 겹침)"), *DisplayName, Overlaps.Num());
	}

	return BestAgent;
}

AFactoryKioskTerminal* AFactoryNPCHuman::FindKioskInFrontOfCamera() const
{
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC)
	{
		return nullptr;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * KioskInteractTraceDistance;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, QueryParams))
	{
		return nullptr;
	}

	return Cast<AFactoryKioskTerminal>(Hit.GetActor());
}

void AFactoryNPCHuman::OnInteractTriggered(const FInputActionValue& Value)
{
	// 사용자 지시 — F는 빙의 해제 전용(정비/키오스크는 좌클릭, OnClickTriggered 참고). F/정비/키오스크가
	// 전부 같은 키를 썼을 때 "다시 눌러도 빙의가 안 풀리는" 문제가 있었다 — 셋을 분리해 해소.
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: IA_Interact 트리거됨(빙의 해제 요청)"), *DisplayName);

	AFactoryPlayerController* PC = Cast<AFactoryPlayerController>(GetController());
	if (!PC)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: AFactoryPlayerController 캐스트 실패(GetController()=%s)"),
			*DisplayName, GetController() ? *GetController()->GetClass()->GetName() : TEXT("None"));
		return;
	}

	PC->Server_ReleaseNPC();
}

void AFactoryNPCHuman::OnClickTriggered(const FInputActionValue& Value)
{
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: IA_Click 트리거됨(빙의 중)"), *DisplayName);

	AFactoryPlayerController* PC = Cast<AFactoryPlayerController>(GetController());
	if (!PC)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: AFactoryPlayerController 캐스트 실패(GetController()=%s)"),
			*DisplayName, GetController() ? *GetController()->GetClass()->GetName() : TEXT("None"));
		return;
	}

	// 위젯이 열려있으면 이 클릭은 무조건 닫기 전용 — 열린 동안은 이동/시점이 멈춰 있어 새로 다른
	// 상호작용을 시작할 상황 자체가 아니다(사용자 지시).
	if (ActiveKioskWidget)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 키오스크 위젯 닫기(토글)"), *DisplayName);
		CloseKioskWidget();
		return;
	}

	if (JoinedRepairComponent)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 이미 참여 중인 정비 이탈 요청"), *DisplayName);
		PC->Server_LeaveRepair(JoinedRepairComponent);
		return;
	}

	if (AFactoryAgentBase* Target = FindRepairableInFrontOfCamera())
	{
		if (URepairProgressComponent* TargetRepair = Target->GetRepairComponent())
		{
			// 사용자 지시 — 감지+참여 시작 순간만큼은 다른 에이전트 로그에 묻히지 않도록 Warning 등급 +
			// 화면 플래시 메시지로 별도 표시(위의 지속 상태 표시와는 다른 키라 겹쳐 쌓인다).
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] ▶▶▶ %s: %s 정비 참여 시작 ◀◀◀"), *DisplayName, *Target->DisplayName);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.f, FColor::Green,
					FString::Printf(TEXT("정비 대상 감지 — %s 참여 시작!"), *Target->DisplayName));
			}
			PC->Server_JoinRepair(TargetRepair);
			return;
		}
	}

	if (AFactoryKioskTerminal* Kiosk = FindKioskInFrontOfCamera())
	{
		OpenKioskWidget(Kiosk);
	}
}

void AFactoryNPCHuman::OpenKioskWidget(AFactoryKioskTerminal* Kiosk)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !KioskWidgetClass)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: 키오스크 위젯을 열 수 없음(PC=%s, KioskWidgetClass=%s)"),
			*DisplayName, PC ? TEXT("있음") : TEXT("없음"), KioskWidgetClass ? TEXT("있음") : TEXT("비어있음 — BP_FactoryNPCHuman에서 KioskWidgetClass 할당 필요"));
		return;
	}

	ActiveKioskWidget = CreateWidget<UVendorOrderListWidget>(PC, KioskWidgetClass);
	if (!ActiveKioskWidget)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: CreateWidget(KioskWidgetClass) 실패"), *DisplayName);
		return;
	}

	if (AMSmartFactoryManager* Manager = GetWorld()->GetGameState<AMSmartFactoryManager>())
	{
		ActiveKioskWidget->BindToManager(Manager);
	}
	else
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Interact] %s: AMSmartFactoryManager(GameState)를 못 찾음 — 위젯이 주문 목록을 못 받음"), *DisplayName);
	}
	ActiveKioskWidget->BindToKiosk(Kiosk);
	ActiveKioskWidget->AddToViewport();

	// 사용자 지시 — 위젯이 열린 동안은 캐릭터 이동/시야 회전 없이 마우스 커서만 움직일 수 있어야 한다.
	PC->SetInputMode(FInputModeGameAndUI());
	PC->SetShowMouseCursor(true);
	PC->SetIgnoreMoveInput(true);
	PC->SetIgnoreLookInput(true);

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 키오스크(%s) 위젯 열림"), *DisplayName, *Kiosk->GetName());
}

void AFactoryNPCHuman::CloseKioskWidget()
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

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Interact] %s: 키오스크 위젯 닫힘"), *DisplayName);
}

void AFactoryNPCHuman::JoinRepairAsPlayer(URepairProgressComponent* RepairComponent)
{
	if (!RepairComponent || JoinedRepairComponent == RepairComponent)
	{
		return;
	}

	// 이미 다른 정비에 참여 중이었으면 먼저 이탈(한 번에 하나만 돕는다).
	LeaveRepairAsPlayer();

	// Broken 상태만 대상으로 삼으므로(FindRepairableInFrontOfCamera) 항상 FullRepair — AI의
	// AssignMaintenance와 동일하게 매 참여 시점에 반영한다(이미 같은 값이면 덮어써도 무해).
	RepairComponent->CurrentRepairType = ERepairType::FullRepair;
	RepairComponent->Server_JoinRepair(this);
	JoinedRepairComponent = RepairComponent;
	SetState(EAgentState::UnderRepair);
}

void AFactoryNPCHuman::LeaveRepairAsPlayer()
{
	if (URepairProgressComponent* Joined = JoinedRepairComponent)
	{
		Joined->Server_LeaveRepair(this);
	}
	JoinedRepairComponent = nullptr;

	if (CurrentState == EAgentState::UnderRepair)
	{
		SetState(EAgentState::Idle);
	}
}

void AFactoryNPCHuman::OnJoinedRepairCompleted()
{
	JoinedRepairComponent = nullptr;
	if (CurrentState == EAgentState::UnderRepair)
	{
		SetState(EAgentState::Idle);
	}
}

void AFactoryNPCHuman::DrawRepairStatusDebugMessage()
{
	if (!GEngine)
	{
		return;
	}

	// GetUniqueID() — 이 NPC 전용 키로 고정해 매번 새 줄로 쌓이지 않고 같은 자리를 계속 덮어쓰게 한다.
	const int32 Key = GetUniqueID();

	if (URepairProgressComponent* Repair = JoinedRepairComponent)
	{
		const bool bIsFullRepair = Repair->CurrentRepairType == ERepairType::FullRepair;
		const float TargetDuration = bIsFullRepair ? Repair->FullRepairDurationSeconds : Repair->QuickCheckDurationSeconds;
		const float Percent = TargetDuration > 0.f ? FMath::Clamp(Repair->RepairProgress / TargetDuration * 100.f, 0.f, 100.f) : 0.f;
		const AFactoryAgentBase* TargetOwner = Cast<AFactoryAgentBase>(Repair->GetOwner());

		GEngine->AddOnScreenDebugMessage(Key, 0.6f, FColor::Green,
			FString::Printf(TEXT("[정비 참여 중] 대상: %s / %s / 진행률 %.0f%% (%d명 참여)"),
				TargetOwner ? *TargetOwner->DisplayName : TEXT("?"),
				bIsFullRepair ? TEXT("FullRepair") : TEXT("QuickCheck"),
				Percent, Repair->GetValidRepairerCount()));
	}
	else
	{
		GEngine->AddOnScreenDebugMessage(Key, 0.6f, FColor::White, TEXT("[정비 미참여] 좌클릭으로 고장난 로봇에 다가가 참여 가능"));
	}
}

void AFactoryNPCHuman::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFactoryNPCHuman, JoinedRepairComponent);
}
