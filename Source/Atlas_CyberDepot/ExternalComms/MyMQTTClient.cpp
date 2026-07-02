// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExternalComms/MyMQTTClient.h"
#include "EventBus/FactoryEventTypes.h"
#include "Infrastructure/FactoryKioskTerminal.h"
#include "JsonObjectConverter.h"
#include "Async/Async.h"

// === 매크로 충돌 방지막 ===
// Paho MQTT C 헤더가 정의하는 매크로/기호가 언리얼의 check/verify/UI와 충돌해 반드시 백업/해제/복구가 필요하다.
#include "Windows/AllowWindowsPlatformTypes.h"

#pragma push_macro("check")
#pragma push_macro("verify")
#pragma push_macro("UI")

THIRD_PARTY_INCLUDES_START
#undef UI
#undef check
#undef verify
#include "MQTTAsync.h"
THIRD_PARTY_INCLUDES_END

#pragma pop_macro("UI")
#pragma pop_macro("verify")
#pragma pop_macro("check")

#include "Windows/HideWindowsPlatformTypes.h"

// Docs에 토픽 문자열이 명시돼 있지 않아 정한다. KioskOrder는 현실 키오스크 단말이 발행하는 주문 수신용.
namespace MQTTTopics
{
	static const TCHAR* Anomaly = TEXT("atlas_cyberdepot/anomaly");
	static const TCHAR* Snapshot = TEXT("atlas_cyberdepot/snapshot");
	static const TCHAR* TaskLifecycle = TEXT("atlas_cyberdepot/task_lifecycle");
	static const TCHAR* KioskOrder = TEXT("atlas_cyberdepot/kiosk_order");
}

// MQTTAsync 핸들을 헤더 밖에 숨겨두는 pImpl 대상
class FMQTTWrapper
{
public:
	MQTTAsync ClientHandle = nullptr;
};

// 발행 중인 토픽/페이로드의 UTF8 버퍼 수명을 비동기 콜백이 끝날 때까지 유지하기 위한 컨텍스트
struct FPahoPublishContext
{
	FTCHARToUTF8 Topic;
	FTCHARToUTF8 Payload;

	FPahoPublishContext(const FString& InTopic, const FString& InPayload)
		: Topic(*InTopic)
		, Payload(*InPayload)
	{
	}
};

// --- C API 정적 콜백들(Paho 백그라운드 스레드에서 실행됨) ---

static void OnConnectSuccess(void* Context, MQTTAsync_successData* /*Response*/)
{
	if (AMyMQTTClient* Client = static_cast<AMyMQTTClient*>(Context))
	{
		Client->HandleConnectionSuccess();
	}
}

static void OnConnectFailure(void* Context, MQTTAsync_failureData* /*Response*/)
{
	if (AMyMQTTClient* Client = static_cast<AMyMQTTClient*>(Context))
	{
		Client->HandleConnectionFailure();
	}
}

static void OnConnectionLostCallback(void* Context, char* Cause)
{
	if (AMyMQTTClient* Client = static_cast<AMyMQTTClient*>(Context))
	{
		Client->HandleConnectionLost(Cause ? FString(UTF8_TO_TCHAR(Cause)) : TEXT("Unknown"));
	}
}

static int OnMessageArrivedCallback(void* Context, char* TopicName, int /*TopicLen*/, MQTTAsync_message* Message)
{
	if (AMyMQTTClient* Client = static_cast<AMyMQTTClient*>(Context))
	{
		if (Message)
		{
			const FString Topic(UTF8_TO_TCHAR(TopicName));

			TArray<uint8> PayloadBytes;
			PayloadBytes.Append(static_cast<uint8*>(Message->payload), Message->payloadlen);
			PayloadBytes.Add(0);
			const FString Payload(UTF8_TO_TCHAR(reinterpret_cast<const char*>(PayloadBytes.GetData())));

			Client->HandleMessageArrived(Topic, Payload);
		}
	}

	MQTTAsync_freeMessage(&Message);
	MQTTAsync_free(TopicName);
	return 1;
}

static void OnSubscribeFailure(void* /*Context*/, MQTTAsync_failureData* /*Response*/)
{
	UE_LOG(LogTemp, Warning, TEXT("MQTT 구독 실패"));
}

static void OnPublishComplete(void* Context, MQTTAsync_successData* /*Response*/)
{
	delete static_cast<FPahoPublishContext*>(Context);
}

static void OnPublishFailureCallback(void* Context, MQTTAsync_failureData* /*Response*/)
{
	UE_LOG(LogTemp, Warning, TEXT("MQTT 발행 실패"));
	delete static_cast<FPahoPublishContext*>(Context);
}

// --- AMyMQTTClient 구현부 ---

AMyMQTTClient::AMyMQTTClient()
{
	PrimaryActorTick.bCanEverTick = false;
	Wrapper = MakeUnique<FMQTTWrapper>();
}

AMyMQTTClient::~AMyMQTTClient()
{
	if (Wrapper && Wrapper->ClientHandle)
	{
		MQTTAsync_destroy(&Wrapper->ClientHandle);
	}
}

AMyMQTTClient::AMyMQTTClient(FVTableHelper& Helper)
	: Super(Helper)
{
}

void AMyMQTTClient::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Wrapper && Wrapper->ClientHandle)
	{
		MQTTAsync_disconnectOptions DiscOpts = MQTTAsync_disconnectOptions_initializer;
		DiscOpts.timeout = 1000;
		MQTTAsync_disconnect(Wrapper->ClientHandle, &DiscOpts);
	}

	Super::EndPlay(EndPlayReason);
}

