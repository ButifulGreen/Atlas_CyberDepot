# Docs 인덱스

이 폴더는 `Atlas_CyberDepot` 아키텍처 설계안 v5(시그니처 레벨)를 섹션별로 분리한 것입니다. 원본은 단일 문서였으나, `CLAUDE.md`의 "Lazy Loading" 원칙에 따라 필요한 시스템 문서만 골라서 탐색 승인을 요청할 수 있도록 나눴습니다.

작업을 요청할 때는 "지금은 `Docs/06_Infrastructure.md`만 참고해서 `AStorageShelf`를 구현해줘"처럼 참고할 파일을 명시적으로 좁혀서 지정하는 것을 권장합니다.

| 파일 | 다루는 내용 | 구현 단계(권장 순서) |
|---|---|---|
| `00_DesignPrinciples.md` | 전체 설계 원칙 (§0) | 항상 먼저 숙지 |
| `01_EventBus_DataPipeline.md` | 이벤트 버스, 이상징후/스냅샷/태스크 생명주기 이벤트 구조체 (§1) | 1단계 — Core |
| `02_Multiplayer_RPC.md` | 플레이어 역할, RPC 레이어 (§2) | 8단계 — 멀티플레이어 |
| `03_InventoryOrder.md` | 입출고/평판 운영 루프, 선반 포화 처리 (§3) | 6단계 — 배정/디스패치와 함께 |
| `04_Agent_AI.md` | 에이전트 베이스, AI 컨트롤러, 아틀라스/운송로봇/NPC (§4) | 2단계(베이스) → 5단계(로봇 개별 동작) |
| `05_Repair.md` | 정비 레이어, `URepairProgressComponent` (§5) | 7단계 — 정비/고장 |
| `06_Infrastructure.md` | 선반, 트레이, 도킹 포인트, 대기실(`AIdleWaitingZone`) (§6) | 4단계 — 인프라 |
| `07_TaskAssignment.md` | 작업 배정 레이어, `UOutboundDispatchSubsystem` (§7) | 6단계 — 배정/디스패치 |
| `08_Navigation.md` | NavArea, QueryFilter, `ACostZoneVolume` (§8) | 3단계 — Navigation |
| `09_Visualization.md` | 미니맵/관제실 UI 레이어 (§9) | 9단계 — 시각화 |
| `10_Benchmark_Replay.md` | 벤치마크 하네스, 리플레이 (§10) | 10단계 — 마지막 |
| `11_MQTT.md` | 언리얼 측 MQTT 클라이언트 (§11) | 9단계 — 외부 통신 |
| `12_RaspberryPi.md` | 라즈베리파이 파이썬 로깅 (§12) | 9단계 — 외부 통신 |
| `13_CSVSchema.md` | CSV 스키마 (§13) | 9단계 — 외부 통신과 함께 |
| `14_OpenIssues.md` | 의도적 미해결/미채택 사항 (§14) | 항상 참고 |

권장 구현 순서(의존성 기준)는 `CLAUDE.md`에 요약되어 있습니다.
