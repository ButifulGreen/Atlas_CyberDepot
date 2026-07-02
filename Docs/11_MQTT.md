# 11. 외부 통신 레이어 (언리얼 측)

> Atlas_CyberDepot 아키텍처 설계안 v5 — §11. 구현 9단계(외부 통신) 대상. `12_RaspberryPi.md`와 짝을 이룬다.

### `FMQTTPendingMessage` (USTRUCT)
연결 끊김으로 발행에 실패한 메시지를 임시 보관하기 위한 구조체.
- `FString Topic`
- `FString Payload`
- `double QueuedTimestamp`

### `AMyMQTTClient` (AActor)
- 멤버
  - `FString BrokerHost`, `int32 Port = 8883`, `FString ClientID`
  - `TArray<FMQTTPendingMessage> PendingPublishQueue` (발행 실패 시 적재되는 로컬 큐. 별도 외부 플러그인이 아니라 이미 사용 중인 MQTT 클라이언트 위에 얹는 애플리케이션 레벨 배열)
  - `int32 PublishQoSLevel = 1` (MQTT 프로토콜 표준 QoS 레벨. 0=최대 1회 전송(유실 가능), 1=최소 1회 도착 보장(중복 가능, 브로커 ACK 대기 후 필요 시 재전송), 2=정확히 1회(오버헤드 큼, 본 프로젝트엔 과함). 파이썬 측 `is_duplicate`/`SEEN_LOG_IDS`가 이미 중복 제거를 담당하므로 QoS 1과 궁합이 좋다)
- 함수
  - `void Connect()` (연결 성공 시 `FlushPendingQueue()` 자동 호출)
  - `void PublishAnomalyEvent(const FAnomalyEvent& Event)` (발행 실패 시 `PendingPublishQueue`에 적재)
  - `void PublishSnapshot(const FStateSnapshot& Snapshot)` (발행 실패 시 `PendingPublishQueue`에 적재)
  - `void PublishTaskLifecycleEvent(const FTaskLifecycleEvent& Event)` (발행 실패 시 `PendingPublishQueue`에 적재)
  - `void FlushPendingQueue()` (재연결 시 `PendingPublishQueue`에 쌓인 메시지를 순서대로 재발행 후 큐 비움)
  - `void OnKioskOrderReceived(const FString& JsonPayload)` (`Docs/02_Multiplayer_RPC.md`의 `FKioskOrderRequest`로 역직렬화 후, `AFactoryPlayerController::Server_SubmitKioskOrder`와 동일한 공용 함수 `ApplyKioskOrderRequest(UWorld*, const FKioskOrderRequest&)`(`FactoryKioskTerminal.h`)를 호출한다 — 현실 키오스크는 플레이어 컨트롤러를 거치지 않으므로 거리 검증 없이 서브시스템을 바로 호출)

> **구현 비고 (후속 갱신)**: 마켓플레이스 플러그인이 아니라 **Eclipse Paho MQTT C 비동기 클라이언트(`paho.mqtt.c`, `MQTTAsync.h`)를 서드파티로 직접 연동**했다. `Source/ThirdParty/PahoMQTT/PahoMQTT.Build.cs`가 헤더/라이브러리 경로를 잡아주는 `ModuleType.External` 래퍼이고, `Atlas_CyberDepot.Build.cs`가 이를 `PrivateDependencyModuleNames`로 참조한다. Paho 헤더가 언리얼의 `check`/`verify`/`UI` 매크로와 충돌하므로 `MyMQTTClient.cpp`에서 `push_macro`/`pop_macro`로 임시 해제·복구하는 방어막을 두었다. `Connect()`가 `MQTTAsync_connect`로 SSL(`ssl://host:port`) 연결을 시작하고, 성공 콜백(`OnConnectSuccess`)에서 `bIsConnected = true` 설정 → `atlas_cyberdepot/kiosk_order` 토픽 구독 → `FlushPendingQueue()` 순서로 진행한다. `TryPublish()`는 연결돼 있으면 `MQTTAsync_sendMessage`로 실제 발행을 시도하고, 그 호출 자체가 실패하거나 애초에 미연결 상태면 `PendingPublishQueue`에 적재한다 — 단, 브로커가 발행을 사후에 거부하는 경우(비동기 실패 콜백)는 로그만 남기고 자동 재큐잉하지 않는다(범위 밖으로 문서화).
>
> **라이브러리 배치 완료.** `Source/ThirdParty/PahoMQTT/Includes/`(C 헤더, `Includes/mqtt/`의 C++ 래퍼 헤더는 미사용), `Source/ThirdParty/PahoMQTT/Libraries/paho-mqtt3as.lib`, `Source/ThirdParty/PahoMQTT/Binaries/paho-mqtt3as.dll`에 실제 파일이 배치돼 컴파일이 확인됐다(Win64 하위 폴더 없이 평평한 구조). 동일 폴더에 `paho-mqtt3a`/`3c`/`3cs`, C++ 래퍼 `paho-mqttpp3` 라이브러리도 함께 있지만 `MyMQTTClient.cpp`는 순수 C 비동기+SSL API만 사용해 `paho-mqtt3as`만 링크한다.
>
> **토픽 이름 규칙**: Docs에 명시돼 있지 않아 구현 시 `atlas_cyberdepot/anomaly`, `atlas_cyberdepot/snapshot`, `atlas_cyberdepot/task_lifecycle`, `atlas_cyberdepot/kiosk_order`(현실 키오스크→언리얼 주문 수신 전용, 신규)로 정했다(`MyMQTTClient.cpp`, `RaspberryPi/main.py` 양쪽에서 동일하게 사용 — 단 `kiosk_order`는 CSV 로깅 대상이 아니라 `RaspberryPi/main.py`는 구독하지 않는다).