void AMyMQTTClient::Connect()
{
	if (!Wrapper)
	{
		Wrapper = MakeUnique<FMQTTWrapper>();
	}

	if (Wrapper->ClientHandle)
	{
		MQTTAsync_destroy(&Wrapper->ClientHandle);
		Wrapper->ClientHandle = nullptr;
	}

	const FString ServerURI = FString::Printf(TEXT("ssl://%s:%d"), *BrokerHost, Port);

	MQTTAsync_create(&Wrapper->ClientHandle, TCHAR_TO_UTF8(*ServerURI), TCHAR_TO_UTF8(*ClientID), MQTTCLIENT_PERSISTENCE_NONE, nullptr);
	MQTTAsync_setCallbacks(Wrapper->ClientHandle, this, OnConnectionLostCallback, OnMessageArrivedCallback, nullptr);

	MQTTAsync_connectOptions ConnOpts = MQTTAsync_connectOptions_initializer;
	ConnOpts.keepAliveInterval = 20;
	ConnOpts.cleansession = 1;
	ConnOpts.onSuccess = OnConnectSuccess;
	ConnOpts.onFailure = OnConnectFailure;
	ConnOpts.context = this;

	FTCHARToUTF8 Utf8User(*Username);
	FTCHARToUTF8 Utf8Pass(*Password);
	if (!Username.IsEmpty())
	{
		ConnOpts.username = Utf8User.Get();
		ConnOpts.password = Utf8Pass.Get();
	}

	MQTTAsync_SSLOptions SslOpts = MQTTAsync_SSLOptions_initializer;
	SslOpts.enableServerCertAuth = 0;
	ConnOpts.ssl = &SslOpts;

	const int ReturnCode = MQTTAsync_connect(Wrapper->ClientHandle, &ConnOpts);
	if (ReturnCode != MQTTASYNC_SUCCESS)
	{
		UE_LOG(LogTemp, Error, TEXT("MQTT 연결 시작 실패. Code: %d"), ReturnCode);
	}
}

void AMyMQTTClient::HandleConnectionSuccess()
{
	TWeakObjectPtr<AMyMQTTClient> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis]()
	{
		if (AMyMQTTClient* Client = WeakThis.Get())
		{
			Client->bIsConnected = true;
			Client->SubscribeToKioskOrderTopic();
			Client->FlushPendingQueue();
		}
	});
}

void AMyMQTTClient::HandleConnectionFailure()
{
	UE_LOG(LogTemp, Error, TEXT("MQTT 연결 실패"));
}

void AMyMQTTClient::HandleConnectionLost(const FString& Cause)
{
	TWeakObjectPtr<AMyMQTTClient> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, Cause]()
	{
		if (AMyMQTTClient* Client = WeakThis.Get())
		{
			Client->bIsConnected = false;
			UE_LOG(LogTemp, Warning, TEXT("MQTT 연결 끊김: %s"), *Cause);
		}
	});
}

void AMyMQTTClient::HandleMessageArrived(const FString& Topic, const FString& Payload)
{
	TWeakObjectPtr<AMyMQTTClient> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, Topic, Payload]()
	{
		AMyMQTTClient* Client = WeakThis.Get();
		if (!Client)
		{
			return;
		}

		if (Topic == MQTTTopics::KioskOrder)
		{
			Client->OnKioskOrderReceived(Payload);
		}
	});
}

void AMyMQTTClient::SubscribeToKioskOrderTopic()
{
	if (!Wrapper || !Wrapper->ClientHandle)
	{
		return;
	}

	MQTTAsync_responseOptions Opts = MQTTAsync_responseOptions_initializer;
	Opts.onFailure = OnSubscribeFailure;

	MQTTAsync_subscribe(Wrapper->ClientHandle, TCHAR_TO_UTF8(MQTTTopics::KioskOrder), PublishQoSLevel, &Opts);
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

	const TArray<FMQTTPendingMessage> ToFlush = PendingPublishQueue;
	PendingPublishQueue.Empty();

	for (const FMQTTPendingMessage& Message : ToFlush)
	{
		TryPublish(Message.Topic, Message.Payload);
	}
}

void AMyMQTTClient::OnKioskOrderReceived(const FString& JsonPayload)
{
	FKioskOrderRequest Request;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonPayload, &Request, 0, 0))
	{
		return;
	}

	// 현실 키오스크에서 온 요청은 플레이어 컨트롤러를 거치지 않으므로, 인게임 RPC와 동일한 공용 함수로 처리한다.
	ApplyKioskOrderRequest(GetWorld(), Request);
}

bool AMyMQTTClient::TryPublish(const FString& Topic, const FString& Payload)
{
	if (bIsConnected && Wrapper && Wrapper->ClientHandle)
	{
		FPahoPublishContext* Context = new FPahoPublishContext(Topic, Payload);

		MQTTAsync_message PubMsg = MQTTAsync_message_initializer;
		PubMsg.payload = const_cast<void*>(reinterpret_cast<const void*>(Context->Payload.Get()));
		PubMsg.payloadlen = Context->Payload.Length();
		PubMsg.qos = PublishQoSLevel;
		PubMsg.retained = 0;

		MQTTAsync_responseOptions Opts = MQTTAsync_responseOptions_initializer;
		Opts.context = Context;
		Opts.onSuccess = OnPublishComplete;
		Opts.onFailure = OnPublishFailureCallback;

		const int ReturnCode = MQTTAsync_sendMessage(Wrapper->ClientHandle, Context->Topic.Get(), &PubMsg, &Opts);
		if (ReturnCode == MQTTASYNC_SUCCESS)
		{
			return true;
		}

		delete Context;
	}

	FMQTTPendingMessage Pending;
	Pending.Topic = Topic;
	Pending.Payload = Payload;
	Pending.QueuedTimestamp = FPlatformTime::Seconds();
	PendingPublishQueue.Add(Pending);
	return false;
}
