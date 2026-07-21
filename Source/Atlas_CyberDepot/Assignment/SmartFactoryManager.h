// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Agent/RepairTypes.h"
#include "EventBus/FactoryEventTypes.h"
#include "Assignment/DeliveryOrderSubsystem.h"
#include "SmartFactoryManager.generated.h"

class AFactoryAgentBase;
class AFactoryNPCHuman;
class UDataTable;
struct FItemTypeDefinition;

DECLARE_MULTICAST_DELEGATE(FOnVendorOrdersUpdated);

// Docs/03_InventoryOrder.md §3 — 6단계 대상.
UCLASS()
class AMSmartFactoryManager : public AGameStateBase
{
	GENERATED_BODY()

public:
	UPROPERTY(Replicated, BlueprintReadOnly)
	float ReputationScore = 0.f;

	// Docs 이탈, 승인됨 — 플레이어 물품 주문/외부업체 주문 수락 양쪽이 공유하는 공용 자금.
	// 시작값은 클래스 디폴트에서 조정(Balance).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Balance|Economy")
	float SharedFunds = 1000.f;

	// Docs 이탈, 승인됨 — UDeliveryOrderSubsystem::ActiveOrders의 리플리케이트 표시 전용 사본.
	// UpdateVendorOrderDisplays가 채우고, 클라이언트에서는 OnRep_VendorOrderDisplays로 UI 갱신을 트리거한다.
	UPROPERTY(ReplicatedUsing = OnRep_VendorOrderDisplays, BlueprintReadOnly)
	TArray<FVendorOrderDisplay> VendorOrderDisplays;

	// UVendorOrderListWidget이 구독 — 서버/클라이언트 양쪽에서 VendorOrderDisplays가 바뀔 때마다 발생.
	FOnVendorOrdersUpdated OnVendorOrdersUpdated;

	// Docs 이탈, 승인됨 — 원래 UInventoryOrderSubsystem/UDeliveryOrderSubsystem(둘 다 UWorldSubsystem)에
	// EditAnywhere로 있었으나, 엔진이 서브시스템 상속 계층의 concrete 클래스마다 별도 인스턴스를 만들어
	// GetSubsystem<T>()가 항상 네이티브 인스턴스를 반환하는 구조라 BP 서브클래스에서 값을 바꿔도 실제
	// 게임 로직에 반영되지 않는 문제가 있었다(SubsystemCollection.cpp 확인). AGameStateBase는 BP 서브클래스가
	// 실제로 그대로 스폰되는 단일 인스턴스라 SharedFunds와 동일한 방식으로 여기로 옮겨 확실히 동작시킨다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Balance|Economy")
	TObjectPtr<UDataTable> ItemPriceTable;

	// 전역 단일 재주문 쿨다운("차량 연계" 이전까지의 임시 제약, Docs/14_OpenIssues.md 참고).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Balance|Economy")
	float ReorderCooldownSeconds = 30.f;

	// 외부업체 랜덤 주문. 배열 크기가 곧 업체 수(요구사항은 5, 나중에 조정 가능).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Balance|Economy")
	TArray<FName> VendorNames = { TEXT("Vendor A"), TEXT("Vendor B"), TEXT("Vendor C"), TEXT("Vendor D"), TEXT("Vendor E") };

	// 업체마다 다음 주문까지 대기하는 랜덤 간격의 최소/최대(초).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Balance|Economy")
	float MinOrderIntervalSeconds = 30.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Balance|Economy")
	float MaxOrderIntervalSeconds = 90.f;

	// 한 번 생성될 때 품목(A/B/C)당 랜덤 수량 범위. 0 포함 — 이번 라운드에 그 품목은 요청 안 함을 표현.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Balance|Economy")
	int32 MinQuantityPerItem = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Balance|Economy")
	int32 MaxQuantityPerItem = 10;

	// ItemPriceTable이 비어있거나 해당 EItemType 행을 못 찾으면 0을 반환한다(주문은 막지 않되 금액만 0으로 처리).
	int32 GetUnitPrice(EItemType ItemType) const;
	int32 GetSellPrice(EItemType ItemType) const;

	// 음수 조정 시 잔액이 부족하면(SharedFunds + Delta < 0) 적용하지 않고 false 반환.
	bool TryAdjustFunds(float Delta, FName Reason);
	// UDeliveryOrderSubsystem이 ActiveOrders 변경 시마다 호출(주문 생성/수락 등).
	void UpdateVendorOrderDisplays(const TArray<FDeliveryOrder>& ActiveOrders);

	// 5단계 신규 — 이번 세션에서 EItemType별로 몇 번 메시 컴포넌트(0=MeshItemA/1=B/2=C)를 쓸지의 매핑.
	// 서버가 BeginPlay에서 한 번만 셔플해서 정하고, 판이 끝날 때까지 유지된다(ALogisticsItem::UpdateItemMesh가 참조).
	UPROPERTY(Replicated, BlueprintReadOnly)
	TArray<uint8> ItemTypeToMeshSlot;

	void AdjustReputation(float Delta, FName Reason);
	void RequestMaintenance(AFactoryAgentBase* Agent, ERepairType RepairType);
	void OnAgentBecameIdle(AFactoryAgentBase* Agent);
	void OnRepairCompleted(AFactoryAgentBase* Agent);
	AFactoryNPCHuman* FindNearestAvailableNPC(const FVector& Location) const;

	// Docs에 없는 구현값 — 정비 사이클(NPC 접근+수리) 단독 테스트용. 레벨의 Idle 상태 Atlas/TransportRobot
	// 중 아무거나 하나를 골라 실제 고장 처리(TriggerBreakdown)를 강제 호출한다. Level BP에서 키 바인딩으로 호출.
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void DebugForceRandomBreakdown();

	// 버그 수정 — RequestMaintenance 호출 시점에 가용 NPC가 없으면 PendingMaintenanceQueue에 쌓아두고
	// 조용히 리턴한다(재시도가 없어 배정자 없이 영구히 Broken으로 방치되던 문제). URepairProgressComponent::
	// OnRepairCompleted가 NPC를 사무실로 돌려보내기 전에 이 함수로 먼저 대기열을 확인한다.
	// 큐에서 아직 유효한(여전히 Broken이고 정비자가 없는) 항목을 찾으면 NPC를 배정하고 true를 반환한다.
	bool TryAssignNextPendingMaintenance(AFactoryNPCHuman* NPC);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_VendorOrderDisplays();

private:
	const FItemTypeDefinition* FindItemDefinition(EItemType ItemType) const;

	UPROPERTY()
	TArray<TWeakObjectPtr<AFactoryAgentBase>> PendingMaintenanceQueue;
};
