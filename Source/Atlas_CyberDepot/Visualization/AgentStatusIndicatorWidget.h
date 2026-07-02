// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EventBus/FactoryEventTypes.h"
#include "AgentStatusIndicatorWidget.generated.h"

class AFactoryAgentBase;

// Docs/09_Visualization.md §9 — 9단계. 에이전트 머리 위에 부착하는 위젯(WidgetComponent 배치는 에디터 작업).
UCLASS()
class UAgentStatusIndicatorWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	EAgentState DisplayedState = EAgentState::Idle;

	UPROPERTY(BlueprintReadOnly)
	float RepairProgressDisplay = 0.f;

	void BindToAgent(AFactoryAgentBase* Agent);

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION(BlueprintImplementableEvent, Category = "AgentStatus")
	void BP_OnDisplayedStateChanged(EAgentState NewState);

private:
	UPROPERTY()
	TWeakObjectPtr<AFactoryAgentBase> BoundAgent;
};
