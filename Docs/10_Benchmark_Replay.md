# 10. 벤치마크 / 리플레이 레이어

> Atlas_CyberDepot 아키텍처 설계안 v5 — §10. 구현 마지막 단계 대상. 다른 모든 시스템이 안정화된 뒤 붙인다.

### `UBenchmarkHarnessSubsystem` (UWorldSubsystem)
절대적인 "최대 수용 대수"가 아니라, 기준 규모(실제 배치 예정 대수) 대비 리소스 사용량의 상대적 증가율을 측정하는 데 목적을 둔다 — 실행 하드웨어에 따라 절대치 주장은 설득력이 떨어지므로, 환경 무관하게 의미 있는 "스케일링 비교"로 증명한다.

- 멤버: `TArray<FPerfSample> RecordedSamples`, `FPerfSample BaselineAverage`
- 함수
  - `void RunScalingComparison(int32 BaselineAgentCount, float ScaleMultiplier)` (기준치와 ScaleMultiplier배 규모를 순차 실행하며 샘플링)
  - `void RecordPerfSample()`
  - `FScalingReport ComputeScalingReport() const` (증가율 % 계산)
  - `void ExportPerfReport(const FString& FilePath)`
  - ~~`void StartForcedDeadlockDemo(const FStressTestParams& Params)`~~ — 삭제(2026-07-24). 회복력 시각적
    시연용 정성적 데모였으나, (1) 발행하는 `Code:001` 이상징후를 소비하던 유일한 경로(미니맵)가 기획에서
    빠져 관측 불가능했고, (2) 결정적으로 AI 학습 데이터 파이프라인(`FTrainingLogEntry`)과는 완전히 다른
    채널(`OnAnomalyPublished`)이라 애초에 학습 데이터에 전혀 기여하지 않는다는 게 재검토 중 확인돼(사용자
    판단) 함수/타이머/`FStressTestParams`/콘솔 커맨드까지 전부 삭제. 자세한 배경은
    `Docs/14_OpenIssues.md` 참고.
- 실행 진입점: 콘솔 커맨드(`Benchmark.*`, 아래 참고). `Initialize()`에서 `GetWorld()->GetNetMode() ==
  NM_Client`면 등록을 건너뛴다 — 스폰/이상탐지 발행이 서버 권한 동작이고, 리슨 서버 PIE(서버+클라이언트
  월드가 한 프로세스에 공존)에서 같은 이름의 콘솔 커맨드가 여러 월드에서 중복 등록되는 것도 함께 막기
  위함. `Deinitialize()`에서 `IConsoleManager::UnregisterConsoleObject`로 전부 해제(PIE 세션 종료 후
  댕글링 델리게이트 방지).
  - `Benchmark.RunScalingComparison <BaselineAgentCount> <ScaleMultiplier>`
  - `Benchmark.PrintScalingReport` (인자 없음 — `ComputeScalingReport()` 결과를 로그로 출력. `RunScalingComparison`의
    2단계가 모두 끝난 뒤 호출해야 의미 있는 값이 나온다)
  - `Benchmark.ExportPerfReport <FilePath>`
  - 코드 리뷰로 발견된 버그 수정(2026-07-23): `DoesSupportWorldType()`을 오버라이드해 `EWorldType::Editor`를
    제외(Game/PIE만) — 오버라이드가 없으면 `UWorldSubsystem` 기본값 때문에 Play를 누르지 않은 평범한
    에디터 편집 월드에서도 인스턴스가 생성돼 `NetMode==NM_Standalone`이 `NM_Client` 가드를 통과, 편집 중인
    레벨에 커맨드가 그대로 먹혔다(이후 PIE 월드가 같은 이름을 재등록하면서 에디터 인스턴스의
    `IConsoleObject*`가 댕글링되는 문제도 함께 발생). `RunScalingComparison`/`ExportPerfReport`는 각각
    `bool`을 반환하도록 바꿔(이미 진행 중이라 거부됐는지, 파일 저장이 실제로 성공했는지) 콘솔 로그가 항상
    성공을 가정하고 찍히던 문제를 없앴다. `PrintScalingReport`는 `CurrentPhase != Idle`이거나 완료된 실행이
    없으면 stale/0 값 대신 경고를 낸다. 숫자 인자는 `FCString::IsNumeric`으로 검증한다.
  - 후속 수정(2026-07-23): 스폰/샘플링 관련 밸런싱 값(`AgentClassToSpawn` 등 6개)을 `UBenchmarkSettings`
    (아래 참고)로 이관 — 이전엔 `UWorldSubsystem`에 직접 `EditAnywhere`로 둬서 에디터에서 값을 넣을 CDO
    접점이 아예 없었다(`UReplaySettings`와 동일한 제약, 처음엔 범위 밖이라 남겨뒀던 부분).

