# 12. 외부 데이터 로깅 레이어 (Raspberry Pi, Python)

> Atlas_CyberDepot 아키텍처 설계안 v5 — §12. 구현 9단계(외부 통신) 대상. 이 저장소(Unreal C++ 프로젝트) 밖의 별도 파이썬 스크립트다.

### `main.py`
CSV 파일 쓰기는 이 레이어(파이썬)에서만 발생한다. 인코딩 문제(한컴셀 등에서 한글 컬럼/값이 깨지는 현상)를 막기 위해 파일을 열 때 `encoding='utf-8-sig'`를 사용해 BOM을 자동으로 붙인다(언리얼의 `FFileHelper::SaveStringToFile`은 이 레이어와 무관하다 — CSV 쓰기 주체가 파이썬이므로).

- 전역 변수: `BUFFER_FLUSH_INTERVAL_SEC`, `SEEN_LOG_IDS`, `EVENT_BUFFER`, `SNAPSHOT_BUFFER`, `TASK_LIFECYCLE_BUFFER`
- 함수
  - `def on_connect(client, userdata, flags, rc) -> None`
  - `def on_message(client, userdata, msg) -> None`
  - `def is_duplicate(log_id: str) -> bool`
  - `def append_anomaly_row(event: dict) -> None`
  - `def append_snapshot_row(snapshot: dict) -> None`
  - `def append_task_lifecycle_row(event: dict) -> None`
  - `def flush_buffers() -> None` (메모리에 쌓인 각 버퍼를 실제 CSV 파일에 기록하고 비우는 함수. `utf-8-sig` 인코딩으로 파일을 열어 BOM을 포함시킨다. 이 "flush"는 MQTT 재발행(`11_MQTT.md`의 `FlushPendingQueue`)과는 무관한, 파이썬 표준 파일 I/O 동작이다)
  - `def load_credentials_from_env() -> dict`

### `requirements.txt`
- `paho-mqtt` (QoS 0/1/2를 `client.publish(topic, payload, qos=1)` 형태로 표준 지원 — 별도 플러그인 불필요)
