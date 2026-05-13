import threading
import os
import json
import ssl
import time
import base64
from datetime import datetime

import requests
import paho.mqtt.client as mqtt
from dotenv import load_dotenv

load_dotenv()

# ========= OpenAI =========
OPENAI_API_KEY = os.getenv("OPENAI_API_KEY")

# ========= MQTT / HiveMQ =========
MQTT_HOST = "666cb063f1aa4e9a97e37ed4cadebf48.s1.eu.hivemq.cloud"
MQTT_PORT = 8883
MQTT_USER = "hgfbfd"
MQTT_PASS = "12345678aA"

TOPIC_BME       = "hgfbfd/bme280"
TOPIC_CMD       = "hgfbfd/ir/cmd"      # reuse your current bridge topic
TOPIC_STAT      = "hgfbfd/ir/status"
TOPIC_AI_RESULT = "hgfbfd/ai/result"

# ========= ESP32-CAM =========
CAMERA_JPG_URL = "http://172.20.10.13/jpg"

# ========= Trigger settings =========
TEMP_THRESHOLD = 32.0
MQ2_THRESHOLD = 3500
COOLDOWN_SECONDS = 15

# ========= Investigation settings =========
MAX_INVESTIGATION_ROUNDS = 3
TURN90_WAIT = 1.2
TURN180_WAIT = 2.2
STEP_WAIT = 1.0
SETTLE_WAIT = 0.7
SHORT_WAIT = 0.4

latest_temp = None
latest_mq2 = None
last_trigger_time = 0
trigger_armed = True
investigation_busy = False

ALARM_FILE = "alarm.mp3"
alarm_playing = False

def publish_result(client, text: str):
    print("[AI RESULT]", text)
    client.publish(TOPIC_AI_RESULT, text)


def send_bot_cmd(client, cmd: str, wait_s: float = SHORT_WAIT):
    print(f"[BOT CMD] {cmd}")
    client.publish(TOPIC_CMD, cmd)
    time.sleep(wait_s)


def capture_from_esp32cam(tag: str) -> str:
    os.makedirs("captures", exist_ok=True)
    filename = os.path.join(
        "captures",
        datetime.now().strftime(f"{tag}_%Y%m%d_%H%M%S.jpg")
    )

    print(f"[CAM] Fetching image from {CAMERA_JPG_URL}")
    resp = requests.get(CAMERA_JPG_URL, timeout=10)
    resp.raise_for_status()

    with open(filename, "wb") as f:
        f.write(resp.content)

    print(f"[CAM] Saved image as {filename}")
    return filename


def _responses_text(data: dict) -> str:
    text = data.get("output_text", "").strip()
    if text:
        return text

    out = []
    for item in data.get("output", []):
        for c in item.get("content", []):
            if c.get("type") in ("output_text", "text"):
                out.append(c.get("text", ""))
    return "\n".join(out).strip()


def call_openai_with_content(content: list) -> str:
    headers = {
        "Authorization": f"Bearer {OPENAI_API_KEY}",
        "Content-Type": "application/json",
    }

    payload = {
        "model": "gpt-4.1-mini",
        "input": [
            {
                "role": "user",
                "content": content
            }
        ]
    }

    resp = requests.post(
        "https://api.openai.com/v1/responses",
        headers=headers,
        json=payload,
        timeout=60
    )
    resp.raise_for_status()
    data = resp.json()
    return _responses_text(data)


def image_to_data_url(image_path: str) -> str:
    with open(image_path, "rb") as f:
        b64 = base64.b64encode(f.read()).decode("utf-8")
    return f"data:image/jpeg;base64,{b64}"


def normalize_decision(raw: str) -> str:
    t = raw.strip().upper()

    for token in ["FIRE", "CLEAR", "FRONT", "LEFT", "RIGHT", "BACK"]:
        if token in t:
            return token

    return "CLEAR"


def analyze_scan_with_openai(front: str, left: str, right: str, back: str) -> str:
    content = [
        {
            "type": "input_text",
            "text": (
                "You are guiding a fire-investigation robot.\n"
                "These 4 images are captured from the SAME robot position in this exact order:\n"
                "1 = FRONT\n"
                "2 = LEFT\n"
                "3 = RIGHT\n"
                "4 = BACK\n\n"
                "Pick EXACTLY ONE token from this list only:\n"
                "FRONT, LEFT, RIGHT, BACK, CLEAR, FIRE\n\n"
                "Use FIRE only if visible fire or smoke is already clearly present.\n"
                "Use CLEAR if no direction appears suspicious.\n"
                "Otherwise choose the most suspicious direction to investigate next.\n"
                "Reply with one token only."
            )
        },
        {"type": "input_image", "image_url": image_to_data_url(front)},
        {"type": "input_image", "image_url": image_to_data_url(left)},
        {"type": "input_image", "image_url": image_to_data_url(right)},
        {"type": "input_image", "image_url": image_to_data_url(back)},
    ]

    raw = call_openai_with_content(content)
    print("[OPENAI RAW DECISION]", raw)
    return normalize_decision(raw)


