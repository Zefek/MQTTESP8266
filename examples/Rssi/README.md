# Rssi

Reads the WiFi signal strength (RSSI) of the currently associated access point via `AT+CWJAP_CUR?` and prints it to Serial every 10 seconds.

## Configuration

Edit at the top of `Rssi.ino`:

| Variable | Description |
|---|---|
| `ssid` | WiFi network name |
| `wifiPassword` | WiFi password |

## Wiring

ESP8266 module connected to Arduino pins 4 (RX) and 5 (TX) via `SoftwareSerial`. Both Arduino Serial (USB) and ESP serial run at 57600 baud.

## Expected output

```
WiFi connected
RSSI -52 dBm
RSSI -54 dBm
RSSI -51 dBm
```

A return value of `0` means RSSI is unavailable — typically because the ESP is not associated to an AP, or the firmware did not return `+CWJAP_CUR:` in the response. Negative values are normal; closer to 0 means stronger signal (typical range: -30 dBm excellent, -90 dBm marginal).

## Notes

- `GetRssi()` is synchronous and blocks for up to ~1 second per call. Do not call it from tight loops.
- Calling during incoming MQTT traffic is safe — `SendCmd` waits for the driver to reach IDLE state before issuing the command.
