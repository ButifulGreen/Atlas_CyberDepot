// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "EventBus/FactoryEventTypes.h"
#include "LogisticsItem.generated.h"

class AStorageShelf;
class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;

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
	UPROPERTY(ReplicatedUsing = OnRep_ItemType, BlueprintReadOnly)
	EItemType ItemType = EItemType::ItemA;

	UPROPERTY(BlueprintReadOnly)
	FDateTime CreatedTimestamp;

	float GetAgeSeconds() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	// 서버가 스폰 직후 ItemType을 바꾸는 경우, OnRep은 클라이언트에서만 자동 호출되므로
	// AFactoryAgentBase::SetState 사례처럼 서버 쪽에서는 UpdateItemMesh를 직접 호출해야 한다.
	UFUNCTION()
	void OnRep_ItemType();

private:
	void UpdateItemMesh();

	UPROPERTY(VisibleAnywhere, Category = "Mesh")
	TObjectPtr<USceneComponent> ItemRoot;

	// 3종 고정 — 실제 메시 에셋은 BP_LogisticsItem에서 각 컴포넌트에 지정, ItemType에 맞는 것 하나만 보이게 전환
	UPROPERTY(VisibleAnywhere, Category = "Mesh")
	TObjectPtr<UStaticMeshComponent> MeshItemA;

	UPROPERTY(VisibleAnywhere, Category = "Mesh")
	TObjectPtr<UStaticMeshComponent> MeshItemB;

	UPROPERTY(VisibleAnywhere, Category = "Mesh")
	TObjectPtr<UStaticMeshComponent> MeshItemC;
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

	// Docs 이탈, 승인됨 — Docs/03_InventoryOrder.md 금액 산정 시스템. 플레이어 입고 주문 1개당 비용.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Economy")
	int32 UnitPrice = 10;

	// 구매가와 별도로 책정(마진) — 외부업체 출고 주문 수락 시 이 품목 1개당 수익.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Economy")
	int32 SellPrice = 15;
};
