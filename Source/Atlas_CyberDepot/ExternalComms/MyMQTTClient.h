// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyMQTTClient.generated.h"

struct FAnomalyEvent;
struct FStateSnapshot;
struct FTaskLifecycleEvent;
class FMQTTWrapper;

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

// Docs/11_MQTT.md §11 — Eclipse Paho MQTT C 비동기 클라이언트(PahoMQTT 서드파티 모듈, MQTTAsync)로 연동한다.
// 라이브러리 파일이 Source/ThirdParty/PahoMQTT에 배치되기 전까지는 컴파일이 되지 않는다(Docs/14_OpenIssues.md).
UCLASS()
class AMyMQTTClient : public AActor
{
	GENERATED_BODY()

public:
	AMyMQTTClient();
	virtual ~AMyMQTTClient();

	// TUniquePtr<FMQTTWrapper>(불완전 타입) pImpl 멤버 때문에, UHT가 생성하는 VTable 헬퍼 생성자를
	// 암시적으로 합성하게 두면 FMQTTWrapper가 미완성인 지점에서 소멸자 코드를 생성하려다 실패한다.
	// 명시적으로 선언/정의(.cpp에서 Wrapper를 건드리지 않는 얕은 패스스루)해 이를 피한다.
	AMyMQTTClient(FVTableHelper& Helper);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MQTT|Connection")
	FString BrokerHost;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MQTT|Connection")
	int32 Port = 8883;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MQTT|Connection")
	FString ClientID;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MQTT|Connection")
	FString Username;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MQTT|Connection")
	FString Password;

	// 발행 실패(미연결) 시 적재되는 로컬 큐 — 이미 쓰는 MQTT 클라이언트(Paho) 위에 얹는 애플리케이션 레벨 배열
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

	// Paho MQTT C 콜백(백그라운드 스레드)에서 게임스레드로 넘어온 뒤 호출된다 — 직접 호출 대상 아님
	void HandleConnectionSuccess();
	void HandleConnectionFailure();
	void HandleConnectionLost(const FString& Cause);
	void HandleMessageArrived(const FString& Topic, const FString& Payload);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool TryPublish(const FString& Topic, const FString& Payload);
	void SubscribeToKioskOrderTopic();

	bool bIsConnected = false;

	// MQTTAsync.h를 이 헤더에 노출하지 않기 위한 pImpl
	TUniquePtr<FMQTTWrapper> Wrapper;
};
