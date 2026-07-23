// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ReplayControlWidget.generated.h"

class UReplayPlaybackSubsystem;

// Docs/10_Benchmark_Replay.md — 재생 중 일시정지/탐색(스크럽바)/배속 조절 UI. 실제 스크럽바/버튼 배치는
// UMG 디자이너에서 BlueprintImplementableEvent를 구현해 채운다(다른 위젯들과 동일한 C++/BP 역할 분리).
UCLASS()
class UReplayControlWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// BP가 만든 버튼/스크럽바가 호출 — 전부 UReplayPlaybackSubsystem으로 위임한다.
	// Play/Pause 버튼이 하나로 합쳐진 토글 아이콘이면 TogglePause()만 쓰면 되고, 재생/일시정지가 별도
	// 버튼 2개(사용자 설계)면 각 버튼이 Play()/Pause()를 직접 불러야 한다 — TogglePause()를 양쪽에 다
	// 걸면 이미 재생 중일 때 "재생" 버튼을 눌러도 멈춰버리는 등 의도와 다르게 동작한다.
	UFUNCTION(BlueprintCallable, Category = "Replay")
	void Play();

	UFUNCTION(BlueprintCallable, Category = "Replay")
	void Pause();

	UFUNCTION(BlueprintCallable, Category = "Replay")
	void TogglePause();

	UFUNCTION(BlueprintCallable, Category = "Replay")
	void SeekToTime(float TimeSeconds);

	// 되감기(역재생)는 지원하지 않는다(사용자 지시) — 0 이하 값은 거부.
	// 이름 주의 — UUserWidget이 이미 SetPlaybackSpeed(const UWidgetAnimation*, float)를 갖고 있어(위젯
	// 애니메이션 재생 속도) 같은 이름을 못 씀.
	UFUNCTION(BlueprintCallable, Category = "Replay")
	void SetReplayPlaybackSpeed(float Speed);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 매 틱 현재 재생 진행 상태를 BP로 밀어준다 — 스냅샷 발행 자체가 정지 구간에서 뜸해질 수 있어
	// (Docs/01_EventBus_DataPipeline.md), OnPlaybackFrame 이벤트가 아니라 매 틱 직접 값을 읽어야 스크럽바가
	// 그 사이 멈춰 보이지 않는다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Replay")
	void BP_OnPlaybackProgress(float CurrentSeconds, float TotalSeconds, bool bIsPlaying, float CurrentSpeed);

private:
	UPROPERTY()
	TObjectPtr<UReplayPlaybackSubsystem> PlaybackSubsystem;
};
