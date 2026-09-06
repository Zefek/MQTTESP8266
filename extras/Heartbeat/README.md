# Heartbeat

Verifies the MQTT keep alive mechanism against a real broker. The sketch connects, then deliberately sends nothing at all, so the only thing keeping the session alive is `PINGREQ`. Afterwards it disconnects and reconnects to check that the client cleans up and recovers.

## Why an idle test

MQTT 3.1.1 (MQTT-3.1.2-24) lets the server close the connection if it receives nothing for 1.5 × Keep Alive. With `keepAliveSeconds = 10` the broker will drop a silent client after 15 seconds, so surviving a 90-second idle period is only possible if `PINGREQ` is being sent and answered.

The client pings at half the Keep Alive interval and allows the same amount of time for `PINGRESP`. A dead link is therefore detected at 1.0 × Keep Alive, comfortably before the server would give up at 1.5 ×.

## Configuration

| Variable | Description |
|---|---|
| `ssid` / `wifiPassword` | WiFi credentials |
| `mqttUrl` | Broker hostname — must match the certificate when `sslAuthMode` is 2 |
| `mqttPort` | 8883 for TLS, 1883 for plain TCP |
| `mqttId` | MQTT client ID, must be unique on the broker |
| `mqttUser` / `mqttPassword` | Broker credentials |
| `probeTopic` | Topic used for the single publish after reconnecting |
| `sslAuthMode` | `2` = verify server against CA in `client_ca.0`, `0` = no verification |
| `sntpServer` | NTP server as an IP address. Mandatory when `sslAuthMode` is 2 |
| `espRxPin` / `espTxPin` | Arduino pins wired to the ESP TX / RX |
| `keepAliveSeconds` | Keep Alive advertised in CONNECT (default 10) |
| `idleSeconds` | Length of the silent phase (default 90, i.e. 9 × Keep Alive) |

## Output

```
connect OK
keepAlive=10s, ping ocekavan kazdych 5s, server odpojuje na 15s ticha
faze 1: necinnost
  t=10s alive=ano
  ...
  t=90s alive=ano
faze 1 OK: prezilo necinnost bez odpojeni
faze 2: disconnect a reconnect
  po Disconnect je socket zavreny OK
  reconnect OK
  publish po reconnectu OK
--- HOTOVO ---
```

## What each phase proves

**Phase 1** — the keep alive timer fires, `PINGREQ` reaches the broker and `PINGRESP` comes back. Any `alive=NE` before `idleSeconds` elapses means the broker dropped the session, so pings are not going out.

**Phase 2** — `Disconnect()` sends DISCONNECT *and* closes the socket, as MQTT-3.14.4-1 requires. The check reads `IsConnected()` afterwards, which reflects the driver's socket state rather than a local flag, so a client that sent DISCONNECT but left the socket open fails here. The reconnect then proves the ping state is reset on `Connect()`; a client that leaves a `PINGREQ` marked outstanding would tear the new session down at the first keep alive interval.

## Notes

- Raise `keepAliveSeconds` to test longer intervals, but keep in mind that `idleSeconds` should stay well above 1.5 × Keep Alive or the test proves nothing.
- Values above 65 used to overflow the interval arithmetic on AVR, where `int` is 16 bits and `keepAlive * 1000` wrapped. The calculation now promotes to `unsigned long` first.
- The sketch never publishes during phase 1 on purpose. Any outgoing packet — including a QoS 1 `PUBACK` — postpones the next `PINGREQ`, which would mask a broken timer.
- A Keep Alive of 0 disables the mechanism entirely. That is legal, but it also means the client has no protocol-level way to notice a half-open connection.
