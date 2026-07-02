// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyMQTTClient.generated.h"

struct FAnomalyEvent;
struct FStateSnapshot;
struct FTaskLifecycleEvent;

USTRUCT(BlueprintType)
struct FMQTTPendingMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString Topic;

	UPROPERTY(BlueprintReadOnly)
	FString Payload;

	UPROPERTY(BlueprintReadOnly)
	double QueuedTimestamp = 0.0;
};

// Docs/11_MQTT.md §11 — 9단계(외부 통신). 실제 브로커 연결은 MQTT 플러그인 설치 후 진행 예정(Docs/14_OpenIssues.md) —
// 이번 단계는 큐잉/JSON 직렬화/키오스크 수신 처리까지의 인터페이스와 구조만 구현한다.
UCLASS()
class AMyMQTTClient : public AActor
{
	GENERATED_BODY()

public:
	AMyMQTTClient();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MQTT|Connection")
	FString BrokerHost;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MQTT|Connection")
	int32 Port = 8883;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MQTT|Connection")
	FString ClientID;

	// 발행 실패(또는 미연결) 시 적재되는 로컬 큐 — 별도 외부 플러그인이 아니라 이미 쓰는 MQTT 클라이언트 위에 얹는 애플리케이션 레벨 배열
	UPROPERTY(BlueprintReadOnly)
	TArray<FMQTTPendingMessage> PendingPublishQueue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MQTT|Connection")
	int32 PublishQoSLevel = 1;

	void Connect();
	void PublishAnomalyEvent(const FAnomalyEvent& Event);
	void PublishSnapshot(const FStateSnapshot& Snapshot);
	void PublishTaskLifecycleEvent(const FTaskLifecycleEvent& Event);
	void FlushPendingQueue();
	void OnKioskOrderReceived(const FString& JsonPayload);

private:
	// 플러그인이 아직 없어 항상 false — Connect()가 실제로 세팅할 대상(9단계 후속)
	bool bIsConnected = false;

	void TryPublish(const FString& Topic, const FString& Payload);
};
