// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Components/SceneComponent.h"
#include "EventBus/FactoryEventTypes.h"
#include "FactoryAgentBase.generated.h"

class URepairProgressComponent;
class AIdleWaitingZone;
class AFactoryNavWaypoint;
class UFactoryWaypointNavigationSubsystem;

// Docs/08_Navigation.md — 그래프 경유 중 어느 구간인지 구분(Blueprint/리플리케이션 불필요라 순수 C++ enum).
enum class EWaypointTravelPhase : uint8
{
	None,
	TraversingGraph,
	FinalHop,
};

// Docs/08_Navigation.md — 안전거리 라인트레이스 시작점+방향 마커. UStorageSlotMarkerComponent와 동일한
// 패턴으로 뷰포트에서 위치/회전을 직접 드래그해 배치한다(컴포넌트의 전방 벡터 = 트레이스 방향).
// 로봇 블루프린트마다 원하는 개수만큼 자유롭게 추가·배치할 수 있다.
UCLASS(ClassGroup = (Agent), meta = (BlueprintSpawnableComponent))
class USafetyTraceMarkerComponent : public USceneComponent
{
	GENERATED_BODY()
};

// Docs/04_Agent_AI.md §4 — 아틀라스/운송로봇/NPC 공용 베이스. 2단계(스켈레톤) 대상.
UCLASS()
class AFactoryAgentBase : public ACharacter
{
	GENERATED_BODY()

public:
	AFactoryAgentBase();

	static constexpr float BlockedThresholdSeconds = 2.f;

	UPROPERTY(Replicated, BlueprintReadOnly)
	FGuid AgentID;

	// Docs/01_EventBus_DataPipeline.md의 EActorType과 04_Agent_AI.md의 EAgentType이 동일한 값 구성이라 하나로 통합
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	EActorType AgentType = EActorType::AtlasRobot;

	// 디버깅 편의 — VisibleAnywhere 없이 BlueprintReadOnly만으로는 디테일 패널에 안 뜬다.
	UPROPERTY(VisibleAnywhere, ReplicatedUsing = OnRep_CurrentState, BlueprintReadOnly)
	EAgentState CurrentState = EAgentState::Idle;

	UPROPERTY(BlueprintReadOnly)
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float BlockedTimer = 0.f;

	// 버그 수정(사용자 요청, 대비책) — 그래프 구간/FinalHop 안전 트레이스 둘 다 AFactoryAgentBase 파생
	// 액터만 감지 대상이라, 선반 같은 정적 지오메트리에 막히면 Pause조차 안 걸리고 영구 정지할 수 있다
	// (원인 특정 전 최후의 안전망 — Tick 참고). CurrentState==Moving인데 BlockedThresholdSeconds 넘게
	// 안 움직이면 이 간격으로 원인을 가리지 않고 강제 재탐색을 반복 시도한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Navigation")
	float BlockedRecoveryRetryIntervalSeconds = 2.f;

	// Docs/04_Agent_AI.md IsEligibleForQuickCheck()이 참조하지만 어디에도 멤버로 정의돼 있지 않아
	// 5단계에서 추가 — AIdleWaitingZone::TryReserveSlot/ReleaseSlot이 갱신한다.
	UPROPERTY(BlueprintReadOnly)
	bool bIsParkedInIdleZone = false;

	// 7단계 후속 — 선입선출 없이 실행 시 1회만 고정 배정되는 홈 대기실/슬롯. UOutboundDispatchSubsystem::
	// AssignHomeIdleZoneSlots가 레벨 시작 시 채운 뒤로는 세션 내내 그대로 유지된다(리플리케이트 안 함 —
	// CurrentAssignment와 동일하게 서버 전용 부기 값).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TWeakObjectPtr<AIdleWaitingZone> HomeIdleZone;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 HomeSlotIndex = -1;

	// 대기실로 향해 이동하는 동안만 true(도착하면 즉시 false로 내려감). bIsParkedInIdleZone("지금 앉아있음")과
	// 달리 "지금 그리로 가는 중"만을 뜻해, OnArrivedAtDestination이 이 도착이 대기실행인지 판별하는 용도.
	UPROPERTY(BlueprintReadOnly)
	bool bIsHeadingToIdleZone = false;

	virtual void SetState(EAgentState NewState);
	FStateSnapshot ToSnapshot() const;

