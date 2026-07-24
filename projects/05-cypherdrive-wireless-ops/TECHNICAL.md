# CrowPanel CypherDrive Wireless Ops Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at projects/05-cypherdrive-wireless-ops.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in projects/05-cypherdrive-wireless-ops/TECHNICAL.md.
```

---

Safe wireless-visibility console inspired by `cypher-drive`.

Mock-first and passive: a touch-first, four-screen dashboard (Wi-Fi / BLE / Log /
QR) drawn on the 1024x600 DSI panel with the shared `CrowDisplay` bring-up and
`Widgets` toolkit (dark "ops" palette, FreeSans fonts, cards / signal bars /
pills). It deliberately omits HID payloads, captive-portal capture, and active
testing controls.

Safety boundary: this sketch does not join networks, collect credentials,
start an AP/captive portal, emit HID payloads, deauth, replay, transmit BLE
controls, or mutate any external system. The only mutation path is local demo
state on the panel when QR persistence is explicitly enabled. A persistent
**PASSIVE - RECEIVE ONLY** banner sits in the header chrome on every screen.

## UI

Owned entirely by `src/WirelessOpsUi.{h,cpp}` (the generic `OpsDashboard` is
gone). A `WirelessScreen` enum drives four tabbed screens with a `dirty_` flag
so the panel repaints only when state changes, then one `CrowDisplay::flush()`
per frame (built with `manualFlush=true`). Touch is the shared debounced
`CrowTouch`; navigation keys off `releasedEdge()` + `releaseX/Y()`, so a drag
that starts on one control and ends on another fires nothing.

`tick()` returns a typed `WirelessAction` (`WACT_SCAN_WIFI` / `WACT_SCAN_BLE` /
`WACT_NONE`) that the sketch executes; the UI never mutates application state
itself. Screen switches, the Wi-Fi inspector, and log paging are presentational
and handled inside the UI.

| Screen | Contents |
|---|---|
| **Wi-Fi** | Network cards: `signalBars()` RSSI + dBm, channel, band (2.4/5 GHz), security pill (green WPA2/3 · amber WPA · red open/WEP). Tap a card to inspect. |
| **Wi-Fi detail** | One network: channel / band / security / SSID fields, a signal arc-gauge, and a `< NETWORKS` back button. |
| **BLE** | Advertisement cards from the UART sidecar: label, address, vendor pill, RSSI. Parser only. |
| **Log** | The `ScanLog` ring buffer, 6 rows/page, newest first, with type chips and timestamps. |
| **QR** | The handoff URL as a large readable block, a decorative QR placeholder, and a SAVED/VOLATILE persistence pill. |

### Touch controls

| Screen | Control | Action | Serial equivalent |
|---|---|---|---|
| any | bottom tab (WI-FI/BLE/LOG/QR) | switch screen | `screen <name>` |
| Wi-Fi | `RESCAN` button | run a passive Wi-Fi scan | `scan wifi` |
| Wi-Fi | network card | open the inspector | `net <n>` |
| Wi-Fi detail | `< NETWORKS` button | back to the list | `screen wifi` |
| BLE | `REFRESH` button | drain/scan BLE adverts | `scan ble` |
| Log | `PREV` / `NEXT` buttons | page the log | `page prev` / `page next` |

## Feature Flags

Mock mode is the default. Enable one path at a time with `EXTRA_FLAGS` or raw
Arduino CLI `--build-property` defines.

| Flag | Default | What it enables | Proof limit |
|---|---:|---|---|
| `USE_WIFI_SCAN` | `0` | Arduino `WiFi.scanNetworks(... passive ...)` through the hosted ESP32-C6 path when the core/firmware supports it. No SSID/password file is read. | compile-verified until tested on the exact CrowPanel/C6 runtime |
| `USE_BLE_UART_BRIDGE` | `0` | CSV parser for a separate ESP32 BLE scanner sidecar over UART. The panel never runs a BLE stack directly. | parser compile-verified; bridge wiring/runtime needs hardware proof |
| `USE_QR_PERSISTENCE` | `0` | Stores the QR handoff URL in `Preferences` namespace `cypherdrive`, key `url`. Rejects obvious credential-like query fields. | compile-verified; persistence needs reboot proof on device |
| `USE_DISPLAY` | `0` | The bespoke `WirelessOpsUi` touch dashboard (Wi-Fi/BLE/Log/QR). Without it the project is Serial-only with identical behavior. | compile-verified here unless uploaded and observed |

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

115200 baud, line ending Newline. Every touch action has a Serial equivalent, so
the whole tool drives headless.

- `help` / `status` / `history` — shared (`status` also reports the current screen)
- `scan wifi` — mock Wi-Fi rows by default; passive hosted scan with `USE_WIFI_SCAN=1` (switches to the Wi-Fi screen)
- `scan ble` — mock BLE rows by default; drains buffered UART sidecar frames with `USE_BLE_UART_BRIDGE=1` (switches to the BLE screen)
- `bridge BLE,<label>,<addr>,<rssi>,<vendor>,<note>` — smoke-test the BLE parser from Serial when the bridge flag is enabled; the frame is prepended to the BLE list
- `qr set <url>` — set the QR handoff URL, volatile unless `USE_QR_PERSISTENCE=1`
- `qr show` — show the current QR handoff state
- `logs` — print local scan log state
- `screen wifi|ble|log|qr` — switch the on-panel screen (mirrors the tab bar)
- `net <n>` — inspect Wi-Fi row `n` (mirrors tapping a card)
- `page next|prev|<n>` — page the scan log
- `touch` — print raw + mapped touch coordinates, tap count, and the current screen
- `selftest` — drive the mock flow end-to-end headlessly and print PASS/FAIL lines plus a final summary (the functional check with no panel attached)

Smoke sequence:

```text
status
scan wifi
net 2
scan ble
bridge BLE,Beacon,11:22:33:44:55:66,-72,generic,bench-smoke
qr set https://techtiff.ai/cypher-drive
qr show
screen log
page next
touch
selftest
history
```

`selftest` prints `[selftest] PASS/FAIL <check>` for each step and ends with
`[selftest] RESULT PASS` when every check is green.

## Build

From the repository root (unique build paths so parallel builds do not collide):

```sh
# Headless baseline (USE_DISPLAY=0) - Serial-only, identical behavior
arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries shared --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --build-path _arduino-build/05-baseline projects/05-cypherdrive-wireless-ops

# Touch UI (USE_DISPLAY=1)
arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries shared --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --build-property "compiler.cpp.extra_flags=-DUSE_DISPLAY=1" \
  --build-path _arduino-build/05-display projects/05-cypherdrive-wireless-ops
```

The shared flag matrix (`scripts/check-flag-matrix.sh`) exercises the `baseline`,
`display`, `wifi-scan`, `ble-bridge`, `qr-persist`, and `hardware-gated` rows for
this project; all compile green.

## Proof State

Current target proof is `compile-ready`: the touch UI and every feature-flag
combination build for the repo's ESP32-P4 FQBN, and `selftest` passes the mock
flow headlessly. Wireless scan, UART bridge wiring, QR reboot persistence, and
the on-panel display/touch behavior are not `field-proven` until uploaded, run,
and observed on the exact CrowPanel hardware. Nothing here has been seen on a
panel in this session.

Use proof language precisely:

- `compile-ready` — Arduino CLI build passed.
- `uploaded` — repo upload helper flashed the board and verified the hash.
- `field-proven` — Serial output, display state, UART bridge frames, or QR
  persistence were observed on the real panel.
