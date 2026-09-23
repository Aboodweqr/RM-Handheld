# BLE protocol v1

Service UUID: `7f510000-1b15-4e6e-9a6b-44524d48444c`

Telemetry characteristic: `7f510001-1b15-4e6e-9a6b-44524d48444c`

The characteristic accepts Write and Write Without Response. Every packet is
exactly 20 bytes, which fits the 20-byte payload of the default BLE ATT MTU.

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 1 | magic `0xA5` |
| 1 | 1 | protocol version `1` |
| 2 | 1 | frame kind |
| 3 | 1 | rolling sequence number |
| 4 | 14 | kind-specific little-endian payload |
| 18 | 2 | CRC-16/CCITT-FALSE, little-endian |

Frame kinds are `1` core, `2` performance, `3` network, `4` link, and `5`
identity. Identity carries the connected Wi-Fi name, sanitized and truncated to
13 display-safe characters. `0xFF`,
`0xFFFF`, and signed `INT16_MIN` are unknown-value sentinels. Flags tell the UI
whether a value is real, so a valid zero does not get confused with unavailable.

The ESP32 also exposes the standard Bluetooth HID service (`0x1812`) with one
gamepad input report: 16 buttons, one hat, four signed 16-bit axes, and two
unsigned 10-bit triggers.
