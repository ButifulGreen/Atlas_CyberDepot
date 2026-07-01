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
  - `void OnKioskOrderReceived(const FString& JsonPayload)`