### `UBenchmarkSettings` (UDeveloperSettings) — 신규
`UReplaySettings`와 동일한 이유·동일한 구조. `Edit > Project Settings > Game > Atlas Benchmark Settings`
패널로 노출된다.
- `TSoftClassPtr<AFactoryAgentBase> AgentClassToSpawn` (소프트 참조 — 벤치마크 실행 시점에만
  `LoadSynchronous()`로 로드, 세팅 CDO 로드 시점엔 불필요하게 미리 안 불러온다)
- `FVector SpawnOrigin = FVector::ZeroVector`, `float SpawnRadius = 500.f`
- `float SampleIntervalSeconds = 1.f`, `float PhaseDurationSeconds = 30.f`
- `float NonLinearBottleneckToleranceMultiplier = 1.5f`

### `FPerfSample` (USTRUCT)
- `double Timestamp`, `float GameThreadTickTimeMs`, `float GPUFrameTimeMs`, `float MemoryUsageMB`, `int32 ActiveAgentCount`

### `FScalingReport` (USTRUCT)
- `int32 BaselineAgentCount`, `int32 ScaledAgentCount`
- `float TickTimeIncreasePercent`, `float MemoryIncreasePercent`, `float GPUTimeIncreasePercent`
- `bool bNonLinearBottleneckDetected` (리소스 증가율이 에이전트 수 증가율보다 비정상적으로 클 때 true)

### `UReplaySettings` (UDeveloperSettings) — 신규
`Edit > Project Settings > Game > Atlas Replay Settings` 패널로 노출되는 설정. 아래 세 서브시스템은 모두
`UWorldSubsystem`이라 레벨에 배치되는 액터가 아니고, EditAnywhere 프로퍼티를 서브시스템 클래스에 직접
두면 에디터에서 편집할 CDO 접점이 마땅치 않다(Blueprint 서브클래스를 만들어도 별개의 서브시스템
인스턴스가 하나 더 생길 뿐, 실제 코드가 참조하는 C++ 클래스 자체의 인스턴스엔 반영되지 않는다 — 엔진
소스 `FSubsystemCollectionBase::AddAndInitializeSubsystems`가 구체 클래스마다 독립 인스턴스를 만드는
구조라서다). 그래서 관련 밸런싱 값을 이 하나의 `UDeveloperSettings`로 모았다.
- `float RetentionSeconds = 600.f` (리플레이 블랙박스 보존 기간, 초. 테스트 목적 현재 짧은 값)
- `float PruneIntervalSeconds = 10.f` (블랙박스 정리 주기, 초)
- `float TrainingDataFlushIntervalSeconds = 5.f` (AI 학습 로그 강제 Flush 주기, 초)
- `TSoftClassPtr<AReplayGhostActor> AtlasRobotGhostClass`, `TransportRobotGhostClass`, `NPCHumanGhostClass`
  (`EActorType` 값이 3개뿐이라 맵 대신 명시적 슬롯. 소프트 참조라 실제 스폰 시점에만 로드)

### `UReplayRecorderSubsystem` (UWorldSubsystem)
> 로직 재실행이 아니라 기록된 상태(State)의 재생.

블랙박스 — 항목이 들어올 때마다 즉시 파일에 기록하고(강제종료 대비), `UReplaySettings::RetentionSeconds`
보다 오래된 기록은 `UReplaySettings::PruneIntervalSeconds` 주기로 실제 파일에서도 지운다.
`OnWorldBeginPlay`에서 `Saved/Replays/<타임스탬프>.jsonl` 경로로 자동으로 기록을 시작한다(사용자 지시 —
수동 시작에 맡기면 깜빡하고 안 켜는 실수가 생길 수 있어서).

- 멤버: `bool bIsRecording`, `FString CurrentRecordingFilePath`
- 함수
  - `void StartRecording(const FString& FilePath)`
  - `void StopRecording()`
  - `void OnSnapshotReceived(const FStateSnapshot& Snapshot)`

