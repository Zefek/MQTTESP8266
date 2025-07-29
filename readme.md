# ESP8266 MQTT Client Library

This repository provides a lightweight MQTT client implementation for Arduino-compatible microcontrollers using an ESP8266 module for WiFi connectivity. The code is suitable for resource-constrained devices and supports basic MQTT publish/subscribe operations.

## Features

- Connects to WiFi using an ESP8266 module.
- Implements MQTT 3.1/3.1.1 client (configurable).
- Supports MQTT publish, subscribe, and basic session management.
- Handles reconnections and reliability testing.
- Includes example usage for both sending and receiving messages.
- Low-level ESP8266 AT command driver for robust serial communication.

## File Overview

- **src.ino**  
  Example Arduino sketch demonstrating usage. Handles WiFi and MQTT client setup, connection logic, message publishing, and reception. Contains two test modes:
  - *RELIABILITY_TEST*: Publishes messages with time tracking and reliability calculation.
  - *RECEIVE_TEST*: Subscribes and counts received messages.

- **EspDrv.h / EspDrv.cpp**  
  ESP8266 serial driver. Handles AT command communication, connection management, TCP setup, buffer management, and robust parsing of ESP responses. Provides methods for connecting to WiFi, opening/closing TCP sockets, sending/receiving data, and status tracking.

- **MQTTClient.h / MQTTClient.cpp**  
  MQTT protocol client built on top of `EspDrv`. Implements core MQTT features such as CONNECT, PUBLISH, SUBSCRIBE, PING, and DISCONNECT. Handles keep-alive, QoS0/1, session flags, and message parsing. Allows registration of message-received callbacks.

## Hardware Requirements

- Arduino-compatible microcontroller (e.g., Uno, Nano, Mega).
- ESP8266 WiFi module (using AT firmware).
- Level shifter (if required for 3.3V logic).
- Connect ESP8266 RX/TX to Arduino TX/RX (can use SoftwareSerial).

## Software Requirements

- Arduino IDE
- `SoftwareSerial` library (included by default with Arduino)
- This repository's files

## Getting Started

1. **Wiring**
   - Connect ESP8266 to the Arduino using the correct TX/RX pins.
   - Ensure the module is powered with 3.3V and proper GND.

2. **Configuration**
   - Open `src.ino` in Arduino IDE.
   - Set your WiFi credentials, MQTT broker address, and optional MQTT user/pass:
     ```cpp
     char* ssid = "YOUR_WIFI_SSID";
     char* wifiPassword = "YOUR_WIFI_PASSWORD";
     char* mqttUrl = "broker.example.com";
     char* mqttUser = "username";
     char* mqttPassword = "password";
     char* mqttId = "arduino-client-1";
     ```
   - Choose test mode by setting `#define RELIABILITY_TEST` or `#define RECEIVE_TEST` to 1 as needed.

3. **Upload**
   - Upload the sketch to your Arduino.
   - Open Serial Monitor to view debug output and connection status.

## Usage Example

- To publish 1000 test messages and track average delivery time and reliability:
  - Set `#define RELIABILITY_TEST 1` and `#define RECEIVE_TEST 0`.
  - Monitor Serial output for statistics after each message.

- To count received messages as a subscriber:
  - Set `#define RELIABILITY_TEST 0` and `#define RECEIVE_TEST 1`.
  - Monitor Serial output for incoming message count.

## Key Classes and Functions

- `EspDrv`: Handles all AT command communication with ESP8266.
- `MQTTClient`: Handles MQTT packet formatting, state machine, and protocol logic.
- `Connect()`: Manages reconnections and WiFi/MQTT state.
- `MQTTMessageReceive()`: Callback invoked on incoming MQTT messages.

## Advanced Notes

- Buffer sizes and timeouts are configurable in the headers.
- The driver prints debug, warning, and error messages to Serial (can be toggled in the code).
- The implementation uses only static memory allocation for reliability except where dynamic resizing is required for incoming packets.
- Minimal external dependencies; all logic is contained in the files provided.

## Troubleshooting

- Ensure your ESP8266 is running AT firmware.
- Double-check serial wiring and baud rates.
- Use Serial Monitor for real-time debug output.
- If connection issues persist, increase timeouts or check MQTT broker settings.

## License

MIT License

---

**Author:** Petr B.  
**Contributions:** Welcome via pull requests and issues!
