// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RepairTypes.generated.h"

// Docs/05_Repair.md §5에서 정의되지만 AFactoryNPCHuman::AssignMaintenance가 참조해서
// 5단계에서 함께 정의
UENUM(BlueprintType)
enum class ERepairType : uint8
{
	QuickCheck,
	FullRepair
};
