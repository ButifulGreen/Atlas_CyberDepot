// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agent/FactoryAgentBase.h"
#include "Atlas_CyberDepot.h"
#include "Agent/FactoryAIController.h"
#include "Infrastructure/IdleWaitingZone.h"
#include "Navigation/FactoryNavWaypoint.h"
#include "Navigation/FactoryWaypointNavigationSubsystem.h"
#include "EventBus/FactoryEventBusSubsystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/OverlapResult.h"
#include "Assignment/OutboundDispatchSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "Components/CapsuleComponent.h"

AFactoryAgentBase::AFactoryAgentBase()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
}

void AFactoryAgentBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (HasAuthority() && CurrentState == EAgentState::Working)
	{
		OnWorkingTick(DeltaTime);
	}

	// Blocked 판정과 그에 따른 ACostZoneVolume 등록/해제는 서버 권한 로직이라 서버에서만 수행한다.
	// Pause(안전거리/FinalHop 트레이스로 대기 중)도 Moving과 함께 대상에 포함 — 정지 이유가 다를 뿐
	// 여전히 "이동 의도가 있는데 못 가는 중"이라 같은 판정을 받아야 한다.
	if (!HasAuthority() || (CurrentState != EAgentState::Moving && CurrentState != EAgentState::Pause))
	{
		BlockedTimer = 0.f;
		LastBlockedRecoveryAttemptSeconds = 0.f;
		return;
	}

	const bool bIsStationary = GetVelocity().SizeSquared() < KINDA_SMALL_NUMBER;
	if (!bIsStationary)
	{
		if (BlockedTimer >= BlockedThresholdSeconds)
		{
			OnUnblocked();
		}
		BlockedTimer = 0.f;
		LastBlockedRecoveryAttemptSeconds = 0.f;
		return;
	}

	BlockedTimer += DeltaTime;
	if (BlockedTimer >= BlockedThresholdSeconds)
	{
		OnBlockedTick(DeltaTime);

		// 버그 수정(사용자 요청, 대비책) — 그래프 구간/FinalHop 안전 트레이스 둘 다 AFactoryAgentBase
		// 파생 액터만 감지 대상이라(Cast 실패 시 그냥 무시), 선반 같은 정적 지오메트리에 막히면 아무
		// 것도 감지되지 않아 Pause도 안 걸리고 영구 정지할 수 있다. Pause(대기+재확인, 자체 타임아웃 로직
		// 보유)는 제외하고 — 겉으로 "이동 중"이라면서 실제로는 BlockedThresholdSeconds 넘게 안 움직인
		// 경우만, 원인을 가리지 않고 이 간격으로 주기적으로 강제 재탐색을 시도한다. 근본 원인 진단 없이
		// 넣는 최후의 안전망이다.
		// 버그 수정(사용자 지시) — Waitbound 대기(bAwaitingWaitboundClearance)와 다음 홉 재시도 대기
		// (bIsWaitingForNextHopReservation)는 스스로 무기한 재시도하는 정상적인 대기 상태인데, 이
		// 안전망이 2초마다 끼어들어 강제로 재탐색을 시켜버리면 그 두 메커니즘이 지키려던 "같은 자리에서
		// 계속 기다리기"가 깨지고 엉뚱한 노드로 튀는 문제가 재발한다(혼잡 구간에서 실제 재현) — 둘 다
		// 제외한다.
		if (CurrentState == EAgentState::Moving && !bAwaitingWaitboundClearance && !bIsWaitingForNextHopReservation &&
			BlockedTimer - LastBlockedRecoveryAttemptSeconds >= BlockedRecoveryRetryIntervalSeconds)
		{
			LastBlockedRecoveryAttemptSeconds = BlockedTimer;
			UE_LOG(LogFactoryDispatch, Warning, TEXT("[Nav] %s %.1f초 이상 정지 감지(정적 장애물 등 트레이스로 못 잡는 경우 대비) — 강제 재탐색"),
				*GetName(), BlockedTimer);
			AbandonWaypointRouteAndReroute();
		}
	}
}

void AFactoryAgentBase::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && !AgentID.IsValid())
	{
		AgentID = FGuid::NewGuid();
	}

	// Docs에 없는 구현값 — 정비 임계치 테스트용. GetRepairComponent()가 있는(=Atlas/TransportRobot) 에이전트만 표시.
	if (GetRepairComponent())
	{
		GetWorldTimerManager().SetTimer(DebugOperationCountTimerHandle, this, &AFactoryAgentBase::DrawDebugOperationCountLabel, 1.f, true);
	}

	// Docs/08_Navigation.md — 안전거리 감지도 GetRepairComponent()가 있는(=Atlas/TransportRobot)
	// 에이전트만 대상(NPC는 스코프 밖, 기존 Detour Crowd 그대로 유지). 서버 권한에서만 판정한다.
	if (HasAuthority() && GetRepairComponent())
	{
		GetComponents<USafetyTraceMarkerComponent>(SafetyTraceMarkers);
		GetWorldTimerManager().SetTimer(SafetyTraceTimerHandle, this, &AFactoryAgentBase::RunSafetyTraceCheck, SafetyTraceIntervalSeconds, true);
	}
}