	// 7단계 후속 — AIdleWaitingZone::AssignHomeSlots가 호출해 HomeIdleZone/HomeSlotIndex를 채운다.
	void AssignHomeIdleZoneSlot(AIdleWaitingZone* Zone, int32 SlotIndex);
	// 7단계 신규 — 유휴 상태(할 일 없음)면 항상 대기실로 향한다는 규칙의 공용 구현.
	// Atlas/TransportRobot 양쪽에서 동일하게 쓰여 베이스에 한 번만 둔다. 자신의 고정 홈 슬롯으로만 이동하므로
	// 검색/경합이 없다 — 이미 향하는 중이거나 파킹돼 있으면 즉시 true, 홈이 배정 안 됐으면 false.
	bool TryHeadToIdleZone();
	// OnArrivedAtDestination 맨 앞에서 호출 — 대기실로 향하던 중이었으면 Idle로 전환하고 true(호출부는 이후 로직을 건너뛴다).
	bool TryHandleIdleZoneArrival();
	// 파킹 중이던 로봇이 새 작업을 받을 때(AcceptStationAssignment/AcceptTransportTask 맨 앞) 호출. 파킹 중이 아니면 no-op.
	void LeaveIdleZoneIfParked();

	// Docs/08_Navigation.md — 웨이포인트 그래프를 경유해 TargetWaypoint까지 이동한 뒤, 그래프를 벗어나
	// FinalHopTarget까지 짧게 직진 이동한다. Atlas/TransportRobot 양쪽이 목적지(선반/트레이/대기실)
	// 진입 시 공용으로 쓴다. 버그 수정 — TargetWaypoint를 nullptr로 넘기면, 슬롯/트레이마다 사람이 미리
	// 지정해둔 고정 도킹 참조 대신 FinalHopTarget(마커 좌표)에 가장 가깝고 실제로 도달 가능한 웨이포인트를
	// 매번 동적으로 찾는다(호출부가 구체적인 도킹 웨이포인트를 알고 있을 때만 명시적으로 넘기면 됨).
	bool TryRequestWaypointRoute(AFactoryNavWaypoint* TargetWaypoint, const FVector& FinalHopTarget);

	// 버그 수정 — 경로 탐색/첫 홉 예약이 경합으로 실패해도 재시도가 전혀 없어 로봇이 영구 정지하는 문제가
	// 있었다. 이 간격으로 같은 요청(TargetWaypoint/FinalHopTarget)을 자동 재시도한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Navigation")
	float WaypointRetryIntervalSeconds = 1.f;

	// 버그 수정 — 웨이포인트 경로를 따라 이동 중(TravelPhase==TraversingGraph)일 때 새 이동/배정 요청이
	// 끼어들면, 그 시점에 예약해 쥐고 있던 노드를 반납하지 않고 그냥 내부 상태를 덮어써서 그 노드가
	// 영구히 "점유됨"으로 남아 그 노드를 지나야 하는 모든 다른 로봇의 경로 탐색이 막히는 광범위한
	// 그리드락으로 번졌다. TryRequestWaypointRoute 진입 시, 그리고 AcceptStationAssignment/
	// AcceptTransportTask처럼 완전히 새로운 의도로 전환되는 지점에서 호출해 먼저 정리한다.
	void AbandonAnyActiveWaypointRoute();

	// OnArrivedAtDestination 맨 앞에서(TryHandleIdleZoneArrival보다도 먼저) 호출.
	// 경로 중간/최종 홉 이동 중이면 다음 단계를 알아서 진행하고 true 반환(호출부는 아무것도 안 함).
	// 웨이포인트 경로가 아예 없었거나 최종 홉까지 전부 끝났으면 false — 호출부가 평소처럼 처리.
	bool TryHandleWaypointRouteArrival();
	// 안전거리 감지가 전방에서 고장 로봇을 발견했을 때 호출. 현재 홉 예약을 반납하고
	// 현재 위치 기준으로 같은 최종 목적지를 다시 탐색한다(막힌 노드는 점유 중이라 자동 제외됨).
	void AbandonWaypointRouteAndReroute();

	// Docs/08_Navigation.md — 안전거리 감지. 배치된 USafetyTraceMarkerComponent 각각의 위치/전방
	// 벡터를 시작점/방향으로 써서 라인트레이스한다. 예약이 항상 완벽하지 않은 현실을 반영하는
	// 반응형 안전망 레이어. 마커를 하나도 안 배치했으면(과도기) 정면 1방향으로 대체한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Safety")
	float SafetyTraceIntervalSeconds = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Safety")
	float SafeDistanceUnits = 300.f;

