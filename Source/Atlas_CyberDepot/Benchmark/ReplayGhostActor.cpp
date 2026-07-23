// Copyright Epic Games, Inc. All Rights Reserved.

#include "Benchmark/ReplayGhostActor.h"
#include "Components/TextRenderComponent.h"

AReplayGhostActor::AReplayGhostActor()
{
	PrimaryActorTick.bCanEverTick = false;
	// 순수 로컬 시각화 전용 — 실제 게임 상태에 영향을 주지 않고 리플리케이트할 필요도 없다.
	bReplicates = false;
	SetReplicatingMovement(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	NameLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NameLabel"));
	NameLabel->SetupAttachment(Root);
	NameLabel->SetRelativeLocation(FVector(0.f, 0.f, 80.f));
	NameLabel->SetHorizontalAlignment(EHTA_Center);
	NameLabel->SetWorldSize(24.f);
	NameLabel->SetTextRenderColor(FColor::Cyan);
}

void AReplayGhostActor::UpdateFromSnapshot(const FStateSnapshot& Snapshot)
{
	SetActorLocationAndRotation(Snapshot.Location, Snapshot.Rotation);
	NameLabel->SetText(FText::FromString(Snapshot.DisplayName));
	BP_OnStateUpdated(Snapshot.CurrentState);
}
