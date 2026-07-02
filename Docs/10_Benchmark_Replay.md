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
  - `void StartForcedDeadlockDemo(const FStressTestParams& Params)` (회복력 시각적 시연용 정성적 데모 모드)

### `FStressTestParams` (USTRUCT)
- `int32 ForcedDeadlockCount`, `float DurationSeconds` (정성적 데모 전용)

### `FPerfSample` (USTRUCT)
- `double Timestamp`, `float GameThreadTickTimeMs`, `float NavigationTickTimeMs`, `float GPUFrameTimeMs`, `float MemoryUsageMB`, `int32 ActiveAgentCount`

### `FScalingReport` (USTRUCT)
- `int32 BaselineAgentCount`, `int32 ScaledAgentCount`
- `float TickTimeIncreasePercent`, `float MemoryIncreasePercent`, `float GPUTimeIncreasePercent`
- `bool bNonLinearBottleneckDetected` (리소스 증가율이 에이전트 수 증가율보다 비정상적으로 클 때 true)

### `UReplayRecorderSubsystem` (UWorldSubsystem)
> 로직 재실행이 아니라 기록된 상태(State)의 재생.

- 멤버: `bool bIsRecording`, `FString CurrentRecordingFilePath`
- 함수
  - `void StartRecording(const FString& FilePath)`
  - `void StopRecording()`
  - `void OnSnapshotReceived(const FStateSnapshot& Snapshot)`

### `UReplayPlaybackSubsystem` (UWorldSubsystem)
- 멤버: `TArray<FStateSnapshot> LoadedFrames`, `int32 CurrentFrameIndex`, `float PlaybackSpeed = 1.f`
- 함수
  - `bool LoadRecording(const FString& FilePath)`
  - `void Play()`
  - `void Pause()`
  - `void SeekToTime(float TimeSeconds)`

> **구현 비고 (마지막 단계)**
> - `RecordPerfSample()`의 `GameThreadTickTimeMs`/`GPUFrameTimeMs`는 언리얼이 `stat unit`에 쓰는 실제 전역 카운터(`GGameThreadTime`, `RHIGetGPUFrameCycles()`)를 그대로 읽어 계측한다(`RHI`/`RenderCore` 모듈 의존성 추가). 반면 `NavigationTickTimeMs`는 이에 대응하는 공개 전역 카운터가 없어 항상 0으로 남고, 실측은 여전히 `stat navigation` 수동 확인에 의존한다(이미 §14에 기록된 제약과 동일).
> - `RunScalingComparison`은 즉시 계산되는 동기 함수가 아니라, `PhaseDurationSeconds`(밸런싱 값) 동안 `AgentClassToSpawn`(밸런싱 값, Docs에 스폰 대상이 명시돼 있지 않아 노출)을 `SpawnOrigin` 주변에 스폰해두고 `SampleIntervalSeconds`마다 샘플링한 뒤, 기준 규모 → 배율 규모 순으로 단계를 전환하는 타이머 기반 상태머신으로 구현했다. 레벨/내비메시가 아직 없어(기획단계) 실기 계측은 후속 단계 몫이다.
> - `ComputeScalingReport`의 `bNonLinearBottleneckDetected`는 "리소스 증가율이 에이전트 수 증가율보다 비정상적으로 클 때"의 임계값이 Docs에 없어 `NonLinearBottleneckToleranceMultiplier`(밸런싱 값, 기본 1.5배)로 노출했다.
> - `StartForcedDeadlockDemo`는 실제 내비게이션 경합을 강제로 유도하는 것이 아니라, 대상 에이전트에 대해 `Code:001`(교착상태) 이상징후를 발행하는 정성적 시연으로 해석해 구현했다(§14).
> - `UReplayPlaybackSubsystem`은 재생 프레임에 도달할 때마다 `FOnPlaybackFrame` 델리게이트로 `FStateSnapshot`을 방출한다. 스냅샷을 실제 액터(고스트 등)에 어떻게 매핑할지는 Docs에 없어, 이 델리게이트를 구독하는 후속 시각화 소비자의 몫으로 남겨뒀다.
