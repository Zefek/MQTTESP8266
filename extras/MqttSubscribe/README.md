# MqttSubscribe

Connects to WiFi and an MQTT broker, subscribes to `test/echo` with QoS 1, and prints each received message to Serial.

## Configuration

Edit at the top of `MqttSubscribe.ino`:

| Variable | Description |
|---|---|
| `ssid` | WiFi network name |
| `wifiPassword` | WiFi password |
| `mqttUrl` | MQTT broker hostname or IP |
| `mqttId` | MQTT client ID (must be unique on the broker) |
| `mqttUser` / `mqttPassword` | Broker credentials (leave empty for anonymous) |
| `subscribeTopic` | Topic to subscribe to (default `test/echo`) |

## Wiring

ESP8266 on pins 4 (RX) / 5 (TX), 57600 baud on both Arduino Serial and ESP serial.

## Testing

Publish a message from another machine:

```
mosquitto_pub -h <broker> -t test/echo -m "hello"
```

Expected output on Arduino Serial:

```
WiFi connected
MQTT connected
Subscribed to test/echo
[test/echo] hello
```

## Notes

- The callback fires from the driver's `Loop()`. Do not call `Publish`, `Subscribe`, or any `EspDrv` method that sends an AT command from inside the callback — set a flag and act from the main `loop()` instead.
- Payload is **not** null-terminated. Use the `length` parameter when reading bytes.
- Maximum payload size is bounded by the `Init` buffer size (128 bytes in this example). Larger messages will be dropped via the `DataIgnored` mechanism.
