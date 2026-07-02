// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "EventBus/FactoryEventTypes.h"
#include "LogisticsItem.generated.h"

class AStorageShelf;
class UStaticMesh;

// Docs/06_Infrastructure.md §6 — 4단계 대상.
UCLASS()
class ALogisticsItem : public AActor
{
	GENERATED_BODY()

public:
	ALogisticsItem();

	UPROPERTY(Replicated, BlueprintReadOnly)
	FGuid ItemID;

	// 클라이언트가 외형(메시)을 결정하는 데 필요해 Replicated로 지정
	UPROPERTY(Replicated, BlueprintReadOnly)
	EItemType ItemType = EItemType::ItemA;

	UPROPERTY(BlueprintReadOnly)
	FDateTime CreatedTimestamp;

	float GetAgeSeconds() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
};

USTRUCT(BlueprintType)
struct FItemTypeDefinition : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EItemType Type = EItemType::ItemA;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UStaticMesh> PreviewMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<AStorageShelf> BoundShelfClass;
};
