# Reconnect

Demonstrates a resilient WiFi + MQTT reconnection pattern with exponential backoff. The sketch automatically recovers from WiFi outages, broker restarts, and TCP disconnects without manual intervention.

## Configuration

Edit at the top of `Reconnect.ino`:

| Variable | Description |
|---|---|
| `ssid` | WiFi network name |
| `wifiPassword` | WiFi password |
| `mqttUrl` | MQTT broker hostname or IP |
| `mqttId` | MQTT client ID (must be unique on the broker) |
| `mqttUser` / `mqttPassword` | Broker credentials (leave empty for anonymous) |

`maxBackoff` (default 300000 ms = 5 minutes) caps the retry interval.

## Wiring

ESP8266 on pins 4 (RX) / 5 (TX), 57600 baud on both Arduino Serial and ESP serial.

## Backoff behavior

- **WiFi disconnected**: retry every 5–30 seconds, doubling on each failure, capped at `maxBackoff`.
- **WiFi OK, MQTT down**: retry immediately on success, otherwise back off 0–5 seconds doubling, capped at `maxBackoff`.
- **Both up**: no retries — `Connect()` is a no-op until something fails.

The backoff timer resets to `0` on successful MQTT connect, so a transient outage doesn't permanently slow down subsequent reconnects.

## Expected output

Normal operation (broker reachable):

```
WiFi reconnect
MQTT reconnect
```

After a WiFi drop:

```
WiFi reconnect
WiFi reconnect
WiFi reconnect
MQTT reconnect
```

Backoff between retries grows visibly during prolonged outages.

## Notes

- `client.Loop()` returning `false` is the trigger for reconnect. It returns `false` when MQTT is not connected or a protocol error occurred.
- The pattern is extracted from `src.ino` — drop it into your own sketch and add your `Publish` / `Subscribe` logic on top.
