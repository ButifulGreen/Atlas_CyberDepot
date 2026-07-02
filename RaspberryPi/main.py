# Docs/12_RaspberryPi.md 9단계 대상 - 이 저장소(언리얼 C++ 프로젝트) 밖에서 도는 별도 파이썬 스크립트.
# Raspberry Pi에서 MQTT 브로커(Docs/11_MQTT.md의 AMyMQTTClient)를 구독해 CSV 로그로 적재한다.

import csv
import json
import os
import time
from pathlib import Path

import paho.mqtt.client as mqtt

BUFFER_FLUSH_INTERVAL_SEC = 10
SEEN_LOG_IDS = set()
EVENT_BUFFER = []
SNAPSHOT_BUFFER = []
TASK_LIFECYCLE_BUFFER = []

LOG_DIR = Path(__file__).parent / "logs"

# Docs/11_MQTT.md에 토픽 문자열이 명시돼 있지 않아 AMyMQTTClient.cpp와 값을 맞춰 여기서 정한다.
ANOMALY_TOPIC = "atlas_cyberdepot/anomaly"
SNAPSHOT_TOPIC = "atlas_cyberdepot/snapshot"
TASK_LIFECYCLE_TOPIC = "atlas_cyberdepot/task_lifecycle"

ANOMALY_FIELDS = [
    "SchemaVersion", "Timestamp", "LogID", "Severity", "ActorID", "ActorType",
    "AnomalyCode", "Location_X", "Location_Y", "Location_Z",
    "Velocity_X", "Velocity_Y", "Velocity_Z",
    "TargetLocation_X", "TargetLocation_Y", "TargetLocation_Z",
    "NearestObstacleDistance", "SafetyZoneStatus", "InterrupterType", "RiskValue",
]

SNAPSHOT_FIELDS = [
    "SchemaVersion", "Timestamp", "ActorID", "ActorType", "CurrentState",
    "Location_X", "Location_Y", "Location_Z",
    "Rotation_P", "Rotation_Y", "Rotation_R",
    "Velocity_X", "Velocity_Y", "Velocity_Z",
]

TASK_LIFECYCLE_FIELDS = [
    "SchemaVersion", "Timestamp", "EventID", "TaskOrAssignmentID", "EventType",
    "ActorID", "ActorType", "ItemType",
]


def load_credentials_from_env() -> dict:
    return {
        "broker_host": os.environ.get("MQTT_BROKER_HOST", "localhost"),
        "port": int(os.environ.get("MQTT_PORT", "8883")),
        "username": os.environ.get("MQTT_USERNAME"),
        "password": os.environ.get("MQTT_PASSWORD"),
        "client_id": os.environ.get("MQTT_CLIENT_ID", "atlas-cyberdepot-logger"),
    }


def is_duplicate(log_id: str) -> bool:
    if not log_id:
        return False
    if log_id in SEEN_LOG_IDS:
        return True
    SEEN_LOG_IDS.add(log_id)
    return False


def _flatten_vector(payload: dict, key: str, prefix: str) -> dict:
    vector = payload.get(key) or {}
    return {
        f"{prefix}_X": vector.get("X", ""),
        f"{prefix}_Y": vector.get("Y", ""),
        f"{prefix}_Z": vector.get("Z", ""),
    }


def _flatten_rotator(payload: dict, key: str, prefix: str) -> dict:
    rotator = payload.get(key) or {}
    return {
        f"{prefix}_P": rotator.get("Pitch", ""),
        f"{prefix}_Y": rotator.get("Yaw", ""),
        f"{prefix}_R": rotator.get("Roll", ""),
    }


def append_anomaly_row(event: dict) -> None:
    row = {
        "SchemaVersion": event.get("SchemaVersion", ""),
        "Timestamp": event.get("Timestamp", ""),
        "LogID": event.get("LogID", ""),
        "Severity": event.get("Severity", ""),
        "ActorID": event.get("ActorID", ""),
        "ActorType": event.get("ActorType", ""),
        "AnomalyCode": event.get("AnomalyCode", ""),
        "NearestObstacleDistance": event.get("NearestObstacleDistance", ""),
        "SafetyZoneStatus": event.get("bSafetyZoneStatus", event.get("SafetyZoneStatus", "")),
        "InterrupterType": event.get("InterrupterType", ""),
        "RiskValue": event.get("RiskValue", ""),
    }
    row.update(_flatten_vector(event, "Location", "Location"))
    row.update(_flatten_vector(event, "Velocity", "Velocity"))
    row.update(_flatten_vector(event, "TargetLocation", "TargetLocation"))
    EVENT_BUFFER.append(row)


