# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [7.0.0] - 2026-09-06

Adds support for the ESP-AT v2+ command set (ESP32 and newer ESP8266
firmware) alongside the existing ESP8266 AT v1.x driver, and fixes five
behavioural defects found by running the library against a real broker
over TLS. The v1.x driver keeps working unchanged; existing sketches
compile without modification.

### Added

- `EspDrvV4` — a second driver for the ESP-AT v2+ command set, in its own
  translation unit. Migration is a matter of changing the include and the
  driver type; `MQTTClient` is unaffected. Supports TLS via
  `SetSecure(enabled, authMode)`, which maps to `AT+CIPSSLCCONF`
  (`0` = no server verification, `2` = verify against the CA in
  `client_ca.0`).
- `EspDrvV4::SetTimeSource(server, timezone)` — applies `AT+CIPSNTPCFG`
  after joining WiFi and waits for `+TIME_UPDATED`. **Mandatory when
  `authMode` is 2**: certificate validity is checked against the clock, and
  a freshly booted module sits at 1 Jan 1970, so every connection attempt
  fails until the time is set. Give the server as an IP address; the
  built-in defaults are hostnames that never resolve without outbound DNS.
- `EspDrvV4::SetTime(unixTime)` — `AT+SYSTIMESTAMP` for devices with an RTC,
  as an alternative to SNTP.
- `IEspDrv` — the interface `MQTTClient` talks to, so both drivers can be
  used with the same MQTT layer. Carries `Write`, `Loop`,
  `GetClientStatus`, `TCPConnect`, `Close` and the `DataReceived` callback,
  plus the `WL_*` and `CL_*` status constants.
- `MQTTClient::GetLastConnackCode()` — the return code from the last
  CONNACK (`0` accepted, `4` bad user name or password, `5` not authorized;
  `0xFF` when no CONNACK arrived).
- Test sketches under `extras/`: `EchoLadder`, `LoopBudget`, `Heartbeat`,
  `Burst`, `TlsChurn` and `QosRedelivery`.

### Changed

- **Breaking (source-compatible):** `MQTTClient` now takes an `IEspDrv*`
  rather than an `EspDrv*`. Existing `MQTTClient client(&drv, cb);` keeps
  compiling because `EspDrv` derives from the interface.
- `MQTTClient::Connect()` now reports whether an MQTT session was
  established, not merely whether a socket is open. See Fixed below.
- Keep alive: `PINGREQ` is sent at half the Keep Alive interval and the same
  window is allowed for `PINGRESP`, so a dead link is detected at 1.0x Keep
  Alive — before the server gives up at 1.5x (MQTT-3.1.2-24). Previously the
  ping went out exactly on the interval with no dedicated response timeout.
- `MQTTClient` no longer keeps its state in static members. Ten fields
  including the QoS buffer and the message callback were shared between all
  instances; they are now per-object, with a single file-scope pointer used
  by the C callback trampoline.
- `sendPubAck()` goes through `MQTTClient::Write()` so acknowledgements
  update `lastOutActivity` and postpone the next `PINGREQ`.
- Removed the unused `lastInActivity` field.

### Fixed

- **Receive buffer growth could exhaust the heap and desynchronise the
  stream.** The driver allocated the larger buffer before releasing the old
  one, so it briefly needed both (measured on an Uno: 148 + 212 bytes, which
  failed and reset the board). The old buffer is now released first — its
  contents are discarded immediately afterwards anyway. When the allocation
  still fails, the message is drained instead of abandoned mid-frame;
  previously the remaining payload was parsed as AT responses and the
  connection was left unusable.
- **A missed `PINGRESP` had no effect.** `Loop()` cleared `isConnected`
  before calling `Disconnect()`, which returns early when not connected — so
  no DISCONNECT was sent and the socket stayed open, and the next `Loop()`
  restored `isConnected` from the socket state.
- **`Disconnect()` did not close the network connection**, contrary to
  MQTT-3.14.4-1. It now calls `Close()` after sending DISCONNECT.
- **`pingOutstanding` was never reset.** After one missed `PINGRESP` it
  stayed set for the lifetime of the program, so every later keep alive
  interval tore the session down again while `IsConnected()` still reported
  true — leaving the usual `if(!client.Loop()) Connect();` pattern spinning
  without ever reconnecting. It is now cleared in `Connect()` and
  `Disconnect()`.