void AFactoryAgentBase::DrawDebugOperationCountLabel()
{
	if (!GetCapsuleComponent())
	{
		return;
	}

	const FVector Location = GetCapsuleComponent()->GetComponentLocation() + FVector(0.f, 0.f, 80.f);
	DrawDebugString(GetWorld(), Location, FString::FromInt(GetOperationCount()), nullptr, FColor::Yellow, 1.1f, false, 1.3f);
}

void AFactoryAgentBase::SetState(EAgentState NewState)
{
	if (CurrentState == NewState)
	{
		return;
	}

	// 디버그 편의 — 상태 전환 자체를 로그로 안 남기고 있어서, "이 로봇이 실제로 Working까지 갔는지"를
	// 확인하려면 매번 다른 로직의 로그를 조합해 추측해야 했다. 모든 에이전트(아틀라스/배송로봇/NPC)에
	// 공용이라 여기 한 곳에서 남기면 어디서든 "[에이전트명] 상태 전환"으로 검색해 타임라인을 바로 본다.
	UE_LOG(LogFactoryDispatch, Log, TEXT("[%s] 상태 전환 %s → %s"),
		*GetName(), *UEnum::GetValueAsString(CurrentState), *UEnum::GetValueAsString(NewState));

	CurrentState = NewState;

	// 서버 자신에게는 OnRep이 자동 호출되지 않으므로 명시적으로 호출
	if (HasAuthority())
	{
		OnRep_CurrentState();
	}
}

void AFactoryAgentBase::OnBlockedTick(float DeltaTime)
{
}

void AFactoryAgentBase::OnUnblocked()
{
}

void AFactoryAgentBase::AssignHomeIdleZoneSlot(AIdleWaitingZone* Zone, int32 SlotIndex)
{
	HomeIdleZone = Zone;
	HomeSlotIndex = SlotIndex;
}

bool AFactoryAgentBase::TryHeadToIdleZone()
{
	if (bIsParkedInIdleZone || bIsHeadingToIdleZone)
	{
		return true;
	}

	AIdleWaitingZone* Zone = HomeIdleZone.Get();
	if (!Zone || HomeSlotIndex < 0)
	{
		return false;
	}

	if (!Cast<AFactoryAIController>(GetController()))
	{
		return false;
	}

	FTransform SlotTransform;
	if (!Zone->GetHomeSlotTransform(HomeSlotIndex, SlotTransform))
	{
		return false;
	}

	bIsHeadingToIdleZone = true;
	SetState(EAgentState::Moving);

	// 버그 수정(회피 재설계) — 선반/트레이/대기실 이탈과 동일한 원칙으로 통일. 고정 AccessWaypoint를
	// 참조하던 방식을 버리고, 목표 마커(홈 슬롯 위치) 기준으로 가장 가깝고 실제로 도달 가능한
	// 웨이포인트를 매번 동적으로 찾는다. 실패 시 직행 폴백 없이 TryRequestWaypointRoute 내부 재시도에만 맡긴다.
	TryRequestWaypointRoute(nullptr, SlotTransform.GetLocation());
	return true;
}

UFactoryWaypointNavigationSubsystem* AFactoryAgentBase::GetWaypointNavSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UFactoryWaypointNavigationSubsystem>() : nullptr;
}

void AFactoryAgentBase::AbandonAnyActiveWaypointRoute()
{
	if (TravelPhase == EWaypointTravelPhase::TraversingGraph && PendingWaypointRoute.IsValidIndex(WaypointRouteIndex))
	{
		if (AFactoryNavWaypoint* HeldNode = PendingWaypointRoute[WaypointRouteIndex].Get())
		{
			HeldNode->Release(this);
		}
	}

	PendingWaypointRoute.Reset();
	WaypointRouteIndex = 0;
	TravelPhase = EWaypointTravelPhase::None;
	// 새 요청은 이전에 걸려있던 자동 재시도를 대체한다(안 그러면 오래된 재시도가 나중에 엉뚱하게 발동할 수 있음).
	GetWorldTimerManager().ClearTimer(WaypointRetryTimerHandle);
	bHasPendingWaypointRetry = false;
	// Waitbound 대기 중에 새 이동 의도가 끼어드는 경우도 동일하게 정리 — 위의 HeldNode->Release가 이미
	// Waitbound 노드 예약도 반납해준다(TravelPhase==TraversingGraph인 동안은 계속 그 노드를 쥐고 있으므로).
	GetWorldTimerManager().ClearTimer(WaitboundRecheckTimerHandle);
	bAwaitingWaitboundClearance = false;
	// 다음 홉 예약 재시도 대기 중이었다면 그것도 정리 — 안 그러면 이미 무효해진 인덱스를 나중에 참조한다.
	GetWorldTimerManager().ClearTimer(NextHopRetryTimerHandle);
	bIsWaitingForNextHopReservation = false;

	// Pause 지속시간 누적도 완전히 새로운 이동 의도로 초기화한다 — 뒤이어 호출되는 TryRequestWaypointRoute의
	// SetState(Moving)이 CurrentState==Pause였더라도 정상적으로 벗어나게 한다.
	PauseAccumulatedSeconds = 0.f;
}