	// 버그 수정(사용자 지시, 회피 재설계) — 예전엔 상대가 Working/Idle(의도된 정지)이면 회피를 전적으로
	// 엔진 Detour Crowd(RVO)에 위임했는데, RVO는 국소 반응형이라 좁은 틈에서 로컬 미니멈(제자리 흔들림)에
	// 빠질 수 있었다(실제 재현). 이제 그래프 구간은 감지 즉시 판단한다 — Broken이거나 나와 다른
	// EActorType이면서 Moving이면 AbandonWaypointRouteAndReroute로 즉시 재탐색, 그 외(같은 타입은 상대
	// 상태 무관, 다른 타입이라도 정지 상태)는 EAgentState::Pause로 전환해 대기+재확인한다. 후진/우회
	// 지점 탐색처럼 새 이동을 만들어내지 않는다 — 새 이동은 그 자체로 또 다른 실패 지점이었다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Safety")
	float PauseRerouteTimeoutSeconds = 5.f;

	// FinalHop(그래프를 벗어난 마지막 접근) 전용 면(박스) 트레이스 — 정지 로봇과 선반 사이처럼 좁은
	// 구간은 그래프 예약 확장(웨이포인트)만으로 전부 커버되지 않아 별도 판정을 둔다. FinalHop 진입 중
	// (아직 도착 전)에만 동작하고, 감지되면 Pause, 없어지면 재개, 도착하면 동작을 멈춘다 — 이 판정은
	// 진입하는 로봇에만 적용되고 이미 작업을 마치고 벗어나는 로봇(그래프 구간으로 전환됨)은 적용받지
	// 않는다. 그래프 구간과 달리 이 짧은 구간은 접근 각도를 바꿀 여지가 거의 없어 새 목적지 탐색 대신
	// 정지 후 재확인만 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Safety")
	float FinalHopTraceDistance = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Safety")
	float FinalHopTraceHalfWidth = 80.f;

	// 버그 수정 — 안전거리/FinalHop 트레이스 둘 다 "이번 트립에서 나와 핸드오프할 상대"까지 장애물로
	// 감지하면, 정작 만나야 할 상대를 스스로 감지해 접근을 멈춰버린다. 아틀라스/배송로봇이 각자 자기
	// 트립의 짝을 반환하도록 override해서 트레이스 대상에서 제외한다.
	virtual AFactoryAgentBase* GetCurrentTripPartner() const { return nullptr; }

	// 버그 수정(사용자 지시) — FinalHop 트레이스가 Broken 상대를 감지했을 때 기본은 Pause(수리 끝날 때까지
	// 대기)지만, 선반은 칸이 여러 개라 정비 중인 NPC가 접근을 막는 경우 다른 칸으로 재할당하는 게 더
	// 합리적이다(선반 접근 시에만 해당 — 트레이는 대안이 없어 기본 Pause가 맞다). 아틀라스가 override해
	// 처리하고, 실제로 재할당했으면 true(호출부는 기본 Pause를 건너뛴다). 기본 구현은 처리 안 함(false).
	virtual bool TryHandleFinalHopBrokenBlock(AFactoryAgentBase* BrokenAgent) { return false; }

