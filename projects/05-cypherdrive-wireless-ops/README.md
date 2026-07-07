# CrowPanel CypherDrive Wireless Ops

Safe wireless-visibility console inspired by `cypher-drive`.

V1 is mock-first and passive: Wi-Fi/BLE scan cards, QR handoff state, and log
review. It deliberately omits HID payloads, captive-portal capture, and active
testing controls.

Safety boundary: this sketch does not join networks, collect credentials,
start an AP/captive portal, emit HID payloads, deauth, replay, transmit BLE
controls, or mutate any external system. The only mutation path is local demo
state on the panel when QR persistence is explicitly enabled.

## Feature Flags

Mock mode is the default. Enable one path at a time with `EXTRA_FLAGS` or raw
Arduino CLI `--build-property` defines.

| Flag | Default | What it enables | Proof limit |
|---|---:|---|---|
| `USE_WIFI_SCAN` | `0` | Arduino `WiFi.scanNetworks(... passive ...)` through the hosted ESP32-C6 path when the core/firmware supports it. No SSID/password file is read. | compile-verified until tested on the exact CrowPanel/C6 runtime |
| `USE_BLE_UART_BRIDGE` | `0` | CSV parser for a separate ESP32 BLE scanner sidecar over UART. The panel never runs a BLE stack directly. | parser compile-verified; bridge wiring/runtime needs hardware proof |
| `USE_QR_PERSISTENCE` | `0` | Stores the QR handoff URL in `Preferences` namespace `cypherdrive`, key `url`. Rejects obvious credential-like query fields. | compile-verified; persistence needs reboot proof on device |
| `USE_DISPLAY` | `0` | Mirrors the Serial state onto the shared `OpsDashboard` display UI. | compile-verified here unless uploaded and observed |

Example:

```sh
EXTRA_FLAGS="-DUSE_WIFI_SCAN=1 -DUSE_BLE_UART_BRIDGE=1 -DUSE_QR_PERSISTENCE=1" ./scripts/compile-all.sh
```

## BLE UART Bridge Assumptions

BLE advertisements come from an explicit sidecar, not the panel radio. A small
ESP32 can scan BLE and write newline-delimited CSV frames:

```text
BLE,<label>,<address>,<rssi>,<vendor>,<note>
BLE,Beacon,11:22:33:44:55:66,-72,generic,bench-smoke
```

Wiring assumptions:

- Sidecar TX goes to CrowPanel UART RX; sidecar RX is optional unless the
  sidecar firmware needs commands.
- Use 3.3 V TTL UART levels and a common ground.
- Define pins in a gitignored `config/Pins.h`:

```cpp
#define CYPHERDRIVE_BLE_UART_RX_PIN 17
#define CYPHERDRIVE_BLE_UART_TX_PIN 18
#define CYPHERDRIVE_BLE_UART_BAUD 115200
```

Those pin numbers are examples only. Avoid the display/touch pins and verify
against the exact board revision before calling the bridge hardware-proven.

## Serial Commands

- `help` / `status` / `history`
- `scan wifi` — mock Wi-Fi rows by default; passive hosted scan with `USE_WIFI_SCAN=1`
- `scan ble` — mock BLE rows by default; drains buffered UART sidecar frames with `USE_BLE_UART_BRIDGE=1`
- `bridge BLE,<label>,<addr>,<rssi>,<vendor>,<note>` — smoke-test the BLE parser from Serial when the bridge flag is enabled
- `qr set <url>` — set the QR handoff URL, volatile unless `USE_QR_PERSISTENCE=1`
- `qr show` — show the current QR handoff state
- `logs` — print local scan log state

Smoke sequence:

```text
status
scan wifi
scan ble
bridge BLE,Beacon,11:22:33:44:55:66,-72,generic,bench-smoke
qr set https://techtiff.ai/cypher-drive
qr show
logs
history
```

## Proof State

Current target proof is `compile-ready`: the code can build for the repo's
ESP32-P4 FQBN, but wireless scan, UART bridge wiring, QR reboot persistence,
and display/touch behavior are not `field-proven` until uploaded, run, and
observed on the exact CrowPanel hardware.

Use proof language precisely:

- `compile-ready` — Arduino CLI build passed.
- `uploaded` — repo upload helper flashed the board and verified the hash.
- `field-proven` — Serial output, display state, UART bridge frames, or QR
  persistence were observed on the real panel.
