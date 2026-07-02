# 2. 멀티플레이어 폰/RPC 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §2. 구현 8단계 대상이지만, 3~7단계를 만드는 동안에도 에디터에서 Number of Players 2~3(리슨 서버)으로 테스트하는 습관을 들일 것(`CLAUDE.md` 참고).

현장 2인 + 키오스크(장비) 1대. 두 플레이어는 완전히 대칭 권한을 가지며, 접속 시 관전자로 시작해 NPC를 빙의해 조작한다. 키오스크는 전용 플레이어가 아니라 레벨에 배치된 인게임 장비로, 두 플레이어 중 누구나(또는 향후 9단계에서 연결될 현실 키오스크의 제3자가) 접근해 사용할 수 있다. 원안에 있던 `EPlayerRole`/`AFactoryPlayerState`(역할 배정) 체계는 이 재설계로 필요가 없어져 제거됐다.

### `AFactorySpectatorPawn` (ASpectatorPawn)
- 접속 시 기본 스폰되는 관전자 폰. 1인칭 자유이동.
- 콜리전: 전용 채널 `FactoryBoundary`(`ECC_GameTraceChannel1`, `Config/DefaultEngine.ini`에서 이름 부여)만 Block, 그 외(사물/로봇/NPC 등 모든 기본 채널)는 Ignore — 물류센터 외곽만 막고 내부 사물은 자유롭게 통과한다. 실제 벽 메쉬가 아직 없어(Content 비어있음) 경계는 레벨 제작 시 `FactoryBoundary` 채널로 지정된 별도 볼륨으로 배치될 예정.
- `FindInteractableInFrontOfCamera()`: 카메라 위치/전방 벡터 기준 `InteractTraceDistance`(밸런싱값) 내 라인트레이스로 `AFactoryNPCHuman`/`AFactoryKioskTerminal` 후보를 클라이언트 로컬에서 탐지.
- 인터렉트 입력(Enhanced Input, `IA_Interact`는 에디터에서 별도 생성 필요) 시: 후보가 NPC면 `Server_RequestPossessNPC` 호출, 키오스크면 로컬 UI 트리거(실제 UMG 주문 UI는 9단계 `Docs/09_Visualization.md`에서 연결, 8단계는 RPC 경로 확인용 디버그 로그만).

### `AFactoryPlayerController` (APlayerController)
- 함수(모두 역할 검증 없이 전원 호출 가능 — Role 체계 제거)
  - `void Server_RequestPossessNPC(AFactoryNPCHuman* TargetNPC)` (`TargetNPC->CanBePossessedBy(this)`로 다른 플레이어가 이미 빙의 중인지만 확인. AI가 정비/순찰 중이어도 가로채기 가능)
  - `void Server_ReleaseNPC()` (원안에는 없던 RPC — 관전자 복귀. 빙의 전 자신의 관전자 폰을 기억해뒀다가 재빙의하고, 놓아준 NPC는 `AFactoryNPCHuman::ReleasePossession()`으로 AI 제어 복귀 시도)
  - `void Server_SubmitKioskOrder(AFactoryKioskTerminal* SourceKiosk, const FKioskOrderRequest& Request)` (거리 재검증 후 `Request.RequestType`에 따라 `UInventoryOrderSubsystem::TryPlaceOrder` / `UDeliveryOrderSubsystem::TryAcceptOrder` / `UDeliveryOrderSubsystem::TryCancelOrder`로 분기 호출하는 얇은 위임)
  - `void Server_JoinRepair(UActorComponent* TargetRepairComponent)` (빙의 중인 NPC를 `URepairProgressComponent::Server_JoinRepair`에 참여시킴)
  - `void Server_LeaveRepair(UActorComponent* TargetRepairComponent)`

> **네트워크 단절 관련 비고**: `Server_SubmitKioskOrder`는 신뢰성 있는 서버 RPC이므로, 클라이언트 접속이 끊긴 시점에는 RPC 자체가 발생하지 않아 주문이 아예 생성되지 않는다(부분 생성 상태로 남지 않음). `Server_LeaveRepair` 없이 플레이어가 강제로 접속 종료된 경우에도 `URepairProgressComponent::ActiveRepairers`는 유효한 참조만 카운트하므로 해당 빙의 인원은 자동으로 집계에서 빠지고 나머지 인원 기준으로 진행 속도만 재계산된다. 별도의 타임아웃 처리는 불필요하다.

> **주문 처리 순서 비고**: 인게임 플레이어든 현실 키오스크(9단계에서 연결)든, `UDeliveryOrderSubsystem::TryAcceptOrder`가 서버 게임스레드에서 검색→상태 전환까지 단일 호출로 원자적으로 처리되므로 먼저 도달한 요청만 성공하고 나머지는 즉시 실패한다. 별도의 타임스탬프 큐잉/락 로직은 두지 않는다. 단, 9단계에서 현실 키오스크(MQTT) 경로를 연결할 때도 반드시 게임 스레드에서 `TryAcceptOrder`를 호출해야 이 보장이 유지된다.

### `AFactoryGameMode` (AGameMode)
- `DefaultPawnClass = AFactorySpectatorPawn`
- `PlayerControllerClass = AFactoryPlayerController`
- `GameStateClass = AMSmartFactoryManager` (7단계까지 어떤 GameMode에도 연결된 적 없던 기존 클래스를 처음 연결)
- `PlayerStateClass`는 엔진 기본 유지. `AFactoryPlayerState`는 Role 제거로 존재 이유가 사라져 생성하지 않는다(향후 개인별 통계 등 필요가 생기면 별도 단계에서 추가 검토).

### `AFactoryKioskTerminal` (AActor)
- 레벨에 배치되는 정적 장비. 인터렉트 콜리전만 보유하고 주문 데이터를 자체 보관하지 않는다 — 실제 처리는 `AFactoryPlayerController::Server_SubmitKioskOrder`가 서버측 서브시스템을 호출해 수행한다. 리플리케이션 불필요(상태 없음).

### `FKioskOrderRequest` (USTRUCT, `FactoryKioskTerminal.h`에 정의)
- `EItemType ItemType` (Inbound 전용 — 원안의 `FName ItemType`은 기존 `EItemType` enum 컨벤션과 불일치해 교체)
- `int32 Quantity` (Inbound 전용)
- `FGuid TargetOrderID` (OutboundApproval/Cancel 전용 — 원안에 없던 필드, 어느 `FDeliveryOrder`를 가리키는지 참조하기 위해 추가)
- `EOrderRequestType RequestType` (`Inbound` / `OutboundApproval` / `Cancel` — `Cancel`은 예약됐지만 아직 로봇 미배정인 주문 취소도 동일 RPC 경로를 태우기 위해 추가)