bool AFactoryAgentBase::TryRequestWaypointRoute(AFactoryNavWaypoint* TargetWaypoint, const FVector& FinalHopTarget)
{
	AbandonAnyActiveWaypointRoute();

	UFactoryWaypointNavigationSubsystem* NavSubsystem = GetWaypointNavSubsystem();
	AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController());
	if (!NavSubsystem || !AIController)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Nav] %s TryRequestWaypointRoute 실패 — NavSubsystem 또는 AIController 없음"), *GetName());
		return false;
	}

	// 버그 수정 — 단일 최근접 웨이포인트만 시작점으로 시도하면, 마커 진입 전용이라 나가는 연결이 없는
	// 노드(Docs/08_Navigation.md의 Inbound "탈출 지점")가 우연히 가장 가까울 때 영구 실패했다. 가까운
	// 후보부터 순서대로 시도해 실제로 Target까지 뚫리는 시작점을 찾는다. TargetWaypoint가 없으면(슬롯/
	// 트레이마다 사람이 미리 지정해둔 고정 도킹 참조 대신) FinalHopTarget에 가장 가깝고 실제로 도달
	// 가능한 웨이포인트를 목표 쪽도 매번 동적으로 찾는다.
	TArray<AFactoryNavWaypoint*> Route;
	const bool bRouteFound = TargetWaypoint
		? NavSubsystem->FindPathFromNearestReachable(GetActorLocation(), TargetWaypoint, this, Route)
		: NavSubsystem->FindPathToNearestMarkerWaypoint(GetActorLocation(), FinalHopTarget, this, Route);

	if (!bRouteFound || Route.Num() == 0)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Nav] %s TryRequestWaypointRoute 실패 — 현재 위치(%s)에서 %s까지 경로 없음(모든 후보 웨이포인트 탐색 실패) — %.1f초 후 재시도"),
			*GetName(), *GetActorLocation().ToString(), TargetWaypoint ? *TargetWaypoint->GetName() : *FinalHopTarget.ToString(), WaypointRetryIntervalSeconds);
		bHasPendingWaypointRetry = true;
		PendingRetryTargetWaypoint = TargetWaypoint;
		PendingRetryFinalHopTarget = FinalHopTarget;
		GetWorldTimerManager().SetTimer(WaypointRetryTimerHandle, this, &AFactoryAgentBase::RetryWaypointRoute, WaypointRetryIntervalSeconds, false);
		return false;
	}

	AFactoryNavWaypoint* StartWaypoint = Route[0];
	AFactoryNavWaypoint* ResolvedTarget = Route.Last();

	PendingWaypointRoute.Reset(Route.Num());
	for (AFactoryNavWaypoint* Waypoint : Route)
	{
		PendingWaypointRoute.Add(Waypoint);
	}
	PendingFinalHopTarget = FinalHopTarget;

	if (PendingWaypointRoute.Num() == 1)
	{
		// 이미 목표 웨이포인트 위 — 그래프 이동 없이 바로 최종 홉만 수행.
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Nav] %s 이미 목표 웨이포인트(%s) 위 — 최종 홉만 수행"), *GetName(), *ResolvedTarget->GetName());
		TravelPhase = EWaypointTravelPhase::FinalHop;
		WaypointRouteIndex = 0;
		SetState(EAgentState::Moving);
		AIController->RequestMoveWithFilter(PendingFinalHopTarget);
		return true;
	}

	WaypointRouteIndex = 1;
	AFactoryNavWaypoint* FirstHop = PendingWaypointRoute[WaypointRouteIndex].Get();
	if (!FirstHop || !FirstHop->TryReserve(this))
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Nav] %s TryRequestWaypointRoute 실패 — 첫 홉(%s) 예약 실패(경합) — %.1f초 후 재시도"),
			*GetName(), FirstHop ? *FirstHop->GetName() : TEXT("Invalid"), WaypointRetryIntervalSeconds);
		PendingWaypointRoute.Reset();
		WaypointRouteIndex = 0;
		bHasPendingWaypointRetry = true;
		PendingRetryTargetWaypoint = TargetWaypoint;
		PendingRetryFinalHopTarget = FinalHopTarget;
		GetWorldTimerManager().SetTimer(WaypointRetryTimerHandle, this, &AFactoryAgentBase::RetryWaypointRoute, WaypointRetryIntervalSeconds, false);
		return false;
	}

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Nav] %s 경로 확정(%d개 노드) — %s → ... → %s, 첫 홉 %s로 이동 시작"),
		*GetName(), PendingWaypointRoute.Num(), *StartWaypoint->GetName(), *ResolvedTarget->GetName(), *FirstHop->GetName());
	TravelPhase = EWaypointTravelPhase::TraversingGraph;
	SetState(EAgentState::Moving);
	AIController->RequestMoveWithFilter(FirstHop->GetActorLocation());
	return true;
}

