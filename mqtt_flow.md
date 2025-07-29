# MQTT Client Flow Description

This document describes the MQTT client flow based on the provided implementation:

- `MQTTClient.h` / `MQTTClient.cpp`
- `EspDrv.h` / `EspDrv.cpp`
- `src.ino`

## 1. Connection Workflow

### 1.1. WiFi Connection

- The Arduino connects to WiFi using the `EspDrv` class which sends AT commands to the ESP8266 module.
- The `Connect()` function in `src.ino` checks WiFi status and reconnects if necessary via `drv.Connect(ssid, wifiPassword)`.

### 1.2. MQTT Connection

- The `MQTTClient` class is constructed by passing an `EspDrv` driver and a message receive callback.
- To establish an MQTT connection:
  1. `MQTTClient::Connect(MQTTConnectData)` is called.
  2. This opens a TCP connection to the broker with `EspDrv::TCPConnect`.
  3. Then, `MQTTClient::Login(MQTTConnectData)` formats and sends the MQTT CONNECT packet.
  4. The client waits for a CONNACK from the broker (with a timeout).

#### Clean Session and Credentials

- Clean session is supported and configurable via `MQTTConnectData`.
- Will message, username, and password are supported and included if provided.

## 2. Message Publishing

- To publish a message, use `MQTTClient::Publish(topic, payload)`.
- The function supports both:
  - `Publish(const char*, const char*)` – for string payloads
  - `Publish(const char*, const uint8_t*, unsigned int)` – for binary payloads

- The MQTT packet is constructed and sent via the underlying `EspDrv` driver.
- The implementation supports optional "retained" flag.

### QoS Support

- **Supported QoS:**  
  - **QoS 0 (At most once)**: Supported by default.
  - **QoS 1 (At least once)**: Supported for subscriptions (SUBSCRIBE uses QoS 1). For publishing, the code path defaults to QoS 0 (no packet ID is assigned, no PUBACK handling for outgoing messages).
  - **QoS 2 (Exactly once):** Not supported.

- When subscribing, the QoS can be set to 0 or 1.
- When publishing, QoS is effectively 0 (no retransmission or PUBACK required for outgoing messages).

## 3. Subscribing and Reading Messages

- To subscribe, call `MQTTClient::Subscribe(topic, qos)` (default QoS is 0, but 1 is allowed).
- Upon receiving a PUBLISH packet from the broker:
  1. The `DataReceived` static handler parses the MQTT packet.
  2. It extracts the topic and payload, then invokes the user-provided callback (e.g., `MQTTMessageReceive` in `src.ino`).
  3. For QoS 1, the Packet Identifier is extracted and PUBACK is sent (see below).

## 4. Message Acknowledgment

- If a received message has QoS 1, the client must acknowledge with a PUBACK packet.
- In the main loop (`MQTTClient::Loop()`), if a packet ID is set, a PUBACK is sent via `sendPubAck(packetId)`, then the packet ID is cleared.

## 5. Keep-Alive and Ping

- The client automatically manages keep-alive using the interval set in `MQTTConnectData`.
- If the keep-alive interval elapses without data, a PINGREQ is sent.
- If a PINGRESP is not received, the connection is considered lost and reconnect logic is triggered.

## 6. Reconnection Logic

- If WiFi or MQTT connection drops, the `Connect()` function in `src.ino` handles reconnection attempts with exponential backoff.

---

## Example Flow Diagram

1. **Startup:**
   - Initialize ESP8266 and serial, connect to WiFi.

2. **MQTT Connect:**
   - Open TCP connection to broker.
   - Send MQTT CONNECT.
   - Wait for CONNACK.

3. **Publish/Subscribe:**
   - Subscribe to topic(s).
   - Publish messages using `Publish()`.
   - Receive messages via callback.

4. **On Incoming Message:**
   - Parse PUBLISH.
   - If QoS 1, send PUBACK.

5. **Keep-Alive:**
   - Periodically send PINGREQ.
   - Wait for PINGRESP.

6. **Reconnection:**
   - On disconnect, retry WiFi/MQTT connection.

---

## Summary Table

| Feature         | Supported | Notes                    |
|-----------------|-----------|--------------------------|
| MQTT Version    | 3.1, 3.1.1| Selectable via macro     |
| QoS 0           | Yes       | Default for publishing   |
| QoS 1           | Yes       | For subscriptions, incoming messages are acknowledged with PUBACK |
| QoS 2           | No        | Not implemented          |
| Retained        | Yes       | Optional on publish      |
| Will Message    | Yes       | Via `MQTTConnectData`    |
| Username/Pass   | Yes       | Via `MQTTConnectData`    |
| Clean Session   | Yes       | Via `MQTTConnectData`    |
| Keep-Alive      | Yes       | Configurable, auto-ping  |

---

**For more details or usage examples, see the main README or `src.ino`.**