	// 버그 수정(사용자 지시, Waitbound) — 그래프 마지막 노드가 EWaypointAccess::Waitbound면 도착해도
	// 곧장 FinalHop으로 넘어가지 않고 이 훅이 true를 반환할 때까지 예약을 쥔 채 대기한다(그동안
	// WaitboundRecheckIntervalSeconds 간격으로 재확인). 기본은 항상 통과(게이팅 없음) — 배송로봇만
	// override해서 짝 아틀라스가 자기 마커에 먼저 도착(Working)했는지 확인한다(선반 마커가 깊이축으로만
	// 떨어져 있어 배송로봇이 먼저 진입하면 뒤따르는 아틀라스가 물리적으로 가로막히는 문제의 해결책).
	virtual bool CanProceedFromWaitbound() const { return true; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Navigation")
	float WaitboundRecheckIntervalSeconds = 1.f;

	// AFactoryAIController::OnMoveCompleted가 이동 성공 시 호출. 아틀라스/운송로봇이 override해
	// 도착 후 행동(TransferItem 등)을 트리거한다. 6단계 오케스트레이션 레이어에서 신규 추가.
	virtual void OnArrivedAtDestination() {}

	// AFactoryAIController가 MaxMoveRetryAttempts를 전부 소진했을 때 호출(진짜 길찾기 불가 — 레벨
	// NavMesh/지오메트리 문제일 가능성이 높다). 베이스는 웨이포인트 경로 상태만 정리(점유 반납, 재시도
	// 타이머 취소)한다. Atlas/TransportRobot이 override해 자기 배정/트립까지 정리하고 Idle로 복귀한다.
	virtual void OnMoveFailedPermanently();

	// CurrentState==Working인 동안 Tick에서 주기적으로 호출(파트너 대기 재시도 등). 6단계 신규.
	virtual void OnWorkingTick(float DeltaTime) {}

	virtual void Tick(float DeltaTime) override;

	// AtlasRobot/TransportRobot가 각자 OperationCount/MaintenanceThreshold로 구현(override)한다.
	// AIdleWaitingZone·AMSmartFactoryManager처럼 구체 타입을 모르는 코드가 공용으로 판정할 수 있도록
	// 6단계에서 베이스에 추가.
	virtual bool IsMaintenanceDue() const { return false; }
	virtual float GetOperationRatio() const { return 0.f; }
	// Docs에 없는 구현값 — AIdleWaitingZone의 배치 정비 디스패치 판정(BatchMaintenanceOperationThreshold)이
	// 절대값(예: 100) 기준으로 필요해 추가. AtlasRobot/TransportRobot이 override.
	virtual int32 GetOperationCount() const { return 0; }
	virtual void ApplyRestDecay(int32 Amount) {}

	// URepairProgressComponent::OnRepairCompleted가 호출. 베이스 기본 구현은 배정/트립이 없는 에이전트용
	// (Idle 전환뿐). AtlasRobot/TransportRobot는 override해 CurrentAssignment/CurrentTask가 남아있으면
	// (자연 발생 고장은 항상 Working 도중 롤링되므로 보존돼 있음) Working으로 복귀시켜 기존 재시도 경로가
	// 이어받게 한다 — 00_DesignPrinciples.md의 "예약을 유지한 채 수리 후 재개" 원칙 실제 구현.
	virtual void ResumeAfterRepair() { SetState(EAgentState::Idle); }

	// AtlasRobot/TransportRobot가 자신의 RepairComponent를 반환하도록 override한다.
	// AFactoryNPCHuman::AssignMaintenance처럼 구체 타입을 모르는 코드가 접근할 수 있도록 6단계에 이어 추가.
	virtual URepairProgressComponent* GetRepairComponent() const { return nullptr; }

	// Docs에 없는 구현값 — 정비 사이클(NPC 접근+수리) 단독 테스트용 디버그 훅.
	// AtlasRobot/TransportRobot가 기존 EvaluateRotationOrContinue의 고장 처리 블록(TriggerBreakdown)을 그대로 재사용해 override한다.
	virtual void DebugForceBreakdown() {}

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	virtual void OnRep_CurrentState();

private:
	// Docs에 없는 구현값 — 정비 임계치 테스트용 디버그 표시. GetRepairComponent()가 있는(=Atlas/TransportRobot)
	// 에이전트만 캡슐 중심 80유닛 위에 1초마다 GetOperationCount()를 정수로 갱신 표시한다.
	void DrawDebugOperationCountLabel();

	FTimerHandle DebugOperationCountTimerHandle;

	// Tick의 BlockedRecoveryRetryIntervalSeconds 판정용 — 마지막으로 강제 재탐색을 시도했던 시점의
	// BlockedTimer 값(BlockedTimer 자체는 계속 누적되므로, 이 값과의 차이로 다음 재시도 시점을 판단).
	float LastBlockedRecoveryAttemptSeconds = 0.f;

	// 버그 수정(사용자 지시) — 그래프 중간 홉 예약이 경합으로 실패하면 예전엔 곧장 전체 재탐색을 했는데,
	// 그러면 지금 막힌 노드 하나 때문에 경로 전체를 다시 짜면서 엉뚱하게 먼 후보로 빠질 위험이 있었다
	// (실제 재현). 대신 원래 계획했던 같은 노드를 그대로 둔 채 이 자리에서 기다렸다가 같은 노드 예약만
	// 다시 시도한다 — 타임아웃 없이 무기한 대기(아래 플래그로 Tick의 BlockedTimer 안전망에서 제외).
	void RetryNextHopReservation();
	FTimerHandle NextHopRetryTimerHandle;
	// 버그 수정(사용자 지시) — Tick의 BlockedTimer 안전망은 "왜 정지했는지" 모르는 채로 2초 넘게
	// 안 움직이면 무조건 전체 재탐색을 강제한다. 이 대기(같은 노드가 풀리길 기다리는 중)는 스스로
	// 무기한 재시도하는 정상 동작인데, 그 안전망이 끼어들면 지금 막힌 노드 대신 엉뚱한 노드로 튀는
	// 문제가 재발한다(혼잡한 구간에서 실제 재현) — 이 대기 중에는 안전망이 개입하지 않도록 제외시킨다.
	bool bIsWaitingForNextHopReservation = false;

	UFactoryWaypointNavigationSubsystem* GetWaypointNavSubsystem() const;

	// 타이머 콜백 — TravelPhase에 따라 그래프 구간/FinalHop 판정 중 하나로 위임한다.
	void RunSafetyTraceCheck();
	// 그래프 구간(TraversingGraph) — 라인트레이스 + 타입 기반 분기(Broken/이종타입 Moving은 즉시 재탐색,
	// 그 외는 Pause) + 상호 교착 방지 우선순위 + Pause 장시간 지속 시 재탐색.
	void RunGraphSegmentTraceCheck(class AFactoryAIController* AIController);
	// FinalHop — 면(박스) 트레이스로 정면 감지, 감지되면 Pause만(새 이동 없음), 도착 시 자동 비활성.
	void RunFinalHopAreaTraceCheck(class AFactoryAIController* AIController);
	void EnterOrMaintainPause(class AFactoryAIController* AIController, const AFactoryAgentBase* DetectedAgent, float Distance);
	void ResumeFromPause(class AFactoryAIController* AIController, const TCHAR* Reason);
	void PublishSafetyDetectionEvent(const AFactoryAgentBase* DetectedAgent, float Distance) const;

	// WaypointRetryIntervalSeconds 후 같은 요청을 다시 시도.
	void RetryWaypointRoute();

	// WaitboundRecheckIntervalSeconds 후 CanProceedFromWaitbound()를 다시 확인 — 통과하면 예약을 반납하고
	// FinalHop으로 넘어가고, 아니면 다시 같은 간격으로 재예약한다.
	void RecheckWaitboundClearance();

	FTimerHandle SafetyTraceTimerHandle;
	FTimerHandle WaypointRetryTimerHandle;
	FTimerHandle WaitboundRecheckTimerHandle;
	// Waitbound 노드에서 대기 중인지 — true인 동안은 해당 노드 예약을 반납하지 않는다(다른 로봇 진입 방지).
	bool bAwaitingWaitboundClearance = false;
	// Pause 상태가 얼마나 지속됐는지(그래프 구간 재탐색 타임아웃 판정용) — Pause 진입/해제 시 리셋.
	float PauseAccumulatedSeconds = 0.f;
	// 버그 수정 — PendingRetryTargetWaypoint가 nullptr인 게 "마커 기준 동적 탐색 재시도 대기 중"이라는
	// 유효한 상태가 되면서, PendingRetryTargetWaypoint 자체의 null 여부로는 "재시도가 예약돼 있는지"를
	// 더 이상 판별할 수 없다. 이 플래그로 별도 판별한다.
	bool bHasPendingWaypointRetry = false;
	TWeakObjectPtr<AFactoryNavWaypoint> PendingRetryTargetWaypoint;
	FVector PendingRetryFinalHopTarget = FVector::ZeroVector;

	// BeginPlay에서 자식 컴포넌트 중 USafetyTraceMarkerComponent만 모아 캐싱(StorageShelf::SlotMarkers와 동일 패턴).
	UPROPERTY()
	TArray<TObjectPtr<USafetyTraceMarkerComponent>> SafetyTraceMarkers;

	// TryRequestWaypointRoute가 채우고 TryHandleWaypointRouteArrival/AbandonWaypointRouteAndReroute가 소비하는
	// 진행 중 경로 상태. 서버 전용 부기 값(리플리케이트 안 함) — CurrentAssignment와 동일한 근거.
	TArray<TWeakObjectPtr<AFactoryNavWaypoint>> PendingWaypointRoute;
	int32 WaypointRouteIndex = 0;
	FVector PendingFinalHopTarget = FVector::ZeroVector;
	EWaypointTravelPhase TravelPhase = EWaypointTravelPhase::None;
};