void AFactoryAgentBase::OnMoveFailedPermanently()
{
	// 버그 수정 — 이동 요청 자체가 계속 실패하면(레벨 지오메트리/NavMesh 문제 등) 웨이포인트 경로 상태가
	// 예약된 채로 영구히 남아 그 노드를 다른 로봇도 못 쓰게 될 수 있었다. 점유만 반납하고 자동 재시도는
	// 하지 않는다 — 목적지가 근본적으로 도달 불가능하면 재시도해도 똑같이 실패한다.
	AbandonAnyActiveWaypointRoute();

	// 버그 수정 — 대기실로 향하던 중 이동이 영구 실패하면 bIsHeadingToIdleZone이 true로 눌러붙어
	// TryHeadToIdleZone()이 이후 "이미 가는 중"이라고 착각해 조용히 무시하는 문제가 있었다.
	bIsHeadingToIdleZone = false;
}

void AFactoryAgentBase::RetryWaypointRoute()
{
	// 버그 수정 — PendingRetryTargetWaypoint가 nullptr인 게 이제 "마커 기준 동적 탐색" 유효 상태라
	// Target 포인터 유무가 아니라 이 플래그로 재시도 예약 여부를 판별한다.
	if (!bHasPendingWaypointRetry)
	{
		return;
	}
	bHasPendingWaypointRetry = false;

	TryRequestWaypointRoute(PendingRetryTargetWaypoint.Get(), PendingRetryFinalHopTarget);
}

bool AFactoryAgentBase::TryHandleWaypointRouteArrival()
{
	if (TravelPhase == EWaypointTravelPhase::None)
	{
		return false;
	}

	if (TravelPhase == EWaypointTravelPhase::FinalHop)
	{
		// 최종 홉까지 완료 — 그래프 이동 종료, 호출부(파생 클래스)가 평소처럼 처리하게 넘긴다.
		PendingWaypointRoute.Reset();
		WaypointRouteIndex = 0;
		TravelPhase = EWaypointTravelPhase::None;
		return false;
	}

	// TraversingGraph — WaypointRouteIndex는 방금 도착한 노드를 가리킨다.
	AFactoryNavWaypoint* ArrivedNode = PendingWaypointRoute[WaypointRouteIndex].Get();
	AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController());

	const bool bReachedLastWaypoint = (WaypointRouteIndex >= PendingWaypointRoute.Num() - 1);

	// 버그 수정(사용자 지시, Waitbound) — 마지막 노드가 Waitbound면 통과 허가 전까지 예약을 반납하지 않고
	// 그 자리에서 대기한다(다른 로봇이 이 노드로 들어오지 못하게). 아래의 "도착 즉시 반납" 일반 규칙에서
	// 이 경우만 예외.
	if (bReachedLastWaypoint && ArrivedNode && ArrivedNode->HasAccessFlag(EWaypointAccess::Waitbound) && !CanProceedFromWaitbound())
	{
		bAwaitingWaitboundClearance = true;
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Nav] %s Waitbound(%s) 대기 — 통과 조건 미충족, %.1f초 후 재확인"),
			*GetName(), *ArrivedNode->GetName(), WaitboundRecheckIntervalSeconds);
		GetWorldTimerManager().SetTimer(WaitboundRecheckTimerHandle, this, &AFactoryAgentBase::RecheckWaitboundClearance, WaitboundRecheckIntervalSeconds, false);
		return true;
	}

	// 그 외(중간 노드, 또는 Waitbound인데 즉시 통과 가능)는 도킹/레인 노드가 관문일 뿐 정지 지점이 아니므로
	// 도착 즉시 반납해 뒤따르는 로봇이 바로 쓸 수 있게 한다.
	if (ArrivedNode)
	{
		ArrivedNode->Release(this);
	}

	if (bReachedLastWaypoint)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Nav] %s 그래프 마지막 노드 도착 — 최종 좌표(%s)로 짧은 직진 이동"),
			*GetName(), *PendingFinalHopTarget.ToString());
		TravelPhase = EWaypointTravelPhase::FinalHop;
		if (AIController)
		{
			AIController->RequestMoveWithFilter(PendingFinalHopTarget);
		}
		return true;
	}

	++WaypointRouteIndex;
	AFactoryNavWaypoint* NextNode = PendingWaypointRoute[WaypointRouteIndex].Get();
	if (!NextNode)
	{
		// 노드 자체가 유효하지 않으면(레벨 변경 등) 기다려도 소용없다 — 전체 재탐색만이 답이다.
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Nav] %s 다음 홉 노드가 유효하지 않음 — 현재 위치 기준 재탐색"), *GetName());
		AbandonWaypointRouteAndReroute();
		return true;
	}

	if (!NextNode->TryReserve(this))
	{
		// 버그 수정(사용자 지시) — 예전엔 예약 실패 즉시 전체 재탐색을 했는데, 지금 막힌 이 노드 하나
		// 때문에 경로 전체를 처음부터 다시 짜면서 엉뚱하게 먼 후보로 빠질 수 있었다(실제 재현 — 60개
		// 노드 경로 중간에서 재탐색이 걸려 수 홉 전 노드로 되돌아감). 원래 계획했던 같은 노드를 그대로
		// 유지한 채 이 자리에서 기다렸다가 같은 노드 예약만 다시 시도한다 — 대개 점유는 금방 풀린다.
		// 타임아웃 없이 무기한 대기(Tick의 BlockedTimer 안전망은 아래 플래그로 이 대기를 건드리지 않음).
		bIsWaitingForNextHopReservation = true;
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Nav] %s 다음 홉(%s) 예약 실패(경합) — 같은 목표로 %.1f초 후 재시도"),
			*GetName(), *NextNode->GetName(), WaypointRetryIntervalSeconds);
		GetWorldTimerManager().SetTimer(NextHopRetryTimerHandle, this, &AFactoryAgentBase::RetryNextHopReservation, WaypointRetryIntervalSeconds, false);
		return true;
	}

	UE_LOG(LogFactoryDispatch, Log, TEXT("[Nav] %s 다음 홉(%d/%d) %s로 이동"),
		*GetName(), WaypointRouteIndex, PendingWaypointRoute.Num() - 1, *NextNode->GetName());
	if (AIController)
	{
		AIController->RequestMoveWithFilter(NextNode->GetActorLocation());
	}
	return true;
}