- **Keep Alive values of 66 seconds and above overflowed.** `keepAlive * 1000`
  was evaluated in 16-bit arithmetic on AVR, so 66 became 464 milliseconds
  and 120 became 54464. The calculation now promotes to `unsigned long`.
- **`Login()` discarded a CONNACK that arrived early.** `connack` was reset
  after `Write()`, but `Write()` blocks and pumps `Loop()` internally, so a
  fast CONNACK was recorded and then thrown away — costing the full 10-second
  timeout. Measured against RabbitMQ over TLS, roughly two connections in
  three took 11.4 seconds instead of 1.4.
- **A rejected connection was reported as success.** `Login()` returned the
  socket state and ignored both `connack` and its return code, so wrong
  credentials produced a "connected" client. It now requires a CONNACK with
  return code 0 and closes the connection otherwise, per MQTT-3.2.2-5.

### Migration Guide

Existing sketches need no changes. To move to the v2+ command set:

```cpp
#include "EspDrvV4.h"          // misto "EspDrv.h"

EspDrvV4 drv(&espSerial);      // misto EspDrv
MQTTClient client(&drv, callback);

drv.Init(128, 256);
drv.SetSecure(true, 2);        // TLS s overenim serveru
drv.SetTimeSource("192.168.1.1");
drv.Connect(ssid, password);
```

`EspDrvV4` sends `AT+SYSSTORE=0` during `Init()`, so the module does not
persist WiFi credentials; the sketch supplies them on every boot. It also
sets the SNI from the hostname passed to `TCPConnect()` before every
connection, because `AT+CIPSSLCSNI` is cleared whenever a connection closes.

Callers that treated `Connect()` as "the socket is up" should be reviewed:
it now returns false when the broker rejects the session. Use
`GetLastConnackCode()` to tell a bad password (4) from an unauthorized user
(5) or a rejected client ID (2).

Sketches relying on more than one `MQTTClient` instance sharing state will
behave differently now that the fields are per-object. Sharing was never
intentional.

## [6.0.0] - 2026-05-07

