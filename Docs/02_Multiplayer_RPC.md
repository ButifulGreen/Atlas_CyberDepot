# 2. 멀티플레이어 역할 및 RPC 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §2. 구현 8단계 대상이지만, 3~7단계를 만드는 동안에도 에디터에서 Number of Players 2~3(리슨 서버)으로 테스트하는 습관을 들일 것(`CLAUDE.md` 참고).

현장 2인 + 키오스크 1인, 총 3인 협동 구조. 인게임 키오스크 인터랙트 UI는 제거하고 주문은 키오스크 전용 플레이어만 처리한다.

### `EPlayerRole` (enum)
- `FieldWorker`, `KioskOperator`

### `AFactoryPlayerState` (APlayerState)
- 멤버: `EPlayerRole Role` (Replicated)
- 함수: `void Server_RequestRole(EPlayerRole DesiredRole)` (로비 단계에서 호출, 서버가 중복 배정 검증)

### `AFactoryPlayerController` (APlayerController)
- 함수
  - `void Server_RequestPossessNPC(AFactoryNPCHuman* TargetNPC)` (FieldWorker 전용, 서버에서 `Role` 검증 후 처리)
  - `void Server_SubmitKioskOrder(const FKioskOrderRequest& Request)` (KioskOperator 전용, 서버에서 `Role` 검증)
  - `void Server_JoinRepair(UActorComponent* TargetRepairComponent)`
  - `void Server_LeaveRepair(UActorComponent* TargetRepairComponent)`

> **네트워크 단절 관련 비고**: `Server_SubmitKioskOrder`는 신뢰성 있는 서버 RPC이므로, 클라이언트 접속이 끊긴 시점에는 RPC 자체가 발생하지 않아 주문이 아예 생성되지 않는다(부분 생성 상태로 남지 않음). `Server_LeaveRepair` 없이 플레이어가 강제로 접속 종료된 경우에도 `URepairProgressComponent::ActiveRepairers`는 유효한 참조만 카운트하므로 해당 빙의 인원은 자동으로 집계에서 빠지고 나머지 인원 기준으로 진행 속도만 재계산된다. 별도의 타임아웃 처리는 불필요하다.

### `FKioskOrderRequest` (USTRUCT)
- `FName ItemType`
- `int32 Quantity`
- `EOrderRequestType RequestType` (Inbound / OutboundApproval)