void AFactoryAgentBase::RetryNextHopReservation()
{
	if (TravelPhase != EWaypointTravelPhase::TraversingGraph || !PendingWaypointRoute.IsValidIndex(WaypointRouteIndex))
	{
		bIsWaitingForNextHopReservation = false;
		return;
	}

	AFactoryNavWaypoint* NextNode = PendingWaypointRoute[WaypointRouteIndex].Get();
	if (!NextNode)
	{
		bIsWaitingForNextHopReservation = false;
		AbandonWaypointRouteAndReroute();
		return;
	}

	if (!NextNode->TryReserve(this))
	{
		GetWorldTimerManager().SetTimer(NextHopRetryTimerHandle, this, &AFactoryAgentBase::RetryNextHopReservation, WaypointRetryIntervalSeconds, false);
		return;
	}

	bIsWaitingForNextHopReservation = false;
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Nav] %s 다음 홉(%d/%d) %s로 이동(대기 후 재시도 성공)"),
		*GetName(), WaypointRouteIndex, PendingWaypointRoute.Num() - 1, *NextNode->GetName());
	if (AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController()))
	{
		AIController->RequestMoveWithFilter(NextNode->GetActorLocation());
	}
}

void AFactoryAgentBase::RecheckWaitboundClearance()
{
	// AbandonAnyActiveWaypointRoute가 그 사이 경로를 정리했으면(플래그도 같이 초기화됨) 여기서 조용히 끝낸다.
	if (!bAwaitingWaitboundClearance)
	{
		return;
	}

	if (!CanProceedFromWaitbound())
	{
		GetWorldTimerManager().SetTimer(WaitboundRecheckTimerHandle, this, &AFactoryAgentBase::RecheckWaitboundClearance, WaitboundRecheckIntervalSeconds, false);
		return;
	}

	bAwaitingWaitboundClearance = false;

	if (!PendingWaypointRoute.IsValidIndex(WaypointRouteIndex))
	{
		return;
	}

	if (AFactoryNavWaypoint* ArrivedNode = PendingWaypointRoute[WaypointRouteIndex].Get())
	{
		ArrivedNode->Release(this);
	}

	AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController());
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Nav] %s Waitbound 통과 허가 — 최종 좌표(%s)로 짧은 직진 이동"),
		*GetName(), *PendingFinalHopTarget.ToString());
	TravelPhase = EWaypointTravelPhase::FinalHop;
	if (AIController)
	{
		AIController->RequestMoveWithFilter(PendingFinalHopTarget);
	}
}

void AFactoryAgentBase::AbandonWaypointRouteAndReroute()
{
	if (TravelPhase == EWaypointTravelPhase::None)
	{
		return;
	}

	// 버그 수정(사용자 요청) — 기존엔 TraversingGraph(그래프 구간)에서만 동작했다. FinalHop(그래프를
	// 벗어난 마지막 접근) 중 정적 지오메트리 등에 막히는 경우는 두 안전 트레이스 모두 감지를 못 해
	// Pause조차 안 걸리므로, Tick의 BlockedTimer 기반 강제 재탐색이 유일한 복구 수단이 되도록 FinalHop도
	// 처리 대상에 포함한다.
	const bool bWasFinalHop = (TravelPhase == EWaypointTravelPhase::FinalHop);
	UE_LOG(LogFactoryDispatch, Log, TEXT("[Nav] %s 경로 포기(%s) — 재탐색"),
		*GetName(), bWasFinalHop ? TEXT("FinalHop") : TEXT("그래프 구간"));

	AFactoryNavWaypoint* FinalTargetWaypoint = (!bWasFinalHop && PendingWaypointRoute.Num() > 0) ? PendingWaypointRoute.Last().Get() : nullptr;
	const FVector FinalHopTarget = PendingFinalHopTarget;

	AbandonAnyActiveWaypointRoute();

	if (bWasFinalHop)
	{
		// 그래프 마지막 노드 참조는 이미 반납되어 없으므로(FinalHop 진입 시점에 반납됨), 마커 좌표
		// 기준으로 처음부터 동적 재탐색한다(TryRequestWaypointRoute(nullptr, ...)와 동일 패턴).
		TryRequestWaypointRoute(nullptr, FinalHopTarget);
	}
	else if (FinalTargetWaypoint)
	{
		TryRequestWaypointRoute(FinalTargetWaypoint, FinalHopTarget);
	}
}

