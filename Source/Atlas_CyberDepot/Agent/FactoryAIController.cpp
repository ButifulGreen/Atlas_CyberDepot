// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryAIController.h"
#include "Atlas_CyberDepot.h"
#include "Agent/FactoryAgentBase.h"
#include "AITypes.h"
#include "GameFramework/Pawn.h"
#include "Navigation/CrowdFollowingComponent.h"

namespace
{
	UCrowdFollowingComponent* FindCrowdFollowingComponent(const APawn* Pawn)
	{
		const AAIController* PawnController = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
		return PawnController ? Cast<UCrowdFollowingComponent>(PawnController->GetPathFollowingComponent()) : nullptr;
	}

	// 사용자 지시 — 로그/아웃라이너 이름 통일. 이 컨트롤러는 항상 AFactoryAgentBase 계열만 조종하므로
	// DisplayName을 그대로 쓴다.
	FString GetPawnDisplayName(const APawn* Pawn)
	{
		const AFactoryAgentBase* Agent = Cast<AFactoryAgentBase>(Pawn);
		return Agent ? Agent->DisplayName : TEXT("Unknown");
	}
}

void AFactoryAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);

	// 버그 수정(사용자 리포트) — MoveTo()가 동기적으로 성공을 반환해도 실제로는 전혀 움직이지 않는 사례가
	// 있어, 비동기 완료 콜백이 애초에 오기는 하는지부터 조건 없이 로그로 남긴다.
	UE_LOG(LogFactoryDispatch, Log, TEXT("[%s] OnMoveCompleted 호출됨 — Code=%d"),
		*GetPawnDisplayName(GetPawn()), static_cast<int32>(Result.Code.GetValue()));

	if (Result.IsSuccess())
	{
		MoveFailureRetryCount = 0;
		if (AFactoryAgentBase* Agent = Cast<AFactoryAgentBase>(GetPawn()))
		{
			Agent->OnArrivedAtDestination();
		}
		return;
	}

	// 버그 수정 — 이동 실패는 예전엔 로그만 남기고 아무 조치가 없어 에이전트가 Moving에 영구히 멈췄다.
	// Aborted는 보통 더 최신 RequestMoveWithFilter 호출(예: 정비 재배정)이 이 요청을 대체해서 생기는
	// 정상적인 상황이다 — 그 최신 요청이 알아서 자기 결과를 다시 보고하므로 여기서 재시도하면
	// 서로 충돌만 한다. 재시도는 진짜 길찾기 실패(Blocked/OffPath 등)에만 적용한다.
	if (Result.Code == EPathFollowingResult::Aborted)
	{
		return;
	}

	if (MoveFailureRetryCount >= MaxMoveRetryAttempts)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] 이동 실패(Code=%d) — 재시도 %d회 모두 실패"),
			*GetPawnDisplayName(GetPawn()), static_cast<int32>(Result.Code.GetValue()), MaxMoveRetryAttempts);

		// 버그 수정 — 재큐잉된 배정을 같은 에이전트가 바로 다시 집어 같은 목적지로 재시도할 때, 여기서
		// 카운트를 리셋하지 않으면 RequestMoveWithFilter가 "이전과 같은 목적지"로 보고 카운트를 그대로
		// 최대치에 둔 채 시작한다. 그 상태로 MoveTo가 동기적으로 즉시 실패를 보고하면 재시도 유예 없이
		// 곧장 다시 OnMoveFailedPermanently를 호출해, 같은 콜스택 안에서 재큐잉→재배정→즉시재실패가
		// 반복되며 스택 오버플로우로 이어졌다(실제 재현됨). 다음 시도는 항상 정상적으로 재시도 유예
		// (MaxMoveRetryAttempts회, MoveRetryDelaySeconds 간격)를 다시 거치도록 리셋한다.
		MoveFailureRetryCount = 0;

		// 버그 수정 — 예전엔 여기서 그냥 포기해 CurrentState가 Moving에 영구히 멈췄다(배정/트립을 붙든 채
		// 함대에서 영구 이탈). 에이전트가 자기 배정을 정리하고 Idle로 복귀하도록 위임한다.
		if (AFactoryAgentBase* Agent = Cast<AFactoryAgentBase>(GetPawn()))
		{
			Agent->OnMoveFailedPermanently();
		}
		return;
	}

	++MoveFailureRetryCount;
	UE_LOG(LogFactoryDispatch, Warning, TEXT("[%s] 이동 실패(Code=%d) — %.1f초 후 재시도(%d/%d)"),
		*GetPawnDisplayName(GetPawn()), static_cast<int32>(Result.Code.GetValue()),
		MoveRetryDelaySeconds, MoveFailureRetryCount, MaxMoveRetryAttempts);
	GetWorldTimerManager().SetTimer(MoveRetryTimerHandle, this, &AFactoryAIController::RetryLastMove, MoveRetryDelaySeconds, false);
}

