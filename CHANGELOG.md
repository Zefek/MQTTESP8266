# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
