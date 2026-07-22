// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Agent/FactoryAgentBase.h"
#include "Agent/RepairTypes.h"
#include "FactoryNPCHuman.generated.h"

class APlayerController;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;
class AFactoryKioskTerminal;
class UVendorOrderListWidget;

// Docs/04_Agent_AI.md에 값이 명시돼 있지 않아 관련 함수(StartPatrol/ReturnToOfficeRoom/CallToOfficeExit)에서
// 유추 가능한 최소 상태만 정의
UENUM(BlueprintType)
enum class EPatrolState : uint8
{
	InOffice,
	Patrolling,
	ReturningToOffice
};

// Docs/04_Agent_AI.md §4 — 5단계 대상.
UCLASS()
class AFactoryNPCHuman : public AFactoryAgentBase
{
	GENERATED_BODY()

public:
	AFactoryNPCHuman();

	UPROPERTY(BlueprintReadOnly)
	EPatrolState PatrolState = EPatrolState::InOffice;

	UPROPERTY(BlueprintReadOnly)
	float PatrolStartTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Patrol")
	float MaxPatrolDurationSeconds = 30.f;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<AFactoryAgentBase> AssignedMaintenanceTarget;

	// Docs에 없는 구현값: 순찰 반경, 사무실 복귀 지점 (레벨마다 다르므로 EditAnywhere로 노출)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patrol")
	float PatrolRadius = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patrol")
	FTransform OfficeRoomTransform;

	// 버그 수정(사용자 지시) — AFactoryAgentBase::OnArrivedAtDestination()이 기본 빈 함수라, 사무실로
	// 복귀한 NPC가 실제로 도착해도 아무도 감지하지 못해 CurrentState/PatrolState가 Moving/
	// ReturningToOffice에 영구히 눌러붙었다(자동으로 다시 순찰을 재개시킬 방법이 없어, 정비를 한 번이라도
	// 마친 NPC는 사무실에 영구 정지 — 결국 전원이 같은 사무실 지점에 뭉치는 현상으로 재현). 도착 시
	// 0부터 이 값까지의 랜덤 정수(초)만큼 대기했다가 StartPatrol()을 다시 호출한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Patrol")
	int32 MaxOfficeWaitSeconds = 10;

	// Docs에 없는 구현값 — 순찰 단독(애니메이션/내비메시) 테스트용으로 BlueprintCallable 노출.
	UFUNCTION(BlueprintCallable)
	void StartPatrol();
	void AssignMaintenance(AFactoryAgentBase* Target, ERepairType RepairType);
	void ReturnToOfficeRoom();
	bool TryPossessByPlayer(APlayerController* RequestingController);
	void CallToOfficeExit();

	// 버그 수정(사용자 지시) — 사무실 도착을 실제로 감지해 대기 타이머를 거는 용도(위 MaxOfficeWaitSeconds 참고).
	virtual void OnArrivedAtDestination() override;

	// 8단계(Docs/02_Multiplayer_RPC.md) — 다른 플레이어가 이미 빙의 중인지만 확인한다.
	// AI 상태(정비/순찰 등)는 가로채기 허용 정책에 따라 체크하지 않는다.
	bool CanBePossessedBy(APlayerController* RequestingController) const;

	// 8단계 — 관전자 복귀 처리. AFactoryPlayerController::Server_ReleaseNPC가 자신의 관전자 폰을
	// 다시 Possess하기 직전에 호출해, 이 NPC는 AI 제어로 복귀시킨다.
	void ReleasePossession();

	// 8단계 신규 — 빙의 중 이동/시점. 서드퍼슨 템플릿 추가 시 생성된 IMC_Default(Move/Look/Jump)·
	// IMC_MouseLook(MouseLook)을 그대로 재사용한다(Enhanced Input 에셋은 코드로 생성 불가, 에디터에서 할당).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> MouseLookMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MouseLookAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	// F 전용 — 빙의 해제만 담당(AFactorySpectatorPawn::InteractAction과 같은 IA_Interact 에셋 공유, 사용자 지시로
	// 정비/키오스크와 분리).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	// 좌클릭 전용 — 정비 참여/이탈 + 키오스크 상호작용(AFactorySpectatorPawn::ClickAction과 같은 IA_Click 에셋 공유).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ClickAction;

	// AFactorySpectatorPawn::KioskWidgetClass와 같은 WBP를 가리키도록 할당(빙의 여부와 무관하게 동일 동작, 사용자 지시).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TSubclassOf<UVendorOrderListWidget> KioskWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Interact")
	float RepairInteractTraceDistance = 300.f;