void AFactoryAIController::RetryLastMove()
{
	RequestMoveWithFilter(LastRequestedDestination);
}

void AFactoryAIController::RequestMoveWithFilter(const FVector& Destination)
{
	// 새 목적지면 재시도 카운트를 리셋한다 — RetryLastMove가 같은 목적지로 다시 부를 때는
	// (LastRequestedDestination과 동일) 리셋하지 않아 OnMoveCompleted가 늘린 카운트가 유지된다.
	if (!Destination.Equals(LastRequestedDestination))
	{
		MoveFailureRetryCount = 0;
	}
	LastRequestedDestination = Destination;

	FAIMoveRequest MoveRequest(Destination);
	MoveRequest.SetNavigationFilter(QueryFilterClass);
	const FPathFollowingRequestResult Result = MoveTo(MoveRequest);

	// 버그 수정(사용자 리포트) — MoveTo()의 동기 결과를 지금까지 아무도 확인하지 않아서, 요청 자체가
	// 즉시(동기적으로) 실패하면 OnMoveCompleted도 아예 호출되지 않아 재시도 로직조차 안 걸리고 조용히
	// 아무 일도 일어나지 않았다. 정비 참여는 도착 판정 없이 배정 시점에 즉시 처리되므로, 이동이 이렇게
	// 조용히 실패해도 상태머신(Idle→UnderRepair→Idle)은 정상처럼 계속 돌아가 눈치채기 어려웠다 — 성공/실패
	// 무관하게 항상 결과를 남겨 "요청은 성공했는데 실제로는 안 움직이는" 케이스와 구분한다.
	UE_LOG(LogFactoryDispatch, Log, TEXT("[%s] RequestMoveWithFilter — MoveTo 결과 Code=%d, PathFollowingStatus=%d, 목적지=%s"),
		*GetPawnDisplayName(GetPawn()), static_cast<int32>(Result.Code),
		GetPathFollowingComponent() ? static_cast<int32>(GetPathFollowingComponent()->GetStatus()) : -1,
		*Destination.ToString());
}

void AFactoryAIController::SetAvoidanceIgnoreActor(AActor* TargetActor, bool bIgnore)
{
	UCrowdFollowingComponent* MyCrowd = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent());
	UCrowdFollowingComponent* TargetCrowd = FindCrowdFollowingComponent(Cast<APawn>(TargetActor));
	if (!MyCrowd || !TargetCrowd)
	{
		return;
	}

	// 버그 수정 — 상호 무시가 되려면 양쪽 다 "GroupsToAvoid에서 빼고 AvoidanceGroup엔 더한다"를
	// 동일하게 적용해야 한다. 기존엔 My->GroupsToAvoid와 Target->AvoidanceGroup만 건드려
	// My가 Target을 피하지 않게만 됐을 뿐, Target은 여전히 My를 피하려 했다.
	MyCrowd->SetGroupsToAvoid(bIgnore
		? (MyCrowd->GetGroupsToAvoid() & ~MaintenanceIgnoreAvoidanceGroup)
		: (MyCrowd->GetGroupsToAvoid() | MaintenanceIgnoreAvoidanceGroup));
	MyCrowd->SetAvoidanceGroup(bIgnore
		? (MyCrowd->GetAvoidanceGroup() | MaintenanceIgnoreAvoidanceGroup)
		: (MyCrowd->GetAvoidanceGroup() & ~MaintenanceIgnoreAvoidanceGroup));

	TargetCrowd->SetGroupsToAvoid(bIgnore
		? (TargetCrowd->GetGroupsToAvoid() & ~MaintenanceIgnoreAvoidanceGroup)
		: (TargetCrowd->GetGroupsToAvoid() | MaintenanceIgnoreAvoidanceGroup));
	TargetCrowd->SetAvoidanceGroup(bIgnore
		? (TargetCrowd->GetAvoidanceGroup() | MaintenanceIgnoreAvoidanceGroup)
		: (TargetCrowd->GetAvoidanceGroup() & ~MaintenanceIgnoreAvoidanceGroup));
}
