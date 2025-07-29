# Additional Information for Developers

To ensure smooth integration and extension of this MQTT ESP8266 client library, please review the following important points:

---

## 1. Supported Hardware & Compatibility

- **Tested Arduino Boards:** Uno, Mega, Nano (others may work but are untested).
- **ESP8266 Firmware:** Requires AT command firmware (tested with v1.7.x and newer).
- **Voltage Levels:** ESP8266 operates at 3.3V. Use a logic level shifter if your Arduino board is 5V.

---

## 2. Dependencies

- **Libraries:**  
  - [SoftwareSerial](https://www.arduino.cc/en/Reference/softwareSerial) (standard Arduino library)
- **Arduino IDE:**  
  - Version 1.8.x or newer recommended.

---

## 3. Pinout Diagram

| Arduino Pin | ESP8266 Pin | Note                        |
|-------------|-------------|-----------------------------|
| TX (D1)     | RX          | Use a voltage divider if 5V |
| RX (D0)     | TX          |                             |
| 3.3V        | VCC         | Do not use 5V!              |
| GND         | GND         |                             |

- Cross-connect TX/RX.
- Ensure stable 3.3V power supply for ESP8266.

---

## 4. Example MQTT Broker Setup

- **Public Broker:**  
  - [Eclipse Mosquitto Test Broker](https://test.mosquitto.org)
  - `mqtt.eclipseprojects.io`
- **Local Broker:**  
  - Install [Mosquitto](https://mosquitto.org/download/) on your PC or Raspberry Pi.
- **Example Topic:**  
  - `test/echo`
- **Example Payload:**  
  - `"Hello from Arduino!"`

---

## 5. Extending or Customizing the Code

- **Custom Callback:**  
  Define your own `void MQTTMessageReceive(char* topic, uint8_t* payload, uint16_t length)` function and pass it to the `MQTTClient` constructor.
- **Adding Features:**  
  - Add support for more MQTT features (such as more QoS levels, TLS, or persistent sessions) by extending `MQTTClient`.
  - Increase buffer sizes in the headers if you need to handle larger payloads.

---

## 6. Limitations

- **Buffer Size:**  
  Limited to 128 or 256 bytes by default; large MQTT messages will be truncated.
- **QoS Support:**  
  QoS 0 and 1 only; QoS 2 is not implemented.
- **No TLS:**  
  Does not support encrypted MQTT connections.
- **Single Connection:**  
  Only one MQTT connection at a time.
- **No Persistent Sessions:**  
  Session data is not preserved across reconnects.

---

## 7. Troubleshooting & Debugging

- **Serial Output:**  
  Use the Arduino Serial Monitor for debug messages.
- **Common Issues:**  
  - "Small buffer size" message: Increase `bufferSize` in `MQTTClient.h`.
  - "Not connected" message: Check WiFi credentials and broker status.
- **AT Command Errors:**  
  - Ensure ESP8266 is running AT firmware, not NodeMCU or custom firmware.
  - Try resetting the module and checking wiring.
- **Debug Output:**  
  Change debug level macros in `EspDrv.cpp` for more verbose logging.

---

## 8. Contribution Guide

- **Pull Requests:**  
  Fork, branch, and submit PRs for new features or bug fixes.
- **Coding Style:**  
  Follow existing formatting and comment style for consistency.
- **Issues:**  
  Use GitHub Issues for bug reports or feature requests.

---