	// 버그 수정(사용자 지시) — 라인트레이스는 배송로봇(NPC 무릎보다 낮음)을 시선 각도에 따라 놓칠 수 있어
	// 박스 스윕으로 전환. 가로 폭/세로 높이 절반값.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Interact")
	float RepairInteractBoxHalfWidth = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Interact")
	float RepairInteractBoxHalfHeight = 150.f;

	// 키오스크 감지 사거리(라인트레이스) — RepairInteractTraceDistance와 별개로 에디터에서 조정 가능.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Interact")
	float KioskInteractTraceDistance = 300.f;

	// 8단계 신규 — 빙의 중 상호작용으로 정비 참여/이탈(AssignMaintenance의 플레이어 대응). 이미 다른 정비에
	// 참여 중이면 먼저 이탈한 뒤 참여한다(동시에 하나만 돕는다).
	void JoinRepairAsPlayer(URepairProgressComponent* RepairComponent);
	void LeaveRepairAsPlayer();
	// URepairProgressComponent::OnRepairCompleted가 빙의 중이던 참여자에게 호출 — 로컬 참여 상태 정리.
	void OnJoinedRepairCompleted();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

private:
	// MaxOfficeWaitSeconds만큼(0부터 랜덤) 대기한 뒤 StartPatrol()을 호출하는 타이머.
	FTimerHandle OfficeWaitTimerHandle;

	void OnMoveTriggered(const FInputActionValue& Value);
	void OnLookTriggered(const FInputActionValue& Value);
	// F 전용 — 빙의 해제(Server_ReleaseNPC) 요청만 담당.
	void OnInteractTriggered(const FInputActionValue& Value);
	// 좌클릭 전용 — 정비 참여/이탈 + 키오스크 상호작용(우선순위: 위젯이 열려있으면 닫기 > 정비 이탈 >
	// 정비 참여 > 키오스크 열기).
	void OnClickTriggered(const FInputActionValue& Value);
	// 시야 정면 트레이스로 Broken 상태 + RepairComponent 보유 대상만 후보로 삼는다(AFactorySpectatorPawn::
	// FindInteractableInFrontOfCamera와 동일 패턴).
	AFactoryAgentBase* FindRepairableInFrontOfCamera() const;
	// AFactorySpectatorPawn::FindInteractableInFrontOfCamera의 키오스크 감지 부분과 동일한 라인트레이스 패턴.
	AFactoryKioskTerminal* FindKioskInFrontOfCamera() const;

	void OpenKioskWidget(AFactoryKioskTerminal* Kiosk);
	void CloseKioskWidget();

	UPROPERTY()
	TObjectPtr<UVendorOrderListWidget> ActiveKioskWidget;

	// 버그 수정(사용자 지시) — 로봇 여러 대가 동시에 작업 중이면 각자의 디버그 로그가 뒤섞여 "내가 지금
	// 정비 중인지"를 로그만으로 판단하기 어렵다. 로컬 컨트롤 중인 동안만(다른 클라이언트 화면엔 안 뜸)
	// 0.5초마다 화면에 참여 상태를 계속 갱신해 보여준다 — 로그 스팸과 무관하게 항상 한눈에 확인 가능.
	void DrawRepairStatusDebugMessage();
	FTimerHandle RepairStatusDebugTimerHandle;

	// PossessedBy에서 저장, UnPossessed에서 그 컨트롤러 기준으로 매핑 컨텍스트를 제거하기 위함
	// (UnPossessed 시점엔 GetController()가 이미 바뀌었을 수 있어 직접 참조하지 않는다).
	UPROPERTY()
	TWeakObjectPtr<APlayerController> BoundInputPlayerController;

	// 버그 수정 — 리플리케이트해야 다른 클라이언트(원격 플레이어 소유 NPC를 보는 쪽이 아니라, 정확히는
	// 이 NPC를 빙의한 그 클라이언트 자신)도 서버 권위 값을 받아 상호작용 토글(참여↔이탈) 방향을 올바르게
	// 판단한다 — 서버에서만 채워지는 값이면 리슨서버 호스트 외 클라이언트에서는 항상 "미참여"로 보여
	// 이탈 요청이 아예 안 나간다.
	UPROPERTY(VisibleAnywhere, Replicated)
	TObjectPtr<URepairProgressComponent> JoinedRepairComponent;
};
