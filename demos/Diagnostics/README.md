# Diagnostics

Publishes a snapshot of driver-level health metrics to an MQTT telemetry topic every 30 seconds. Useful for monitoring deployed devices remotely.

## Configuration

Edit at the top of `Diagnostics.ino`:

| Variable | Description |
|---|---|
| `ssid` | WiFi network name |
| `wifiPassword` | WiFi password |
| `mqttUrl` | MQTT broker hostname or IP |
| `mqttId` | MQTT client ID (must be unique on the broker) |
| `mqttUser` / `mqttPassword` | Broker credentials (leave empty for anonymous) |
| `telemetryTopic` | Topic to publish telemetry on (default `telemetry/diag`) |

`reportInterval` (default 30000 ms) controls publish cadence.

## Wiring

ESP8266 on pins 4 (RX) / 5 (TX), 57600 baud on both Arduino Serial and ESP serial.

## Payload format

CSV key=value pairs in a single line:

```
rssi=-58,memFail=0,tagFail=0,uptime=1234
```

| Field | Source | Meaning |
|---|---|---|
| `rssi` | `EspDrv::GetRssi()` | WiFi signal in dBm. `0` = unavailable, otherwise negative (closer to 0 is stronger). |
| `memFail` | `EspDrv::GetMemAllocFailCount()` | Cumulative count of failed `+IPD` buffer allocations. Non-zero indicates memory pressure on the Arduino. Resets to 0 on first successful allocation after a failure streak. |
| `tagFail` | `EspDrv::GetTagRecognitionFailCount()` | Cumulative count of AT commands that did not produce the expected response tag. Non-zero suggests link timing or firmware issues. Resets to 0 on first successful command after a failure streak. |
| `uptime` | `millis() / 1000` | Seconds since Arduino boot. |

## Subscribing

```
mosquitto_sub -h <broker> -t telemetry/diag
```

## Notes

- The example assumes the MQTT connection is already established (`setup()` connects once). For long-running deployments combine with the `Reconnect` pattern.
- The fail counters are **streak indicators**, not lifetime totals — they reset on success. A non-zero value means a recent problem; rapidly oscillating values indicate ongoing instability.
- `GetRssi()` blocks for up to ~1 second. The 30-second interval is generous; do not shrink below a few seconds.
