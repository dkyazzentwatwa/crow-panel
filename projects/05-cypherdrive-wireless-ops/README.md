# CrowPanel CypherDrive Wireless Ops

Safe wireless-visibility console inspired by `cypher-drive`.

V1 is mock-first and passive: Wi-Fi/BLE scan cards, QR handoff state, and log
review. It deliberately omits HID payloads, captive-portal capture, and active
testing controls.

## Serial Commands

- `help` / `status` / `history`
- `scan wifi` — load mock Wi-Fi networks
- `scan ble` — load mock BLE devices
- `qr set <url>` — set the QR handoff URL
- `qr show` — show the current QR handoff state
- `logs` — show recent mock session logs

## Proof State

Scaffolded and compile-ready only until uploaded and observed on a real
CrowPanel.
