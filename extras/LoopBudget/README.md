# LoopBudget

Measures how long the application may block between `Loop()` calls before the driver starts losing incoming bytes. This is the number that decides whether a given sketch can keep up at a given baud rate.

The sketch echoes fixed-size messages through the broker, exactly like `EchoLadder`, but holds the payload size constant and instead ramps an artificial `delay()` inside `loop()`. It reports the block time at which the first loss appears.

## Why this number matters

Incoming bytes are buffered by the serial port and drained by `EspDrv::Loop()`. If the application stays away longer than the buffer holds, bytes are dropped before the driver ever sees them. The buffer is 64 bytes on both `HardwareSerial` and `SoftwareSerial`:

| Baud | Time per byte | 64-byte buffer |
|---|---|---|
| 9600 | 1042 µs | 66.7 ms |
| 57600 | 174 µs | 11.1 ms |
| 115200 | 87 µs | 5.6 ms |

So at 57600 the theoretical budget is about 11 ms. The measured value is normally lower, because the driver needs several `Loop()` passes to consume a message and other interrupts compete for time.

## Configuration

| Variable | Description |
|---|---|
| `ssid` / `wifiPassword` | WiFi credentials |
| `mqttUrl` | Broker hostname — must match the certificate when `sslAuthMode` is 2 |
| `mqttPort` | 8883 for TLS, 1883 for plain TCP |
| `mqttId` | MQTT client ID, must be unique on the broker |
| `budgetTopic` | Topic published to and subscribed from |
| `sslAuthMode` | `2` = verify server against CA in `client_ca.0`, `0` = no verification |
| `sntpServer` | NTP server **as an IP address**. Mandatory when `sslAuthMode` is 2 |
| `sntpTimezone` | Offset passed to `AT+CIPSNTPCFG`; irrelevant to certificate checking |
| `espRxPin` / `espTxPin` | Arduino pins wired to the ESP TX / RX |
| `payloadSize` | Bytes per message, held constant (default 32) |
| `messagesPerStep` | Messages per block-time step (default 10) |
| `blockStepMs` | Increment per step (default 1 ms) |
| `maxBlockMs` | Stop here if nothing was ever lost (default 40 ms) |

## Output

One line per step, then a verdict:

```
block=0ms recv=10/10 lost=0
block=1ms recv=10/10 lost=0
...
block=9ms recv=10/10 lost=0
block=10ms recv=8/10 lost=2
first loss at block=10ms
```

## What this actually measures

The artificial delay is applied while waiting for an echo, and during that window the sketch calls `EspDrv::Loop()` directly rather than `MQTTClient::Loop()`. That distinction matters: `MQTTClient::Loop()` calls `IsConnected()`, which issues `AT+CIPSTATUS`. On a half-duplex link such as `SoftwareSerial` the board cannot receive while it transmits, so a tight polling loop discards the very messages it is waiting for. Measured on an Uno at 57600: with `MQTTClient::Loop()` in the wait window, 7 of 10 messages arrived at `block=0` and — counterintuitively — 10 of 10 at `block>=4ms`, because the delay reduced the sketch's own traffic. With `EspDrv::Loop()` the loss disappears entirely.

A single small message survives an arbitrarily long block, because it simply waits in the serial buffer. A 16-byte payload is about 45 bytes on the wire and the buffer holds 64. So this scenario finds an edge only when a message does not fit, which is what `EchoLadder` measures, or when several messages arrive inside one block window, which needs a burst scenario. On a link where every message fits, expect `no sustained loss` — that is a pass, not a failure to measure.

## Interpreting results

- **The verdict is your latency budget.** Any `loop()` iteration in real firmware that can exceed it will drop messages. Compare it against the worst-case blocking path in your application.
- **A budget far below the table above** means something else is eating time — usually another library disabling interrupts. `SoftwareSerial` itself disables interrupts for the duration of each received byte, so a second `SoftwareSerial` instance or a bit-banged sensor will show up here.
- **No loss up to `maxBlockMs`** means the link is comfortable. Raise `maxBlockMs`, raise `payloadSize`, or move to a higher baud rate to find the real edge.
- **Losses at `block=0`** mean the problem is not latency at all. Run `EchoLadder` to check whether it is size-related.

## Time is mandatory for certificate verification

With `sslAuthMode = 2` the ESP checks the certificate's validity dates. A freshly booted ESP-AT module sits at `Thu Jan 1 00:00:00 1970`, so every connection attempt fails until the clock is set. `EspDrvV4::Connect()` applies `sntpServer` right after joining WiFi and waits for `+TIME_UPDATED`.

Give the server as an IP address. The built-in defaults are Chinese hostnames, and on a network without outbound DNS they never resolve, so no NTP packet is ever sent. A router that redirects UDP/123 will answer whatever address you use.

Symptom when this is wrong: `TCPConnect` fails in under half a second. A genuine handshake attempt takes upwards of a second. Devices with an RTC can call `EspDrvV4::SetTime()` after `Connect()` instead of using SNTP.

## Notes

- The artificial delay is applied while waiting for an echo, which is exactly when incoming bytes are in flight. Blocking at other points in the cycle would not be representative.
- `delay()` is used deliberately rather than a busy loop, to model an application that blocks on a sensor read or a display write.
- Results are printed between steps, never during a measurement window — `Serial` output during reception would itself cause losses.
- On an Uno with `SoftwareSerial` at 57600 the payload must stay at or below 32 bytes. Measured with `EchoLadder`: 32 bytes arrive reliably, 64 bytes never do. The MQTT PUBLISH wrapper adds 21 bytes and the AT layer adds `+IPD,NN:`, so a 32-byte payload already fills 61 of the 64 bytes `SoftwareSerial` can buffer. A larger payload here measures that ceiling, not the latency budget. Boards with a spare hardware UART (Mega, Leonardo) have no such limit.
- `maxAllowedDataLength` is set to 256; payloads here are 64 bytes plus MQTT overhead, so nothing is drained. If you raise `payloadSize` substantially, raise this too or the messages will be discarded and counted as losses.