def append_snapshot_row(snapshot: dict) -> None:
    row = {
        "SchemaVersion": snapshot.get("SchemaVersion", ""),
        "Timestamp": snapshot.get("Timestamp", ""),
        "ActorID": snapshot.get("ActorID", ""),
        "ActorType": snapshot.get("ActorType", ""),
        "CurrentState": snapshot.get("CurrentState", ""),
    }
    row.update(_flatten_vector(snapshot, "Location", "Location"))
    row.update(_flatten_rotator(snapshot, "Rotation", "Rotation"))
    row.update(_flatten_vector(snapshot, "Velocity", "Velocity"))
    SNAPSHOT_BUFFER.append(row)


def append_task_lifecycle_row(event: dict) -> None:
    TASK_LIFECYCLE_BUFFER.append({
        "SchemaVersion": event.get("SchemaVersion", ""),
        "Timestamp": event.get("Timestamp", ""),
        "EventID": event.get("EventID", ""),
        "TaskOrAssignmentID": event.get("TaskOrAssignmentID", ""),
        "EventType": event.get("EventType", ""),
        "ActorID": event.get("ActorID", ""),
        "ActorType": event.get("ActorType", ""),
        "ItemType": event.get("ItemType", ""),
    })


def _flush_one(path: Path, fieldnames: list, buffer: list) -> None:
    if not buffer:
        return

    LOG_DIR.mkdir(parents=True, exist_ok=True)
    is_new_file = not path.exists()

    # 한컴셀 등에서 한글 컬럼/값이 깨지는 걸 막기 위해 BOM을 붙이는 utf-8-sig로 연다.
    with path.open("a", newline="", encoding="utf-8-sig") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        if is_new_file:
            writer.writeheader()
        writer.writerows(buffer)

    buffer.clear()


def flush_buffers() -> None:
    _flush_one(LOG_DIR / "anomaly_log.csv", ANOMALY_FIELDS, EVENT_BUFFER)
    _flush_one(LOG_DIR / "snapshot_log.csv", SNAPSHOT_FIELDS, SNAPSHOT_BUFFER)
    _flush_one(LOG_DIR / "task_lifecycle_log.csv", TASK_LIFECYCLE_FIELDS, TASK_LIFECYCLE_BUFFER)


def on_connect(client, userdata, flags, rc) -> None:
    if rc != 0:
        print(f"MQTT 연결 실패: rc={rc}")
        return

    client.subscribe(ANOMALY_TOPIC, qos=1)
    client.subscribe(SNAPSHOT_TOPIC, qos=1)
    client.subscribe(TASK_LIFECYCLE_TOPIC, qos=1)
    print("MQTT 연결 및 구독 완료")


def on_message(client, userdata, msg) -> None:
    try:
        payload = json.loads(msg.payload.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        print(f"페이로드 파싱 실패: topic={msg.topic}")
        return

    if msg.topic == ANOMALY_TOPIC:
        if is_duplicate(payload.get("LogID", "")):
            return
        append_anomaly_row(payload)
    elif msg.topic == SNAPSHOT_TOPIC:
        # 주기적 상태 스냅샷은 고유 ID가 없는 텔레메트리라 중복 제거 대상이 아니다.
        append_snapshot_row(payload)
    elif msg.topic == TASK_LIFECYCLE_TOPIC:
        if is_duplicate(payload.get("EventID", "")):
            return
        append_task_lifecycle_row(payload)


def main() -> None:
    credentials = load_credentials_from_env()

    client = mqtt.Client(client_id=credentials["client_id"])
    if credentials["username"]:
        client.username_pw_set(credentials["username"], credentials["password"])

    client.on_connect = on_connect
    client.on_message = on_message

    client.connect(credentials["broker_host"], credentials["port"])
    client.loop_start()

    try:
        while True:
            time.sleep(BUFFER_FLUSH_INTERVAL_SEC)
            flush_buffers()
    except KeyboardInterrupt:
        pass
    finally:
        flush_buffers()
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