### `UTrainingDataRecorderSubsystem` (UWorldSubsystem)
> AI 학습용 데이터 전용 실시간 기록기. `UReplayRecorderSubsystem`과 소비자·보존 정책이 달라(전자는 리플레이
> 재생용 블랙박스로 오래된 기록을 폐기, 후자는 계속 누적 보존 — 폐기 로직 없음) 분리했다 —
> `FTrainingLogEntry`(`Docs/01_EventBus_DataPipeline.md`) 참고. `StopRecording` 시점에 한 번에 저장하는
> 대신, 항목을 받을 때마다 열려있는 파일 핸들에 즉시 Write한다(강제종료 대비, 사용자 지시 — Write 자체가
> 이미 OS로 데이터를 넘기므로 우리 프로세스가 죽어도 안전하고, `UReplaySettings::TrainingDataFlushIntervalSeconds`는
> 정전 등 OS 레벨 사고에 대한 추가 보강일 뿐이다). `UReplayRecorderSubsystem`과 동일하게 `OnWorldBeginPlay`에서
> `Saved/TrainingData/<타임스탬프>.jsonl` 경로로 자동으로 기록을 시작한다.

- 멤버: `bool bIsRecording`, `FString CurrentRecordingFilePath`
- 함수
  - `void StartRecording(const FString& FilePath)`
  - `void StopRecording()`
  - `void OnTrainingLogReceived(const FTrainingLogEntry& Entry)` (`ElapsedSinceLastEntrySeconds`를 액터별
    직전 기록 시각과 비교해 채운 뒤 즉시 파일에 기록)

### `UReplayPlaybackSubsystem` (UWorldSubsystem)
- 멤버: `TArray<FStateSnapshot> LoadedFrames`, `int32 CurrentFrameIndex`, `float PlaybackSpeed = 1.f`
- 함수
  - `bool LoadRecording(const FString& FilePath)`
  - `void Play()`
  - `void Pause()`
  - `void SeekToTime(float TimeSeconds)` — 버그 수정(2026-07-23): 기존엔 `CurrentFrameIndex`/
    `PlaybackElapsedSeconds`만 갱신하고 `OnPlaybackFrame`을 방출하지 않아, 탐색해도 고스트가 다음 자연
    진행 때까지 그 자리에 멈춰 있었다(스크럽바 드래그가 화면에 반영 안 됨). 탐색 시점에 해당하는 프레임을
    즉시 한 번 방출하도록 고쳤다.
  - `void Stop()` (신규 — `Pause`와 달리 `LoadedFrames`까지 비워 재생 세션 자체를 닫는다. 재생 토글 OFF용)
  - `bool IsPlaying() const` (신규 — 재생 토글이 현재 상태를 판단하는 용도)
  - `double GetPlaybackElapsedSeconds() const`, `double GetTotalDurationSeconds() const` (신규,
    2026-07-23 — `UReplayControlWidget`의 스크럽바가 매 틱 읽는 용도. `PlaybackSpeed`는 배속 조절만
    지원하고 실시간 역재생은 지원하지 않는다(사용자 결정) — `Tick()`의 프레임 진행 루프가 `CurrentFrameIndex`를
    항상 증가만 시키는 구조라, 음수를 넣어도 시간만 거꾸로 흐르고 프레임은 멈춘다)

### `AReplayGhostActor` (AActor) — 신규
`UReplayVisualizationSubsystem`이 스폰/갱신하는 순수 시각화 전용 대역. 실제 게임플레이 액터와 완전히
분리되어 AI/충돌/리플리케이션이 없다(`bReplicates = false`). 실제 메시/머티리얼/상태별 표현은 BP
서브클래스가 구현(코드에서 에셋을 강제하지 않는 기존 패턴 — `KioskWidgetClass` 등과 동일).
- `UTextRenderComponent* NameLabel`(사용자 지시, 신규 — `Root` 위 80유닛에 부착, `FStateSnapshot::
  DisplayName`을 표시). `DrawDebugString`이 아니라 컴포넌트를 쓴 이유 — 재생 프레임은 꾸준히 오지 않고
  (이동 없이 상태만 유지되는 구간은 훨씬 뜸하게 옴) Duration 기반 디버그 문자열은 다음 프레임이 올 때까지
  사라져버린다. 컴포넌트는 갱신 호출 없이도 계속 붙어있어 그 문제가 없다.
- `void UpdateFromSnapshot(const FStateSnapshot& Snapshot)` (Transform 갱신 + `NameLabel` 텍스트 갱신 +
  `BP_OnStateUpdated` 훅)

