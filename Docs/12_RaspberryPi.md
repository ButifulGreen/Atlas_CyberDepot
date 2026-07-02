# 12. 외부 데이터 로깅 레이어 (Raspberry Pi, Python)

> Atlas_CyberDepot 아키텍처 설계안 v5 — §12. 구현 9단계(외부 통신) 대상. 언리얼 C++ 빌드에는 포함되지 않는 별도 파이썬 스크립트지만, 형상관리 편의를 위해 이 저장소의 `RaspberryPi/` 폴더에 함께 보관한다(원안은 "저장소 밖"이었으나 협업/추적 편의상 9단계에서 변경 — 실행/배포는 여전히 언리얼 빌드와 독립적이다).

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

> **9단계 구현 비고**: `RaspberryPi/main.py`, `RaspberryPi/requirements.txt`로 작성했다. `is_duplicate`는 `anomaly_log`(`LogID`)와 `task_lifecycle_log`(`EventID`)에만 적용하고, `snapshot_log`는 고유 ID가 없는 주기적 텔레메트리라 중복 제거 대상에서 제외했다. `flush_buffers()`가 만드는 CSV 3종(`anomaly_log.csv`/`snapshot_log.csv`/`task_lifecycle_log.csv`)은 `RaspberryPi/logs/`에 쌓이며, 이 폴더는 실행 산출물이라 `.gitignore`에 등록해 커밋 대상에서 제외했다. 실제 MQTT 브로커/플러그인이 아직 없어(`Docs/11_MQTT.md`) 이 스크립트는 코드 리뷰만 됐고 실기 연동 테스트는 못 한 상태다.
