# EchoLadder

Finds the message size at which the receive path starts losing data. The sketch publishes to a topic it is itself subscribed to, so the broker echoes every message back — no external publisher or PC-side script is needed.

For each size in the ladder it sends `messagesPerSize` messages one at a time, waits for the echo, and records round-trip time. Losses show up as timeouts.

## Configuration

Edit at the top of `EchoLadder.ino`:

| Variable | Description |
|---|---|
| `ssid` / `wifiPassword` | WiFi credentials |
| `mqttUrl` | Broker hostname — must match the certificate when `sslAuthMode` is 2 |
| `mqttPort` | 8883 for TLS, 1883 for plain TCP |
| `mqttId` | MQTT client ID, must be unique on the broker |
| `mqttUser` / `mqttPassword` | Broker credentials |
| `echoTopic` | Topic published to and subscribed from |
| `sslAuthMode` | `2` = verify server against CA in `client_ca.0`, `0` = no verification |
| `sntpServer` | NTP server **as an IP address**. Mandatory when `sslAuthMode` is 2 |
| `sntpTimezone` | Offset passed to `AT+CIPSNTPCFG`; irrelevant to certificate checking |
| `espRxPin` / `espTxPin` | Arduino pins wired to the ESP TX / RX |
| `messagesPerSize` | Messages per rung (default 20) |
| `echoTimeout` | How long to wait for an echo before counting a loss (default 3000 ms) |

The ladder itself is the `sizes[]` array, default `16, 32, 64, 96, 128, 192`.

## Wiring

ESP32 running ESP-AT on pins 4 (RX) / 5 (TX), 57600 baud on both Arduino Serial and ESP serial. The ESP-AT UART must be provisioned to 57600 — see the note on baud rate below.

## Output

One line per rung, printed only in the quiet gap after that rung finishes:

```
size=16 recv=20/20 lost=0 rtt=41/58/97 ignored=0
size=192 recv=19/20 lost=1 rtt=44/71/210 ignored=0
```

| Field | Meaning |
|---|---|
| `size` | Payload bytes published |
| `recv` / `lost` | Echoes matched vs. timed out |
| `rtt` | Round-trip min/avg/max in ms |
| `ignored` | `DataIgnored` callbacks during the rung — message exceeded `maxAllowedDataLength` and was drained |

## Interpreting results

- **Losses that begin at one rung and continue on every larger rung** point at a size limit — either `maxAllowedDataLength` (check `ignored`) or the UART buffer.
- **Losses scattered across all rungs** point at timing, not size. Run `LoopBudget` instead.
- **`ignored` non-zero** means the message was larger than `maxAllowedDataLength` and drained deliberately. This is not a fault; it is drain mode working.
- **Rising `max` rtt with a stable `min`** suggests the broker or the link is queueing, not the Arduino.

## Time is mandatory for certificate verification

With `sslAuthMode = 2` the ESP checks the certificate's validity dates. A freshly booted ESP-AT module sits at `Thu Jan 1 00:00:00 1970`, so every connection attempt fails until the clock is set. `EspDrvV4::Connect()` applies `sntpServer` right after joining WiFi and waits for `+TIME_UPDATED`.

Give the server as an IP address. The built-in defaults are Chinese hostnames, and on a network without outbound DNS they never resolve, so no NTP packet is ever sent. A router that redirects UDP/123 will answer whatever address you use.

Symptom when this is wrong: `TCPConnect` fails in under half a second. A genuine handshake attempt takes upwards of a second. Devices with an RTC can call `EspDrvV4::SetTime()` after `Connect()` instead of using SNTP.

## Notes

- `MQTTClient` publishes through a fixed 256-byte buffer, so payloads above roughly 250 bytes cannot be sent at all. The ladder stops at 192 for that reason. To test the drain path with larger messages, publish externally:
  ```
  mosquitto_pub -h <broker> -t test/echoladder -s < bigfile
  ```
- `maxAllowedDataLength` is set to 320 rather than the 512 default. On an Uno the static footprint of this sketch is already ~46% of RAM; allowing a 512-byte receive buffer on top of the 256-byte MQTT buffer would leave too little stack.
- Results are buffered and printed between rungs on purpose. Printing to `Serial` while `SoftwareSerial` is receiving causes exactly the losses this sketch measures.
- The sketch publishes to a topic it subscribes to. Use a topic no other client writes to, or foreign messages will be counted as mismatched echoes and ignored.
