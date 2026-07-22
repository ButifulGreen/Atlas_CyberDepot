// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EventBus/FactoryEventTypes.h"
#include "FactoryKioskTerminal.generated.h"

class UBoxComponent;

// Docs/02_Multiplayer_RPC.md §2 — 8단계. 전용 플레이어가 아니라 누구나 접근해 쓰는 인게임 장비.
UENUM(BlueprintType)
enum class EOrderRequestType : uint8
{
	// 단일 품목(실물 키오스크/MQTT 경로 — Docs/11_MQTT.md::OnKioskOrderReceived)
	Inbound,
	// 금액 산정 시스템 신규 — 플레이어 주문 UI(UVendorOrderListWidget) 전용. A/B/C 수량을 한 번에 제출해
	// 쿨다운/자금 체크를 1회만 수행한다(Inbound를 품목별로 3번 호출하면 첫 품목이 쿨다운을 즉시 소모해
	// 나머지가 전부 실패하는 문제가 있었다 — Docs/03_InventoryOrder.md 참고).
	InboundBatch,
	OutboundApproval,
	// 8단계 — 예약됐지만 아직 로봇이 배정되지 않은 주문 취소도 같은 RPC 경로를 태우기 위해 추가
	Cancel
};

// Docs/02_Multiplayer_RPC.md §2 — 원안의 FName ItemType은 기존 EItemType enum 컨벤션과 불일치해 EItemType으로 교체.
// TargetOrderID는 원안에 없던 필드로, OutboundApproval/Cancel이 어느 FDeliveryOrder를 가리키는지 참조하기 위해 추가.
USTRUCT(BlueprintType)
struct FKioskOrderRequest
{
	GENERATED_BODY()

	// Inbound 전용
	UPROPERTY(BlueprintReadOnly)
	EItemType ItemType = EItemType::ItemA;

	// Inbound 전용
	UPROPERTY(BlueprintReadOnly)
	int32 Quantity = 0;

	// InboundBatch 전용
	UPROPERTY(BlueprintReadOnly)
	int32 QuantityA = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 QuantityB = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 QuantityC = 0;

	// OutboundApproval / Cancel 전용
	UPROPERTY(BlueprintReadOnly)
	FGuid TargetOrderID;

	UPROPERTY(BlueprintReadOnly)
	EOrderRequestType RequestType = EOrderRequestType::Inbound;
};

// Docs/02_Multiplayer_RPC.md §2 — 8단계. 레벨에 배치되는 정적 장비. 주문 데이터를 자체 보관하지 않고
// 인터렉트 감지 대상(콜리전)만 제공한다 — 실제 처리는 AFactoryPlayerController::Server_SubmitKioskOrder가
// 서버측 서브시스템(UInventoryOrderSubsystem/UDeliveryOrderSubsystem)을 호출해 수행한다.
UCLASS()
class AFactoryKioskTerminal : public AActor
{
	GENERATED_BODY()

public:
	AFactoryKioskTerminal();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Kiosk")
	TObjectPtr<UBoxComponent> InteractCollision;
};

// 8/9단계 공용 — 인게임 RPC(AFactoryPlayerController::Server_SubmitKioskOrder)와 외부 현실 키오스크
// 경로(AMyMQTTClient::OnKioskOrderReceived) 양쪽에서 동일한 처리 결과를 보장하기 위해 분리했다.
bool ApplyKioskOrderRequest(UWorld* World, const FKioskOrderRequest& Request);