bool AFactoryAgentBase::TryHandleIdleZoneArrival()
{
	if (!bIsHeadingToIdleZone)
	{
		return false;
	}

	bIsHeadingToIdleZone = false;
	SetState(EAgentState::Idle);

	// 버그 수정 — 예전엔 TryHeadToIdleZone(출발 시점)에서 바로 파킹 등록(SlotOccupancy/bIsParkedInIdleZone)을
	// 해버려서, 대기실에 도착하기도 전에 RestDecay(패시브 감쇠)와 개별 QuickCheck 판정(OnAgentBecameIdle)이
	// 적용되고 있었다. 실제 도착 시점인 여기서만 등록하도록 이동.
	if (AIdleWaitingZone* Zone = HomeIdleZone.Get())
	{
		Zone->MarkSlotOccupied(this, HomeSlotIndex);
	}

	// 버그 수정 — OnAssignmentExhausted/OnTaskCompleted/ResumeAfterRepair는 Idle 전환 직후 전부
	// TryDispatchIdleAgents를 호출해 대기 중인 작업을 재점검하는데, 여기(대기실 실제 도착)만 빠져 있었다.
	// 그래서 미배정 작업이 있어도 마침 이 로봇이 그 시점에 Idle이 아니었다면(이동 중이었다면) 이후 도착해도
	// 아무도 다시 확인하지 않아 작업이 영구 미아가 됐다(로봇 수 대비 작업이 많을 때 특히 잘 드러남).
	if (UWorld* World = GetWorld())
	{
		if (UOutboundDispatchSubsystem* Dispatch = World->GetSubsystem<UOutboundDispatchSubsystem>())
		{
			Dispatch->TryDispatchIdleAgents();
		}
	}

	return true;
}

void AFactoryAgentBase::LeaveIdleZoneIfParked()
{
	if (AIdleWaitingZone* Zone = HomeIdleZone.Get())
	{
		Zone->ReleaseSlot(this);
	}

	bIsHeadingToIdleZone = false;
}

FStateSnapshot AFactoryAgentBase::ToSnapshot() const
{
	FStateSnapshot Snapshot;
	Snapshot.Timestamp = FDateTime::UtcNow();
	Snapshot.ActorID = AgentID;
	Snapshot.ActorType = AgentType;
	Snapshot.CurrentState = CurrentState;
	Snapshot.Location = GetActorLocation();
	Snapshot.Rotation = GetActorRotation();
	Snapshot.Velocity = GetVelocity();
	return Snapshot;
}

void AFactoryAgentBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFactoryAgentBase, AgentID);
	DOREPLIFETIME(AFactoryAgentBase, CurrentState);
}

void AFactoryAgentBase::OnRep_CurrentState()
{
	// 하위 클래스에서 상태 전환 시각/애니메이션 반응을 위해 오버라이드
}

void AFactoryAgentBase::RunSafetyTraceCheck()
{
	if (CurrentState != EAgentState::Moving && CurrentState != EAgentState::Pause)
	{
		return;
	}

	AFactoryAIController* AIController = Cast<AFactoryAIController>(GetController());
	if (!AIController)
	{
		return;
	}

	if (TravelPhase == EWaypointTravelPhase::FinalHop)
	{
		RunFinalHopAreaTraceCheck(AIController);
	}
	else
	{
		RunGraphSegmentTraceCheck(AIController);
	}
}

