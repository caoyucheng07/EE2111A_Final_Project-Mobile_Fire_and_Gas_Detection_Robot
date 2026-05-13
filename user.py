import json
import ssl
import os
import threading
import tkinter as tk
from tkinter import filedialog

import paho.mqtt.client as mqtt

MQTT_HOST = "666cb063f1aa4e9a97e37ed4cadebf48.s1.eu.hivemq.cloud"
MQTT_PORT = 8883
MQTT_USER = "hgfbfd"
MQTT_PASS = "12345678aA"

TOPIC_BME = "hgfbfd/bme280"
TOPIC_AI_RESULT = "hgfbfd/ai/result"

alarm_file_path = None
alarm_playing = False


def find_alarm_file():
    global alarm_file_path

    if alarm_file_path and os.path.exists(alarm_file_path):
        return alarm_file_path

    # 1) try same folder as this script
    same_folder = os.path.join(os.path.dirname(__file__), "alarm.mp3")
    if os.path.exists(same_folder):
        alarm_file_path = same_folder
        return alarm_file_path

    # 2) try current working folder
    cwd_file = os.path.join(os.getcwd(), "alarm.mp3")
    if os.path.exists(cwd_file):
        alarm_file_path = cwd_file
        return alarm_file_path

    # 3) ask user to choose the file manually
    root = tk.Tk()
    root.withdraw()
    root.attributes("-topmost", True)

    selected = filedialog.askopenfilename(
        title="Select alarm.mp3",
        filetypes=[("MP3 files", "*.mp3"), ("All files", "*.*")]
    )

    root.destroy()

    if selected:
        alarm_file_path = selected
        return alarm_file_path

    return None


def play_alarm():
    global alarm_playing

    if alarm_playing:
        return

    alarm_path = find_alarm_file()
    if not alarm_path:
        print("[ALARM] No alarm file selected.")
        return

    alarm_playing = True
    try:
        os.startfile(alarm_path)   # Windows: opens with default media player
        print(f"[ALARM] Playing {alarm_path}")
    except Exception as e:
        print(f"[ALARM ERROR] {e}")
    finally:
        alarm_playing = False


def on_connect(client, userdata, flags, rc):
    print("Connected, rc =", rc)
    client.subscribe(TOPIC_BME)
    client.subscribe(TOPIC_AI_RESULT)
    print(f"Subscribed to {TOPIC_BME}")
    print(f"Subscribed to {TOPIC_AI_RESULT}")


def on_message(client, userdata, msg):
    payload = msg.payload.decode("utf-8", errors="ignore")

    if msg.topic == TOPIC_BME:
        try:
            data = json.loads(payload)
            temp = data.get("temp")
            hum = data.get("hum")
            pres = data.get("pres")
            print(f"Temp: {temp} C | Hum: {hum} % | Pres: {pres} hPa")
        except Exception:
            print("Raw BME message:", payload)

    elif msg.topic == TOPIC_AI_RESULT:
        print(f"[AI RESULT] {payload}")

        if "FIRE" in payload.upper():
            print(">>> FIRE ALERT RECEIVED <<<")
            threading.Thread(target=play_alarm, daemon=True).start()


def main():
    client = mqtt.Client()
    client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.tls_set(cert_reqs=ssl.CERT_REQUIRED)

    client.on_connect = on_connect
    client.on_message = on_message

    print("Connecting to HiveMQ...")
    client.connect(MQTT_HOST, MQTT_PORT, 60)
    client.loop_forever()


if __name__ == "__main__":
    main()
