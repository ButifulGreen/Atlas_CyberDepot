// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EventBus/FactoryEventTypes.h"
#include "ReplayGhostActor.generated.h"

class UTextRenderComponent;

// Docs/10_Benchmark_Replay.md — UReplayVisualizationSubsystem이 스폰/갱신하는 순수 시각화 전용 대역.
// 실제 게임플레이 액터(AFactoryAgentBase 계열)와 완전히 분리 — AI/충돌/리플리케이션이 전혀 없는 껍데기다.
// 실제 메시/머티리얼/상태별 표현은 BP 서브클래스가 구현(KioskWidgetClass 등과 동일한 패턴 — 코드에서
// 특정 에셋을 강제하지 않음).
UCLASS()
class AReplayGhostActor : public AActor
{
	GENERATED_BODY()

public:
	AReplayGhostActor();

	// UReplayVisualizationSubsystem이 재생 프레임을 받을 때마다 호출 — Transform 갱신 + BP 훅 실행.
	void UpdateFromSnapshot(const FStateSnapshot& Snapshot);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Replay")
	TObjectPtr<USceneComponent> Root;

	// 사용자 지시 — 재생 중 실제 로봇의 이름표는 숨기고(AFactoryAgentBase::DrawDebugOperationCountLabel의
	// IsHidden() 조기 반환 참고), 대신 고스트에 같은 이름이 따라다녀야 한다. DrawDebugString이 아니라 컴포넌트를
	// 쓴 이유 — 재생 프레임은 AI 디스패치처럼 꾸준히 오지 않고(이동 없이 상태만 유지되는 구간은 훨씬 뜸하게
	// 옴) Duration 기반 디버그 문자열은 다음 프레임이 올 때까지 사라져버린다. 컴포넌트는 갱신 호출 없이도
	// 계속 붙어있어 그 문제가 없다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Replay")
	TObjectPtr<UTextRenderComponent> NameLabel;

	// 상태별 색상/애니메이션 등은 BP 서브클래스가 구현 — UVendorOrderListWidget의 BP_OnOrdersUpdated와
	// 동일한 패턴.
	UFUNCTION(BlueprintImplementableEvent, Category = "Replay")
	void BP_OnStateUpdated(EAgentState NewState);
};
