# EE2111A_Final_Project-Mobile_Fire_and_Gas_Detection_Robot
# EE2111A Final Project - Mobile Fire and Gas Detection Robot

## Overview
This project is a mobile fire and gas detection robot designed for early hazard detection in areas such as construction sites, labs, or indoor spaces. The robot patrols an area, detects abnormal temperature or gas readings, captures images using an ESP32-CAM, and uses an AI-based local server to confirm potential fire or smoke before raising an alarm.

## Key Features
- Autonomous patrol using DC motors
- Obstacle and boundary avoidance
- Temperature, humidity, and pressure sensing using BME280
- Gas/smoke detection using MQ-2 sensor
- Low-pass filtering for MQ-2 signal stability
- ESP32-CAM image capture
- MQTT communication through HiveMQ Cloud
- AI-based fire/smoke investigation using OpenAI API
- Alarm notification on fire/smoke confirmation
- IR-based docking/charging station detection

## System Architecture
Robot hardware:
- mBot / Arduino: motor control and patrol logic
- ESP32: sensor reading, MQTT communication, command bridge
- ESP32-CAM: image capture
- STM32: IR LED transmitter/receiver function for docking
- Laptop/local server: receives sensor data, requests camera image, sends image to AI, returns robot commands

Data flow:
1. Sensors collect temperature, humidity, pressure, and gas readings.
2. ESP32 publishes sensor data to HiveMQ using MQTT.
3. Python local server subscribes to MQTT data.
4. If temperature or MQ-2 exceeds threshold, patrol stops.
5. ESP32-CAM captures images from different directions.
6. Local server sends images to OpenAI for analysis.
7. If fire/smoke is confirmed, alarm is triggered.

## Repository Structure
| File | Description |
|---|---|
| `Mbot_code.ino` | Arduino/mBot movement and patrol control |
| `ESP_Code.ino` | ESP32 sensor reading and MQTT communication |
| `Cam_Code.ino` | ESP32-CAM image capture server |
| `STM_Code.c` | STM32 IR transmitter/receiver functionality |
| `ai_temp_trigger.py` | Local server for sensor trigger, AI image analysis, and robot command control |
| `user.py` | User-side MQTT monitor and alarm receiver |

## Hardware Components
- mBot / Arduino-compatible controller
- DC motors
- Motor driver
- ESP32
- ESP32-CAM
- STM32 board
- BME280 temperature/pressure/humidity sensor
- MQ-2 gas sensor
- IR LEDs and IR receiver
- Resistors/capacitors for filter circuit
- Power supply / batteries

## Software Requirements
Arduino IDE libraries:
- `WiFi.h`
- `WiFiClientSecure.h`
- `PubSubClient.h`
- `Wire.h`
- `Adafruit_BME280.h`
- `MeMCore.h`

Python packages:
```bash
pip install paho-mqtt requests python-dotenv
