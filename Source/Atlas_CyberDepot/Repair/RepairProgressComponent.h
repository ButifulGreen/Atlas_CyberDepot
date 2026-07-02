// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Agent/RepairTypes.h"
#include "RepairProgressComponent.generated.h"

class AFactoryAgentBase;

// Docs/05_Repair.md §5 — 7단계 대상. AFactoryAtlasRobot/TransportRobot에 부착.
UCLASS(ClassGroup = (Repair), meta = (BlueprintSpawnableComponent))
class URepairProgressComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URepairProgressComponent();

	UPROPERTY(BlueprintReadOnly)
	ERepairType CurrentRepairType = ERepairType::QuickCheck;

	UPROPERTY(Replicated, BlueprintReadOnly)
	float RepairProgress = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Repair")
	float QuickCheckDurationSeconds = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Repair")
	float FullRepairDurationSeconds = 15.f;

	// 진행 속도 = BaseRepairRate * 유효 ActiveRepairers 수 (문서에 공식만 있고 값은 없어 여기서 노출)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Repair")
	float BaseRepairRate = 1.f;

	void Server_JoinRepair(AFactoryAgentBase* Repairer);
	void Server_LeaveRepair(AFactoryAgentBase* Repairer);
	void TickRepairProgress(float DeltaTime);
	void OnRepairCompleted();
	int32 GetValidRepairerCount() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// 플레이어 빙의 NPC와 AI NPC를 동일하게 취급 — 유효한 참조만 카운트
	UPROPERTY()
	TArray<TWeakObjectPtr<AFactoryAgentBase>> ActiveRepairers;

	float GetTargetDurationSeconds() const;
};
