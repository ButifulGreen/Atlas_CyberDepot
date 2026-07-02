// Copyright Epic Games, Inc. All Rights Reserved.

#include "Visualization/AgentStatusIndicatorWidget.h"
#include "Agent/FactoryAgentBase.h"
#include "Repair/RepairProgressComponent.h"

void UAgentStatusIndicatorWidget::BindToAgent(AFactoryAgentBase* Agent)
{
	BoundAgent = Agent;

	if (Agent)
	{
		DisplayedState = Agent->CurrentState;
	}
}

void UAgentStatusIndicatorWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	AFactoryAgentBase* Agent = BoundAgent.Get();
	if (!Agent)
	{
		return;
	}

	if (Agent->CurrentState != DisplayedState)
	{
		DisplayedState = Agent->CurrentState;
		BP_OnDisplayedStateChanged(DisplayedState);
	}

	if (URepairProgressComponent* RepairComponent = Agent->GetRepairComponent())
	{
		RepairProgressDisplay = RepairComponent->RepairProgress;
	}
}