### `UReplayVisualizationSubsystem` (UWorldSubsystem) — 신규
`UReplayPlaybackSubsystem::OnPlaybackFrame`을 구독해 `FStateSnapshot`을 `AReplayGhostActor`에 매핑하는
소비자. 재생 엔진과 화면 표시를 분리한 기존 설계를 그대로 따라 별도 클래스로 뒀다 — 재생기 자체는 이
클래스의 존재를 모른다. 고스트 클래스 3개(`AtlasRobotGhostClass` 등)는 `UReplaySettings`에서 읽는다.
- 함수
  - `void BeginVisualizing()` (구독 시작)
  - `void StopVisualizing()` (구독 해제 + 스폰된 고스트 전부 `Destroy`)
  - `ActorID→AReplayGhostActor` 매핑을 처음 보면 스폰(해당 `EActorType`의 고스트 클래스가 비어있으면
    경고 로그 후 스킵), 이미 있으면 `UpdateFromSnapshot`만 호출

### `UReplayControlWidget` (UUserWidget) — 신규, 2026-07-23
재생 중 일시정지/탐색(스크럽바)/배속 조절 UI. 사용자가 온스크린 스크럽바 방식을 선택(대안이었던 키 입력만
방식은 화면 표시가 없어 기각). `AReplayGhostActor`/`UMinimapWidget` 등과 동일하게 C++은 데이터·로직만
쥐고, 실제 스크럽바/버튼 배치는 UMG 디자이너에서 `BP_OnPlaybackProgress`를 구현해 채운다.
- `void Play()`, `void Pause()`, `void TogglePause()`, `void SeekToTime(float TimeSeconds)`,
  `void SetReplayPlaybackSpeed(float Speed)` (BP 버튼/스크럽바가 호출 — 전부 `UReplayPlaybackSubsystem`으로
  위임. `Play`/`Pause`가 별도로 있는 이유(2026-07-23) — 재생/일시정지가 하나로 합쳐진 토글 아이콘이면
  `TogglePause()`만 쓰면 되지만, 사용자가 실제로 만든 WBP는 재생/일시정지가 별도 버튼 2개라 `TogglePause()`
  하나를 양쪽에 걸면 이미 재생 중에 "재생" 버튼을 눌러도 멈춰버리는 등 의도와 다르게 동작했다. 이름이
  `SetPlaybackSpeed`가 아닌 이유: `UUserWidget`이 이미 `SetPlaybackSpeed(const UWidgetAnimation*, float)`을
  위젯 애니메이션 재생 속도 용도로 갖고 있어 충돌). `SetReplayPlaybackSpeed`는 0 이하 값을 거부한다(역재생
  미지원 결정, 아래 참고).
- 매 틱 `BP_OnPlaybackProgress(CurrentSeconds, TotalSeconds, bIsPlaying, CurrentSpeed)`를 방출해 스크럽바
  위치/시간 텍스트 등을 갱신시킨다. `OnPlaybackFrame` 이벤트가 아니라 매 틱 직접 값을 읽는 이유 — 스냅샷
  발행 자체가 정지 구간에서 뜸해질 수 있어(`Docs/01_EventBus_DataPipeline.md`), 이벤트 기반이면 그 사이
  스크럽바가 멈춰 보인다.

### 재생 시점 전환 — `AFactorySpectatorPawn` (신규, Docs 이탈 승인됨, 2026-07-22)
고스트는 여전히 녹화 당시의 월드 좌표 그대로 스폰된다(인월드 배치).
- `void BeginReplayCameraView()` — 재생 시작 시 호출. 처음엔 전용 `ACameraActor`로 시점만 전환하고 이 폰의
  이동 입력을 막는 방식으로 구현했었는데, 자동 프레이밍이 빗나가 건물 밖 등을 비추면 안을 살펴볼 방법이
  전혀 없다는 문제가 실기 테스트에서 나왔다(사용자 리포트) — 이 폰 자체를 로드된 스냅샷 중심/반경 기준
  계산 위치로 순간이동시키는 방식으로 재구현했으나, 그 자동 프레이밍마저 물류센터 밖 등 엉뚱한 위치로
  튀는 문제가 있어(사용자 리포트, 2026-07-23) 결국 폐기 — **지금은 위치를 전혀 옮기지 않고 자유비행
  카메라가 있던 자리 그대로 재생을 시작한다.** `PreReplayCameraLocation`/`Rotation`은 재생 도중 사용자가
  직접 이동했을 경우를 위해 계속 기억해둔다.