void AFactoryAgentBase::RunGraphSegmentTraceCheck(AFactoryAIController* AIController)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	AFactoryAgentBase* TripPartner = GetCurrentTripPartner();
	if (TripPartner)
	{
		Params.AddIgnoredActor(TripPartner);
	}

	AFactoryAgentBase* DetectedAgent = nullptr;
	float DetectedDistance = TNumericLimits<float>::Max();

	auto ConsiderHit = [&DetectedAgent, &DetectedDistance](const FHitResult& Hit)
	{
		if (Hit.Distance >= DetectedDistance)
		{
			return;
		}
		if (AFactoryAgentBase* HitAgent = Cast<AFactoryAgentBase>(Hit.GetActor()))
		{
			DetectedAgent = HitAgent;
			DetectedDistance = Hit.Distance;
		}
	};

	// 마커가 하나도 안 배치돼 있으면(과도기) 이동/정면 벡터 기준 1방향으로 대체.
	if (SafetyTraceMarkers.Num() == 0)
	{
		FVector Direction = GetVelocity();
		if (!Direction.Normalize())
		{
			Direction = GetActorForwardVector();
		}

		const FVector Start = GetActorLocation();
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Start, Start + Direction * SafeDistanceUnits, ECC_Pawn, Params))
		{
			ConsiderHit(Hit);
		}
	}
	else
	{
		// 버그 수정 — 마커별로 첫 히트에서 break하면 "먼저 검사한 마커가 우연히 잡은 대상"이 채택될 수
		// 있었다. 모든 마커를 다 검사해서 전체에서 가장 가까운 대상을 채택한다(길이도 최장 변보다
		// 길어서 한 트레이스에도 여러 후보가 잡힐 수 있음).
		for (const TObjectPtr<USafetyTraceMarkerComponent>& Marker : SafetyTraceMarkers)
		{
			if (!Marker)
			{
				continue;
			}

			const FVector Start = Marker->GetComponentLocation();
			const FVector End = Start + Marker->GetForwardVector() * SafeDistanceUnits;

			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params))
			{
				ConsiderHit(Hit);
			}
		}
	}

	if (!DetectedAgent)
	{
		if (CurrentState == EAgentState::Pause)
		{
			ResumeFromPause(AIController, TEXT("감지 대상 없음"));
		}
		return;
	}

	PublishSafetyDetectionEvent(DetectedAgent, DetectedDistance);

	if (DetectedAgent->CurrentState == EAgentState::Broken)
	{
		// 고장은 곧 안 풀린다 — 기다리지 않고 즉시 재탐색(Abandon 내부에서 새 이동 요청이
		// 지금 진행 중이던 이동을 대체하므로 별도 Resume이 필요 없다).
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Safety] %s가 %.0f 거리에서 고장난 %s 감지 — 즉시 재탐색"),
			*GetName(), DetectedDistance, *DetectedAgent->GetName());
		PauseAccumulatedSeconds = 0.f;
		AbandonWaypointRouteAndReroute();
		return;
	}

	// 버그 수정(사용자 지시, 회피 재설계) — 나와 다른 EActorType이 Moving 중이면 지속적인 속도차로
	// 뒤에서 계속 따라붙히는 상황이라(예: 배송로봇이 아틀라스 뒤에 붙음) 기다려봐야 다시 벌어지지
	// 않는다 — 즉시 다른 경로를 찾는다. 그 외(같은 타입은 상대 상태 무관 — 감속 중인 같은 타입은 곧
	// 다시 같은 속도로 붙는다, 다른 타입이라도 정지 상태)는 Pause로 대기한다.
	const bool bDifferentTypeMoving = (DetectedAgent->AgentType != AgentType) && (DetectedAgent->CurrentState == EAgentState::Moving);
	if (bDifferentTypeMoving)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Safety] %s가 이종 타입 %s(Moving) 감지 — 즉시 재탐색"),
			*GetName(), *DetectedAgent->GetName());
		PauseAccumulatedSeconds = 0.f;
		AbandonWaypointRouteAndReroute();
		return;
	}

	// 버그 수정 — 상대도 Moving이면 상대 쪽에서도 동시에 나를 감지해 똑같이 Pause를 걸 수 있다.
	// 둘 다 양보하면 서로가 서로를 풀어주길 기다리는 영구 교착 상태가 된다(둘 다 Moving에 멈춘 채
	// 실패 로그도 없이 방치 — 실제로 재현된 증상). AgentID를 결정론적 우선순위로 써서 한쪽만
	// 양보하게 한다.
	if (DetectedAgent->CurrentState == EAgentState::Moving && GetTypeHash(AgentID) > GetTypeHash(DetectedAgent->AgentID))
	{
		if (CurrentState == EAgentState::Pause)
		{
			ResumeFromPause(AIController, TEXT("상호 교착 우선순위"));
		}
		return;
	}

	EnterOrMaintainPause(AIController, DetectedAgent, DetectedDistance);
}

void AFactoryAgentBase::EnterOrMaintainPause(AFactoryAIController* AIController, const AFactoryAgentBase* DetectedAgent, float Distance)
{
	if (CurrentState != EAgentState::Pause)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Safety] %s가 %.0f 거리에서 %s(상태=%s) 감지 — Pause 전환"),
			*GetName(), Distance, *DetectedAgent->GetName(), *UEnum::GetValueAsString(DetectedAgent->CurrentState));
		SetState(EAgentState::Pause);
		PauseAccumulatedSeconds = 0.f;
		if (UPathFollowingComponent* PathFollowing = AIController->GetPathFollowingComponent())
		{
			PathFollowing->PauseMove();
		}
		return;
	}

	// 버그 수정(사용자 지시) — Pause가 길어지면(체인으로 여러 대가 순차적으로 발이 묶이는 등) 원인을
	// 가리지 않고 그래프 구간 한정으로 재탐색을 시도한다. FinalHop 중 걸린 Pause는 여기 안 들어온다
	// (AbandonWaypointRouteAndReroute가 TraversingGraph 전용이라 자동으로 no-op).
	PauseAccumulatedSeconds += SafetyTraceIntervalSeconds;
	if (PauseAccumulatedSeconds >= PauseRerouteTimeoutSeconds)
	{
		UE_LOG(LogFactoryDispatch, Warning, TEXT("[Safety] %s Pause %.1f초 지속 — 재탐색 시도"), *GetName(), PauseAccumulatedSeconds);
		PauseAccumulatedSeconds = 0.f;
		AbandonWaypointRouteAndReroute();
	}
}

void AFactoryAgentBase::ResumeFromPause(AFactoryAIController* AIController, const TCHAR* Reason)
{
	if (CurrentState == EAgentState::Pause)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Safety] %s Pause 해제(%s) — 이동 재개"), *GetName(), Reason);
		SetState(EAgentState::Moving);
	}
	PauseAccumulatedSeconds = 0.f;
	if (UPathFollowingComponent* PathFollowing = AIController->GetPathFollowingComponent())
	{
		PathFollowing->ResumeMove();
	}
}

