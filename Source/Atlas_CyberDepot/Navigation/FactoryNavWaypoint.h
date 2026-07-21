// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EventBus/FactoryEventTypes.h"
#include "FactoryNavWaypoint.generated.h"

class AFactoryAgentBase;

// 버그 수정 — 원래 TransportRobotInbound/TransportRobotOutbound라는 이름 자체에 "배송로봇 전용"이
// 박혀 있었다. 이제 이름은 방향성(진입/이탈)만 나타내고, 실제로 어떤 에이전트가 쓸 수 있는지는
// AFactoryNavWaypoint::AllowedAgentTypes(비트마스크)로 분리한다.
UENUM(BlueprintType)
enum class EWaypointAccess : uint8
{
	Common,
	Inbound,
	Outbound,
	// 버그 수정(사용자 지시) — Inbound와 마커 사이에 두는 게이트 노드. 도착해도 곧장 FinalHop으로
	// 넘어가지 않고, AFactoryAgentBase::CanProceedFromWaitbound()가 허가할 때까지 예약을 쥔 채 대기한다
	// (기본은 항상 허가=게이팅 없음 — 배송로봇만 override해서 짝 아틀라스의 도착 여부를 확인한다).
	Waitbound,
};

// Docs/08_Navigation.md — 아틀라스/배송로봇 웨이포인트 그래프의 노드 1개. 특정 인프라(선반/트레이)를
// 모른다 — 그 반대로 AStorageShelf/AHorizontalTray/AIdleWaitingZone이 자신의 도킹 웨이포인트를 참조한다.
UCLASS()
class AFactoryNavWaypoint : public AActor
{
	GENERATED_BODY()

public:
	AFactoryNavWaypoint();

	// 버그 수정(사용자 지시) — 하나의 웨이포인트가 여러 역할을 동시에 겸할 수 있어야 해서(특히 Inbound+
	// Outbound 동시 지정) AllowedAgentTypes와 동일한 비트마스크 패턴으로 전환했다. 프로퍼티 이름을
	// 기존 Access에서 AccessFlags로 바꿨다 — 언리얼 프로퍼티 시스템은 타입이 바뀌면(enum→int32) 기존에
	// 저장된 인스턴스 값을 이어받지 못하므로, 이미 레벨에서 Access를 체크해둔 웨이포인트는 이 변경 이후
	// AccessFlags를 다시 체크해야 한다(값이 조용히 잘못 해석되는 대신 깨끗하게 기본값(Common)으로 빠짐).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (Bitmask, BitmaskEnum = "/Script/Atlas_CyberDepot.EWaypointAccess"))
	int32 AccessFlags = (1 << static_cast<int32>(EWaypointAccess::Common));

	bool HasAccessFlag(EWaypointAccess Flag) const;
	// Inbound/Outbound/Waitbound 중 어느 것도 겸하지 않는(순수 공용 백본) 노드인지 — 경로 탐색의
	// "전용 레인 우선" 정렬 판단에 쓰인다(FactoryWaypointNavigationSubsystem 참고).
	bool IsPureCommon() const;

	// 버그 수정(사용자 지시) — IsPureCommon() 기반 2단계 정렬은 Inbound/Outbound를 동일하게(둘 다
	// "전용 레인") 취급해서, 마커에서 이탈(출발)할 때도 도착용 Inbound가 더 가까우면 그쪽을 시작점으로
	// 잘못 고를 수 있었다(확정 재현 — 배송로봇이 가까운 Outbound 대신 Inbound/Common으로 이탈). 목적
	// (bWantOutbound=출발지 탐색이면 true, 도착지 탐색이면 false)에 맞는 방향을 0순위, 그 외 전용 레인을
	// 1순위, 순수 Common을 2순위로 매긴다 — Inbound+Outbound 동시 지정 노드는 어느 목적이든 0순위.
	int32 GetDirectionalTier(bool bWantOutbound) const;

	// 버그 수정 — 하나만 켜도, 아틀라스/배송로봇/NPC 전부 켜도 되도록 비트마스크로 분리. 기본값은
	// 전부 허용(기존 Common의 "전부 허용" 동작과 동일) — 기존에 배치된 Inbound/Outbound 웨이포인트가
	// 있다면(예전엔 배송로봇 전용으로 하드코딩) 이 값이 새로 생기면서 기본값(전부 허용)을 물려받으니,
	// 배송로봇 전용으로 계속 남기고 싶은 지점은 에디터에서 직접 체크를 해제해야 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (Bitmask, BitmaskEnum = "/Script/Atlas_CyberDepot.EActorType"))
	int32 AllowedAgentTypes = 7;

	// 방향성은 이 배열 자체가 편도/양방향인지로 표현한다 — 레인 노드(파랑/빨강)는 다음 노드만
	// 편도로 연결하고, 백본(Common)은 서로 왕복 연결한다. 레벨에서 직접 배선(자동 탐지하지 않음).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<TObjectPtr<AFactoryNavWaypoint>> ConnectedWaypoints;

	// Docs에 없는 구현값 — 디버그 시각화(스피어+방향 화살표) 갱신 간격.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug")
	float DebugDrawIntervalSeconds = 1.f;

	bool TryReserve(AFactoryAgentBase* Agent);
	void Release(AFactoryAgentBase* Agent);
	bool IsOccupied() const;
	bool IsOccupiedBy(const AFactoryAgentBase* Agent) const;

	// AgentType이 이 웨이포인트를 경로 탐색 후보로 쓸 수 있는지. Common은 전부 허용,
	// TransportRobotInbound/Outbound는 EActorType::TransportRobot만 허용.
	bool IsUsableBy(EActorType AgentType) const;

protected:
	virtual void BeginPlay() override;

private:
	// Access(검은/파랑/빨강)와 점유 상태에 따라 색을 바꿔 스피어+연결선을 그린다 —
	// FactoryAgentBase::DrawDebugOperationCountLabel과 동일한 타이머 패턴.
	void DrawDebugVisualization();

	FTimerHandle DebugDrawTimerHandle;

	UPROPERTY()
	TWeakObjectPtr<AFactoryAgentBase> Occupant;
};
