# 13. CSV 스키마

> Atlas_CyberDepot 아키텍처 설계안 v5 — §13. `11_MQTT.md`, `12_RaspberryPi.md`와 함께 참고.

### `snapshot_log.csv`
`SchemaVersion, Timestamp, ActorID, ActorType, CurrentState, Location_X/Y/Z, Rotation_P/Y/R, Velocity_X/Y/Z`

### `anomaly_log.csv`
`SchemaVersion, Timestamp, LogID, Severity, ActorID, ActorType, AnomalyCode, Location_X/Y/Z, Velocity_X/Y/Z, TargetLocation_X/Y/Z, NearestObstacleDistance, SafetyZoneStatus, InterrupterType, RiskValue`

### `task_lifecycle_log.csv`
`SchemaVersion, Timestamp, EventID, TaskOrAssignmentID, EventType, ActorID, ActorType, ItemType`

`Code:003`(예방정비 미실시 누적)은 `OnTaskCompleted()`에서 임계치 초과가 지속될 때마다 발행해, "정비를 미룬 패턴 → 실제 고장 발생"의 상관관계를 데이터로 추적 가능하게 한다(`RiskValue`로 당시 확률 수치까지 함께 기록). `Code:004`(선반 포화)는 `IsLineLocked()`가 true로 전환/해제되는 시점마다 발행해, "재고 적체 → 해소 수단(대량 출고 수락/폐기)" 패턴을 추적 가능하게 한다. `task_lifecycle_log.csv`는 트립/거점 배정 단위의 리드타임과 공장 전체의 병목 구간을 분석하는 용도다.
