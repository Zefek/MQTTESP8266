# MqttPublish

Connects to WiFi and an MQTT broker, then publishes an incrementing counter to `test/counter` every 5 seconds.

## Configuration

Edit at the top of `MqttPublish.ino`:

| Variable | Description |
|---|---|
| `ssid` | WiFi network name |
| `wifiPassword` | WiFi password |
| `mqttUrl` | MQTT broker hostname or IP |
| `mqttId` | MQTT client ID (must be unique on the broker) |
| `mqttUser` / `mqttPassword` | Broker credentials (leave empty for anonymous) |

Broker port is hardcoded to `1883` (plain TCP). Keep-alive is `60` seconds.

## Wiring

ESP8266 on pins 4 (RX) / 5 (TX), 57600 baud on both Arduino Serial and ESP serial.

## Expected output

On Arduino Serial:

```
WiFi connected
MQTT connected
Published 0
Published 1
Published 2
```

On the broker, subscribe to `test/counter` (e.g. `mosquitto_sub -h <broker> -t test/counter`) — you should see the same numeric stream.

## Notes

- A no-op callback is registered because `MQTTClient` constructor requires one even for publish-only use.
- `Publish` returns `false` if the underlying TCP write fails — the example only logs success. For production add reconnect logic (see the `Reconnect` example).
- Default QoS is 0 (fire-and-forget). No delivery guarantee.