def scan_four_directions(client, round_idx: int):
    # Assume robot starts this scan facing its current "front"
    images = {}

    time.sleep(SETTLE_WAIT)
    images["FRONT"] = capture_from_esp32cam(f"r{round_idx}_front")

    send_bot_cmd(client, "TURN_LEFT_90", TURN90_WAIT)
    time.sleep(SETTLE_WAIT)
    images["LEFT"] = capture_from_esp32cam(f"r{round_idx}_left")

    send_bot_cmd(client, "TURN_RIGHT_90", TURN90_WAIT)   # back to front
    send_bot_cmd(client, "TURN_RIGHT_90", TURN90_WAIT)   # now facing right
    time.sleep(SETTLE_WAIT)
    images["RIGHT"] = capture_from_esp32cam(f"r{round_idx}_right")

    send_bot_cmd(client, "TURN_LEFT_90", TURN90_WAIT)    # back to front
    send_bot_cmd(client, "TURN_RIGHT_180", TURN180_WAIT) # now facing back
    time.sleep(SETTLE_WAIT)
    images["BACK"] = capture_from_esp32cam(f"r{round_idx}_back")

    send_bot_cmd(client, "TURN_RIGHT_180", TURN180_WAIT) # restore original front

    return images


def move_toward_decision(client, decision: str):
    if decision == "LEFT":
        send_bot_cmd(client, "TURN_LEFT_90", TURN90_WAIT)
    elif decision == "RIGHT":
        send_bot_cmd(client, "TURN_RIGHT_90", TURN90_WAIT)
    elif decision == "BACK":
        send_bot_cmd(client, "TURN_RIGHT_180", TURN180_WAIT)
    elif decision == "FRONT":
        pass

    send_bot_cmd(client, "STEP_FWD_INVEST", STEP_WAIT)


def run_investigation(client):
    global investigation_busy

    if investigation_busy:
        return

    investigation_busy = True
    try:
        publish_result(client, "INVESTIGATION_START")

        send_bot_cmd(client, "PATROL_OFF", SHORT_WAIT)
        send_bot_cmd(client, "STOP", SHORT_WAIT)

        for round_idx in range(1, MAX_INVESTIGATION_ROUNDS + 1):
            publish_result(client, f"SCAN_ROUND_{round_idx}")

            imgs = scan_four_directions(client, round_idx)
            decision = analyze_scan_with_openai(
                imgs["FRONT"],
                imgs["LEFT"],
                imgs["RIGHT"],
                imgs["BACK"]
            )

            publish_result(client, f"ROUND_{round_idx}_DECISION:{decision}")

            if decision == "FIRE":
                send_bot_cmd(client, "STOP", SHORT_WAIT)
                publish_result(client, "ALERT:FIRE_OR_SMOKE_CONFIRMED")
                threading.Thread(target=play_alarm, daemon=True).start()
                return

            if decision == "CLEAR":
                send_bot_cmd(client, "PATROL_ON", SHORT_WAIT)
                publish_result(client, "CLEAR:RESUME_PATROL")
                return

            move_toward_decision(client, decision)
            time.sleep(SETTLE_WAIT)

        send_bot_cmd(client, "STOP", SHORT_WAIT)
        publish_result(client, "MAX_ROUNDS_REACHED:CHECK_MANUALLY")

    except Exception as e:
        err = f"ERROR:{e}"
        print(err)
        publish_result(client, err)
        try:
            send_bot_cmd(client, "STOP", SHORT_WAIT)
        except Exception:
            pass
    finally:
        investigation_busy = False


def on_connect(client, userdata, flags, rc):
    print("MQTT connected, rc =", rc)
    client.subscribe(TOPIC_BME)
    client.subscribe(TOPIC_STAT)


def on_message(client, userdata, msg):
    global latest_temp, latest_mq2

    payload = msg.payload.decode("utf-8", errors="ignore")

    if msg.topic == TOPIC_STAT:
        print(f"[STAT] {payload}")
        return

    if msg.topic == TOPIC_BME:
        try:
            print(f"[MQTT] {msg.topic}: {payload}")
            data = json.loads(payload)
            latest_temp = float(data["temp"])
            latest_mq2 = int(data["mq2"])
        except Exception as e:
            print("Message parse error:", e)


def should_trigger() -> bool:
    global last_trigger_time, trigger_armed, latest_temp, latest_mq2, investigation_busy

    if investigation_busy or latest_temp is None or latest_mq2 is None:
        return False

    now = time.time()

    if latest_temp <= TEMP_THRESHOLD and latest_mq2 < MQ2_THRESHOLD:
        trigger_armed = True
        return False

    if (latest_temp > TEMP_THRESHOLD or latest_mq2 >= MQ2_THRESHOLD) and trigger_armed and (now - last_trigger_time >= COOLDOWN_SECONDS):
        if latest_temp > TEMP_THRESHOLD:
            print(f"[TRIGGER] Temperature {latest_temp:.2f}C > {TEMP_THRESHOLD:.2f}C")
        if latest_mq2 >= MQ2_THRESHOLD:
            print(f"[TRIGGER] MQ2 {latest_mq2} >= {MQ2_THRESHOLD}")
        trigger_armed = False
        last_trigger_time = now
        return True

    return False


def play_alarm():
    global alarm_playing

    if alarm_playing:
        return

    alarm_playing = True
    try:
        if os.path.exists(ALARM_FILE):
            os.startfile(ALARM_FILE)   # Windows: opens the mp3 with default player
            print(f"[ALARM] Playing {ALARM_FILE}")
        else:
            print(f"[ALARM] File not found: {ALARM_FILE}")
    finally:
        alarm_playing = False


def main():
    if not OPENAI_API_KEY:
        raise RuntimeError("Missing OPENAI_API_KEY in .env")

    client = mqtt.Client()
    client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.tls_set(cert_reqs=ssl.CERT_REQUIRED)

    client.on_connect = on_connect
    client.on_message = on_message

    print("[MQTT] Connecting...")
    client.connect(MQTT_HOST, MQTT_PORT, 60)
    client.loop_start()

    try:
        while True:
            if should_trigger():
                run_investigation(client)
            time.sleep(0.2)
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