void AFactoryAgentBase::RunFinalHopAreaTraceCheck(AFactoryAIController* AIController)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	AFactoryAgentBase* TripPartner = GetCurrentTripPartner();
	if (TripPartner)
	{
		Params.AddIgnoredActor(TripPartner);
	}

	const FVector Start = GetActorLocation();

	FVector Direction = GetVelocity();
	if (!Direction.Normalize())
	{
		// 버그 수정(사용자 지시) — 도착 후 제자리에서 돌아나가는 구간은 속도가 0이라 forward 벡터로
		// 대체됐는데, 회전이 진행되는 동안 forward가 계속 바뀌면서 트레이스 박스가 옆칸을 훑고 지나가며
		// 오탐을 냈다. 목표 지점 방향으로 고정해 회전 중에도 트레이스가 실제 진행 경로만 보게 한다.
		Direction = (PendingFinalHopTarget - Start);
		if (!Direction.Normalize())
		{
			Direction = GetActorForwardVector();
		}
	}

	const FVector BoxCenter = Start + Direction * (FinalHopTraceDistance * 0.5f);
	const FQuat BoxRotation = Direction.ToOrientationQuat();
	const FCollisionShape Box = FCollisionShape::MakeBox(FVector(FinalHopTraceDistance * 0.5f, FinalHopTraceHalfWidth, 50.f));

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByChannel(Overlaps, BoxCenter, BoxRotation, ECC_Pawn, Box, Params);

	AFactoryAgentBase* DetectedAgent = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (AFactoryAgentBase* HitAgent = Cast<AFactoryAgentBase>(Overlap.GetActor()))
		{
			const float DistSq = FVector::DistSquared(Start, HitAgent->GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				DetectedAgent = HitAgent;
			}
		}
	}

	if (!DetectedAgent)
	{
		if (CurrentState == EAgentState::Pause)
		{
			ResumeFromPause(AIController, TEXT("FinalHop 정면 클리어"));
		}
		return;
	}

	PublishSafetyDetectionEvent(DetectedAgent, FMath::Sqrt(BestDistSq));

	// 버그 수정(사용자 지시) — Broken은 선반 접근이면 다른 칸 재할당이 대기보다 합리적일 수 있다(선반은
	// 칸이 여러 개, 정비 중인 NPC가 하필 인접 칸 접근을 막는 경우). 재할당에 성공했으면(true) 이 아래
	// 일반 Pause는 건너뛴다 — 이미 배정을 놓고 새 배정으로 넘어갔으므로 Pause 걸 대상 자체가 없다.
	if (DetectedAgent->CurrentState == EAgentState::Broken && TryHandleFinalHopBrokenBlock(DetectedAgent))
	{
		return;
	}

	// 버그 수정(사용자 지시) — FinalHop은 접근 각도를 바꿀 여지가 거의 없는 짧은 구간이라 그래프
	// 구간과 달리 타입 구분 없이 무조건 Pause만 한다(새 목적지 탐색 없음).
	if (CurrentState != EAgentState::Pause)
	{
		UE_LOG(LogFactoryDispatch, Log, TEXT("[Safety] %s FinalHop 정면에서 %s(상태=%s) 감지 — Pause 전환"),
			*GetName(), *DetectedAgent->GetName(), *UEnum::GetValueAsString(DetectedAgent->CurrentState));
		SetState(EAgentState::Pause);
		if (UPathFollowingComponent* PathFollowing = AIController->GetPathFollowingComponent())
		{
			PathFollowing->PauseMove();
		}
	}
}

void AFactoryAgentBase::PublishSafetyDetectionEvent(const AFactoryAgentBase* DetectedAgent, float Distance) const
{
	if (!DetectedAgent)
	{
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UFactoryEventBusSubsystem* EventBus = GI ? GI->GetSubsystem<UFactoryEventBusSubsystem>() : nullptr;
	if (!EventBus)
	{
		return;
	}

	FAnomalyEvent Event;
	Event.Timestamp = FDateTime::UtcNow();
	Event.LogID = FGuid::NewGuid();
	Event.Severity = EEventSeverity::Warning;
	Event.ActorID = AgentID;
	Event.ActorType = AgentType;
	Event.AnomalyCode = TEXT("Code:002");
	Event.Location = GetActorLocation();
	Event.TargetLocation = DetectedAgent->GetActorLocation();
	Event.NearestObstacleDistance = Distance;
	Event.bSafetyZoneStatus = true;

	switch (DetectedAgent->AgentType)
	{
	case EActorType::AtlasRobot:
		Event.InterrupterType = EInterrupterType::AtlasRobot;
		break;
	case EActorType::TransportRobot:
		Event.InterrupterType = EInterrupterType::TransportRobot;
		break;
	case EActorType::NPCHuman:
		Event.InterrupterType = EInterrupterType::NPC;
		break;
	default:
		break;
	}

	EventBus->PublishAnomaly(Event);
}
