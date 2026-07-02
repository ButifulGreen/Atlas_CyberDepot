// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExternalComms/MyMQTTClient.h"
#include "EventBus/FactoryEventTypes.h"
#include "Infrastructure/FactoryKioskTerminal.h"
#include "JsonObjectConverter.h"

// Docs에 토픽 문자열이 명시돼 있지 않아 12_RaspberryPi.md의 버퍼 구분과 짝을 맞춰 여기서 정한다.
namespace MQTTTopics
{
	static const TCHAR* Anomaly = TEXT("atlas_cyberdepot/anomaly");
	static const TCHAR* Snapshot = TEXT("atlas_cyberdepot/snapshot");
	static const TCHAR* TaskLifecycle = TEXT("atlas_cyberdepot/task_lifecycle");
}

AMyMQTTClient::AMyMQTTClient()
{
}

void AMyMQTTClient::Connect()
{
	// TODO(9단계 후속): 실제 MQTT 플러그인으로 BrokerHost:Port에 연결하고 성공 시 bIsConnected = true.
	// 플러그인이 없는 지금은 항상 미연결 상태로 남고, 발행 요청은 전부 PendingPublishQueue에 적재된다.
	if (bIsConnected)
	{
		FlushPendingQueue();
	}
}

void AMyMQTTClient::PublishAnomalyEvent(const FAnomalyEvent& Event)
{
	FString Payload;
	if (FJsonObjectConverter::UStructToJsonObjectString(Event, Payload))
	{
		TryPublish(MQTTTopics::Anomaly, Payload);
	}
}

void AMyMQTTClient::PublishSnapshot(const FStateSnapshot& Snapshot)
{
	FString Payload;
	if (FJsonObjectConverter::UStructToJsonObjectString(Snapshot, Payload))
	{
		TryPublish(MQTTTopics::Snapshot, Payload);
	}
}

void AMyMQTTClient::PublishTaskLifecycleEvent(const FTaskLifecycleEvent& Event)
{
	FString Payload;
	if (FJsonObjectConverter::UStructToJsonObjectString(Event, Payload))
	{
		TryPublish(MQTTTopics::TaskLifecycle, Payload);
	}
}

void AMyMQTTClient::FlushPendingQueue()
{
	if (!bIsConnected)
	{
		return;
	}

	for (const FMQTTPendingMessage& Message : PendingPublishQueue)
	{
		// TODO(9단계 후속): 실제 플러그인의 Publish(Topic, Payload, QoS) 호출로 교체
	}

	PendingPublishQueue.Empty();
}

void AMyMQTTClient::OnKioskOrderReceived(const FString& JsonPayload)
{
	FKioskOrderRequest Request;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonPayload, &Request, 0, 0))
	{
		return;
	}

	// 현실 키오스크(파이썬)에서 온 요청은 플레이어 컨트롤러를 거치지 않으므로,
	// 인게임 RPC(AFactoryPlayerController::Server_SubmitKioskOrder)와 동일한 공용 함수로 처리한다.
	ApplyKioskOrderRequest(GetWorld(), Request);
}

void AMyMQTTClient::TryPublish(const FString& Topic, const FString& Payload)
{
	if (bIsConnected)
	{
		// TODO(9단계 후속): 실제 플러그인 Publish 호출이 성공하면 큐에 쌓지 않고 여기서 반환.
	}

	FMQTTPendingMessage Pending;
	Pending.Topic = Topic;
	Pending.Payload = Payload;
	Pending.QueuedTimestamp = FPlatformTime::Seconds();
	PendingPublishQueue.Add(Pending);
}