- `void EndReplayCameraView()` — 재생 정지 시 호출. `BeginReplayCameraView` 진입 직전 위치/시점으로
  되돌린다(`PreReplayCameraLocation`/`PreReplayCameraRotation`에 기억해둠).
- `void SetLiveWorldActorsHidden(bool bShouldHide)` — 버그 수정(사용자 리포트) — 위치만 옮기면
  실시간으로 계속 움직이는 실제 로봇/NPC(`AFactoryAgentBase`)뿐 아니라 선반 위 물류 아이템
  (`ALogisticsItem`)처럼 실시간으로 계속 바뀌는 대상이 고스트와 겹쳐 보이는 문제가 있었다. "전용 화면"의
  의미가 위치 이동이 아니라 실물이 안 보이고 고스트만 보이는 시점이어야 한다는 뜻이었다 — 재생
  시작/종료 시 `TActorIterator`로 순회하며 `SetActorHiddenInGame`을 걸고/푼다. 이 호출은 리플리케이트되지
  않는 순수 로컬 렌더링 상태라(`AActor::GetLifetimeReplicatedProps`에 없음) 다른 플레이어의 화면에는
  영향이 없다 — 각자 독립적으로 리플레이를 본다는 설계 그대로 유지된다.
  - 버그 수정(사용자 리포트, 2차, 심각) — `ALogisticsItem`은 `AFactoryAgentBase`와 달리 `bHidden`이 순수
    렌더링 상태가 아니라 `ALogisticsItemSpawner::TryAcquireItem`이 "재고 풀에서 아직 안 쓰인 아이템"을
    판정하는 실제 게임 로직 기준(`Item->IsHidden()`, `Docs/06_Infrastructure.md` 참고)이다. 처음엔 로봇과
    똑같이 무차별로 숨기고/보이게 했는데, 이러면 풀에서 대기 중이던(원래부터 숨겨져 있던) 아이템까지
    재생 종료 시 강제로 보이게 되어(스포너 액터 위치에 물품이 쌓여 보임) 동시에 모든 아이템이 "숨겨져
    있지 않음"이 되면서 이후 신규 주문이 아이템을 하나도 못 받는 문제로 실제 재현됐다. 그래서
    `SetLiveWorldActorsHidden`은 더 이상 `const`가 아니고, 재생 시작 시점에 **실제로 보이고 있던(사용
    중이던)** 아이템만 골라 `LogisticsItemsHiddenForReplay`에 기억해두고 숨긴 뒤, 종료 시 그 목록만
    정확히 되돌린다 — 풀 대기 아이템은 아예 건드리지 않는다.
  - 새로운 종류(예: 향후 추가될 다른 실시간 시각 요소)가 더 생기면, 그 대상의 `bHidden`이 순수 렌더링
    용도인지 먼저 확인한 뒤(게임 로직이 참조한다면 `ALogisticsItem`과 동일한 "실제로 보이던 것만 기억"
    패턴 필요) 이 함수 안에 순회 루프를 추가하면 된다.
- `ReplayControlWidgetClass`(`UReplayControlWidget` 상속 WBP 슬롯) — 별도 토글 없이 `ToggleReplayAction`의
  재생 시작 분기가 `OpenReplayControlWidget()`을 자동으로 호출해 즉시 띄운다(사용자 지시, 2026-07-23 —
  "리플레이 진입 즉시 UI가 보여야 한다"). 정지 시(`ToggleReplayAction`의 정지 분기, `UnPossessed()`)
  자동으로 닫는다.
- 패널이 열려있는 동안의 입력 처리 — 키오스크(`KioskWidgetClass`)와 달리 **이동은 막지 않는다**(사용자
  지시, 2026-07-23 — "리플레이 보는 내내 자유롭게 움직이면서 마우스/키보드로 버튼도 조작하고 싶다"). 대신
  시야 회전만 기본적으로 막아두고(`SetIgnoreLookInput(true)`, 커서 표시), 신규 `ReplayLookHoldAction`
  (`IA_ReplayLookHold`, 우클릭에 매핑 예정)을 누르고 있는 동안만 `OnReplayLookHoldStarted`가 일시적으로
  `SetInputMode(GameOnly)`+커서 숨김+`SetIgnoreLookInput(false)`로 전환해 시야 회전을 허용하고, 떼면
  `OnReplayLookHoldReleased`가 원복한다 — 에디터 뷰포트의 우클릭 내비게이션과 동일한 방식. 두 핸들러 모두
  패널이 닫혀있으면 아무 효과가 없어 평소 스펙테이팅에는 전혀 관여하지 않는다.