Major release with breaking API changes and substantial behavioral
improvements based on a comprehensive review of the UART receive path and
MQTT packet parsing. Existing applications continue to compile in most
cases, but several behaviors changed — see the [Migration Guide](#migration-guide)
below.

### Added

- `EspDrv::DataIgnored(uint16_t length)` callback fired when an incoming
  message exceeds `maxAllowedDataLength` and is drained without buffering.
  Useful for telemetry and observability.
- `EspDrv::OnBusy(uint8_t count)` callback fired on every `BUSY`
  response from ESP, with the running consecutive-BUSY counter. The
  application decides what threshold (if any) warrants action. Replaces
  the previous internal `Close()` after 10 retries — driver no longer
  imposes any policy.
- `EspDrv::Init()` parameters for tuning receive behavior:
  - `maxAllowedDataLength` (default 512) — caps in-RAM buffering; larger
    messages are drained.
  - `interByteTimeoutMs` (default 1000) — inter-byte timeout for catching
    a stuck UART link quickly.
  - `fixedTimeoutReserveMs` (default 1000) — fixed reserve added to the
    cumulative timeout.
- `PER_BYTE_BUDGET_MS` compile-time constant (50 ms) used for cumulative
  timeout calculation. Conservative across all common UART speeds
  (9600 Bd through 115200 Bd).
- **Drain mode** for oversized incoming messages: the TCP connection is
  preserved, bytes are counted but not stored, and `DataIgnored` fires
  after the full message is consumed.
- API documentation in `EspDrv.h` clarifying that callbacks must not call
  EspDrv methods which issue AT commands (use a flag and act from the
  application loop).

### Changed

- **Breaking:** `EspDrv::Init()` signature now accepts up to four optional
  parameters. Existing single-argument calls continue to compile.
- **Breaking:** `MQTTClient::Subscribe()` now returns `bool` instead of
  `void`. Returns `true` on SUBACK received, `false` otherwise. Existing
  call sites that ignore the return value continue to compile.
- Receive timeout model rewritten from a per-byte 3000 ms timer to a
  **hybrid** (inter-byte + cumulative). The cumulative budget scales with
  message length: `length × PER_BYTE_BUDGET_MS + fixedTimeoutReserveMs`.
  This eliminates the patological scenario where slow drip-fed bytes
  (e.g. 1 byte every 2.9 s) could keep the driver busy indefinitely.
- `CLOSED` tag detection no longer issues `AT+CIPSTATUS` to refresh state.
  The local connection status is updated directly (TCP not connected),
  eliminating a recursive `SendCmd` call from inside `Loop()`.
- `BUSY` retry exceeded (count > 10) no longer auto-closes the connection
  inline. Instead, `OnBusy` callback fires and the application
  decides the recovery action — keeping the BUSY counter (which is reset
  by driver-internal events) in the driver where it belongs.
- `nextMsgId` resets to 0 on `Connect()` so packet IDs after reconnect
  start fresh — easier debugging.
- Static member variable definitions in `MQTTClient.cpp` no longer carry
  the redundant `static` keyword (cleaner compatibility with stricter
  toolchains).

### Fixed

- **Memory corruption:** MQTT PUBLISH packets with Remaining Length ≥ 128
  bytes were misparsed because only the first VLQ byte was read. This
  caused `uint16_t` underflow in the payload length calculation and could
  trigger an out-of-bounds `memmove` over the receive buffer. Full VLQ
  decoding (1–4 bytes per MQTT 3.1.1) is now implemented with sanity
  checks against the actual received length.
- **Stream desync risk:** Tag detection no longer runs on intermediate
  bytes during the `DATA` state. Previously, payload bytes could feed the
  ring buffer and trigger spurious `+IPD,` / `STATUS:` / `CLOSED` / `BUSY`
  matches mid-message.
- **Stream desync risk:** Invalid or unparseable `+IPD,N:` length no
  longer falls back to `lastState` (which exposed the unknown payload to
  top-level tag detection). The connection is closed via a deferred flag
  instead.
- `Close()` is now guarded against re-entry, preventing potential infinite
  recursion if the close path itself triggers another close request.
- `MQTTClient::Subscribe()` no longer returns `false` from a `void`
  function (previous undefined-behavior pattern accepted only as a GCC
  extension).
- Null checks added to user-supplied callbacks (`DataReceived`,
  `DataTimeout`, MQTT publish callback) to prevent crashes when callbacks
  are not registered.

### Migration Guide

#### Subscribe return value

`Subscribe()` now returns `bool` indicating that SUBACK was received. To
make use of it:

```cpp
if (!client.Subscribe("topic")) {
  // handle subscribe failure
}
```

Existing calls that ignore the return value continue to compile.

#### BUSY recovery

Previously the library closed the TCP connection automatically after 10
consecutive BUSY responses. Now it fires `OnBusy` on every BUSY with
the running counter, and the application decides any threshold and
recovery action. To preserve the previous behavior (close after 10):

```cpp
volatile bool closeOnBusyOverflow = false;

void HandleBusy(uint8_t count) {
  if (count > 10) closeOnBusyOverflow = true;
  // count is also available for general telemetry
}

void setup() {
  drv.OnBusy = HandleBusy;
  // ...
}

void loop() {
  if (closeOnBusyOverflow) {
    closeOnBusyOverflow = false;
    drv.Close();
  }
  client.Loop();
  // ...
}
```

#### Oversized incoming messages

Previously, messages with payload size > 512 bytes were silently rejected
in a way that could desynchronize the receive stream. Now they are drained
cleanly while the connection is preserved, and `DataIgnored(length)` fires
for telemetry. To track such events:

```cpp
void OnDataIgnored(uint16_t length) {
  // increment counter, log, etc.
}

drv.DataIgnored = OnDataIgnored;
```

To change the size limit, pass the second `Init` parameter:

```cpp
drv.Init(128, 1024);  // 128 B receive buffer, drain anything > 1024 B
```

#### Callback restriction

User-supplied callbacks (`DataReceived`, `DataTimeout`, `DataIgnored`,
`OnBusy`) are invoked synchronously from `EspDrv::Loop()`. They
**must not** call EspDrv methods that issue AT commands: `Close`, `Reset`,
`Disconnect`, `GetConnectionStatus(true)`, `GetClientStatus(true)`.
For such actions, set a flag and perform the action from your main
`loop()`.

## [5.1.2] and earlier

See git history for changes prior to this changelog.
