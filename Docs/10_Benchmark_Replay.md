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