> **구현 비고 (마지막 단계)**
> - `RecordPerfSample()`의 `GameThreadTickTimeMs`/`GPUFrameTimeMs`는 언리얼이 `stat unit`에 쓰는 실제 전역 카운터(`GGameThreadTime`, `RHIGetGPUFrameCycles()`)를 그대로 읽어 계측한다(`RHI`/`RenderCore` 모듈 의존성 추가).
> - ~~`NavigationTickTimeMs`~~ — 2026-07-24 필드 자체를 삭제. 엔진 소스(`NavigationSystem.cpp`)를 확인해보니 내비게이션 계측치가 `STAT_Navigation_TickAsyncBuild` 등 여러 하위 항목으로 쪼개져 있고 하나로 합쳐진 값도, `GGameThreadTime`처럼 아무 코드나 읽을 수 있는 공개 API도 없었다. 더 결정적으로, 실제 스케일링 병목의 대부분을 차지할 군중회피(RVO/Crowd) 연산은 우리 코드가 직접 호출하는 게 아니라 엔진이 매 프레임 자체적으로 돌리는 부분이라, 자체 계측(우리가 호출하는 지점만 타이머로 감싸기)으로도 못 잡는다 — 즉 신뢰할 수 있게 고칠 방법 자체가 없다고 판단, 항상 0인 죽은 필드로 남겨두는 대신 삭제했다(CSV/구조체에 "항상 0인 컬럼"이 남으면 나중에 보는 사람이 버그로 오인할 여지가 있다는 게 삭제 이유). 나중에 진짜 병목이 생겨 정밀 분석이 필요해지면 그 시점에 Unreal Insights/`stat navigation`을 직접 쓰는 쪽을 권장.
> - `RunScalingComparison`은 즉시 계산되는 동기 함수가 아니라, `PhaseDurationSeconds`(밸런싱 값) 동안 `AgentClassToSpawn`(밸런싱 값, Docs에 스폰 대상이 명시돼 있지 않아 노출)을 `SpawnOrigin` 주변에 스폰해두고 `SampleIntervalSeconds`마다 샘플링한 뒤, 기준 규모 → 배율 규모 순으로 단계를 전환하는 타이머 기반 상태머신으로 구현했다. 레벨/내비메시가 아직 없어(기획단계) 실기 계측은 후속 단계 몫이다.
> - `ComputeScalingReport`의 `bNonLinearBottleneckDetected`는 "리소스 증가율이 에이전트 수 증가율보다 비정상적으로 클 때"의 임계값이 Docs에 없어 `NonLinearBottleneckToleranceMultiplier`(밸런싱 값, 기본 1.5배)로 노출했다.
> - ~~`StartForcedDeadlockDemo`는 실제 내비게이션 경합을 강제로 유도하는 것이 아니라, 대상 에이전트에 대해 `Code:001`(교착상태) 이상징후를 발행하는 정성적 시연으로 해석해 구현했다(§14).~~ → 2026-07-24 삭제(§14 참고, AI 학습 데이터 파이프라인과 무관한 채널이라 목적에 기여하지 않는다고 판단).
> - `UReplayPlaybackSubsystem`은 재생 프레임에 도달할 때마다 `FOnPlaybackFrame` 델리게이트로 `FStateSnapshot`을 방출한다. 스냅샷을 실제 액터(고스트 등)에 어떻게 매핑할지는 Docs에 없었으나, `UReplayVisualizationSubsystem`(신규)이 이 역할을 맡아 더 이상 미착수 상태가 아니다.
> - 재생 트리거도 `AFactorySpectatorPawn`의 `ToggleReplayAction`(`IA_ToggleReplay`, 신규, `Docs/02_Multiplayer_RPC.md` 참고)으로 채워졌다 — 관전자 전용, 재생 중이 아니면 `UReplayRecorderSubsystem::CurrentRecordingFilePath`(현재 기록 중인 세션)를 그대로 불러와 재생, 재생 중이면 `Stop`+고스트 정리.
> - `UReplayRecorderSubsystem`이 기록 중인 파일을 재생 토글이 그대로 열어 읽을 수 있어야 해서, `IPlatformFile::OpenWrite`의 `bAllowRead` 인자를 `true`로 바꿨다 — 다만 이것만으론 부족했다(아래 두 항목 참고).
> - 버그 수정(사용자 리포트, 실기 테스트) — `PruneAndRewrite`의 `RecordingFile.Reset(PlatformFile.OpenWrite(...))`는 새 핸들을 먼저 연 뒤에야 기존 핸들이 닫혀서, 기존 핸들이 열려있는 채로 같은 파일을 또 열려다 매번 공유 위반으로 실패했다(`PruneIntervalSeconds`마다 확정적으로 재현). `RecordingFile.Reset()`으로 기존 핸들을 먼저 명시적으로 닫은 뒤 재오픈하도록 고쳤다.
> - 버그 수정(사용자 리포트, 실기 테스트) — `UReplayRecorderSubsystem`/`UTrainingDataRecorderSubsystem`의 자동 시작 파일명이 타임스탬프+프로세스 ID만으론 부족했다 — PIE에서 "Run Under One Process"(기본값)로 여러 플레이어를 켜면 서버/클라이언트 월드가 같은 프로세스에서 돌아 프로세스 ID까지 똑같다. `UPackage::GetPIEInstanceID()`(패키징 빌드에선 항상 -1)까지 더해 두 경우 모두 확실히 겹치지 않게 했다.
> - 버그 수정(사용자 리포트, 실기 테스트) — `UReplayPlaybackSubsystem::LoadRecording`이 쓰던 `FFileHelper::LoadFileToStringArray`는 내부적으로 `bAllowWrite` 없이 파일을 열어서, 기록기가 쓰기 핸들을 쥔 채로(=거의 항상, 재생 토글은 `CurrentRecordingFilePath`를 그대로 불러오므로) 읽으려 하면 매번 공유 위반으로 실패했다. `bAllowRead`는 "쓰기 핸들이 읽기를 허용"하는 쪽이고 이건 반대로 "읽기 핸들이 기존 쓰기를 허용"해야 하는 별개 설정이라, `IPlatformFile::OpenRead(Filename, bAllowWrite=true)`로 직접 열도록 고쳤다.
> - 버그 수정(사용자 리포트, 실기 테스트, 근본 원인) — 위 항목을 다 고친 뒤에도 3분간 플레이한 기록 파일이 여전히 비어 있었다. `AFactoryAgentBase::Tick`의 리플레이 스냅샷 발행 조건이 `CurrentState==Moving`이었는데, 이는 AI 디스패치(`TryRequestWaypointRoute` 등)가 `SetState(Moving)`을 명시적으로 부를 때만 참이 된다. 이 게임의 핵심 조작 방식인 "NPC 빙의 후 Enhanced Input으로 직접 걷기"는 `AddMovementInput`만 호출할 뿐 `SetState`를 전혀 거치지 않아, 플레이어가 직접 조작하는 동안은 `CurrentState`가 계속 `Idle`에 머물러 스냅샷이 단 하나도 발행되지 않았다. "AI가 이동을 의도했는지"가 아니라 "실제로 움직이고 있는지"가 기준이어야 하므로, `GetVelocity().SizeSquared() > KINDA_SMALL_NUMBER`(실제 속도) 기준으로 바꿨다 — AI 디스패치든 플레이어 조작이든 동일하게 잡힌다.
> - 버그 수정(사용자 리포트, 실기 테스트, 근본 원인 — 처음 이 포맷을 설계할 때부터 있던 실수) — 위 항목까지 다 고쳐 파일에 실제 데이터가 쌓이기 시작한 뒤에도 재생이 "유효한 스냅샷 0개"로 계속 실패했다. 파일을 직접 열어보니 스냅샷 하나가 한 줄이 아니라 여러 줄로 들여쓰기되어 저장되고 있었다 — `FJsonObjectConverter::UStructToJsonObjectString`의 2-인자 편의 오버로드는 `bPrettyPrint` 기본값이 `true`다. 기록 쪽(`OnSnapshotReceived`/`PruneAndRewrite`/`UTrainingDataRecorderSubsystem::OnTrainingLogReceived`)은 그냥 문자열을 쓰는 거라 이 포맷으로도 파일 자체는 정상적으로 자랐지만, 재생 쪽(`LoadRecording`)은 줄바꿈 기준으로 "한 줄 = 스냅샷 하나"를 전제하고 있어 실제로는 `{`나 `"schemaVersion": 1,` 같은 JSON 조각만 나뉘어 전부 파싱에 실패했다. 세 호출부 모두 `bPrettyPrint=false`를 명시적으로 넘기도록 고쳤다 — 이 수정 이전에 기록된 파일은 전부 이 깨진 포맷이라 복구되지 않는다.
> - `EventBus->PublishSnapshot`/`PublishTrainingLogEntry`는 `AFactoryAgentBase`가 발행한다 — 이동
>   중(`CurrentState==Moving`)엔 `ReplaySnapshotIntervalSeconds`(기본 0.1초) 주기로 `PublishSnapshot`만,
>   상태 변화 시점엔 `OnRep_CurrentState`에서 `PublishSnapshot`+`PublishTrainingLogEntry`를 함께, 이동
>   목적지가 결정되는 시점(`RequestMoveWithFilter` 호출 직전 6곳)엔 `PublishTrainingLogEntry`만 발행한다.
>   서버/클라이언트 각자 권한 무관하게 자기가 보는 값을 독립적으로 발행하므로(멀티플레이 각 플레이어가
>   리플레이를 각자 로컬로 볼 수 있어야 한다는 설계), 두 기록기 모두 서버 전용으로 가정하지 않는다.
> - `UReplayRecorderSubsystem`/`UTrainingDataRecorderSubsystem` 둘 다 `OnWorldBeginPlay`에서 자동으로
>   `StartRecording`을 호출한다(위 각 섹션 참고) — `StopRecording`을 거는 트리거(디버그 키 등)는 여전히
>   없어, 세션 종료는 `Deinitialize`(월드 종료)에만 의존한다.
> - 처음엔 `RetentionSeconds` 등을 각 `UWorldSubsystem`에 직접 `EditAnywhere`로 뒀었는데, 이 서브시스템들은
>   레벨에 배치되지 않아 에디터에서 편집할 CDO 접점이 없다는 걸 뒤늦게 발견해 `UReplaySettings`
>   (`UDeveloperSettings`)로 전부 이관했다 — `Docs/03_InventoryOrder.md`/`Docs/06_Infrastructure.md` 등
>   다른 곳의 밸런싱 값(대부분 액터의 `EditAnywhere`)과 달리, 이 파이프라인의 밸런싱 값은 전부 프로젝트
>   세팅 패널에서 편집한다는 점이 이 시스템의 특이점이다. `UBenchmarkHarnessSubsystem`도 같은
>   `UWorldSubsystem` 구조라 동일한 제약이 있었는데, 콘솔 커맨드 도입 후 실기 테스트 과정에서 실제로
>   막혀(`AgentClassToSpawn`을 설정할 방법이 없어 스폰 대수가 항상 0) `UBenchmarkSettings`로 동일하게
>   이관해 해소했다(2026-07-23).
> - 버그 수정(2026-07-23, 일시정지 기능 도입 중 발견) — `AFactorySpectatorPawn::OnToggleReplayTriggered`가
>   기존엔 `Playback->IsPlaying()`으로 "정지시킬지/새로 시작할지"를 분기했는데, `Pause()`가 실제로 쓰이기
>   전까지는 `IsPlaying()==false`가 항상 "재생 세션 자체가 없음"과 같아서 문제가 없었다. `UReplayControlWidget`으로
>   일시정지가 실제로 도달 가능한 상태가 되면서, 일시정지 중(`IsPlaying()==false`이지만 `LoadedFrames`는
>   여전히 남아있음)에 재생 토글 키를 누르면 정지가 아니라 기존 세션이 살아있는 채로 새 재생을 또 시작하려
>   드는 버그가 생겼다. 분기 기준을 `LoadedFrames.Num() > 0`(재생 중이든 일시정지든 세션이 살아있는지)으로
>   바꿔 고쳤다.
> - 재설계(2026-07-23) — `UReplayControlWidget` 최초 구현은 `ToggleReplayControlsAction`이라는 별도
>   토글로 여닫고, 열려있는 동안 키오스크와 동일하게 이동/시야를 전부 차단했다. 사용자가 두 가지를 지적:
>   (1) "재생 진입과 동시에 UI가 바로 보여야 한다"는 요구와 맞지 않아 별도 토글을 없애고
>   `ToggleReplayAction`의 재생 시작 분기가 자동으로 패널을 연다. (2) 이동까지 막으면 재생을 보는 내내
>   자유롭게 못 돌아다니는, 이전에 카메라 액터 방식을 폐기했던 것과 같은 문제가 재현된다는 지적에 따라,
>   이동은 아예 막지 않고 시야 회전만 `ReplayLookHoldAction`(우클릭) 홀드 중에만 허용하는 방식(에디터
>   뷰포트 내비게이션과 동일)으로 다시 구현했다.
