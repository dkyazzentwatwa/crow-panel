# CrowPanel CypherDrive Active Field Tool — Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at projects/05-cypherdrive-wireless-ops.

Read the repository AGENTS.md first. Preserve this project's behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details here.
```

---

An **active** wireless + HID field tool built to use the onboard ESP32-C6 as
fully as possible. Mock-first: a touch-first, four-screen dashboard
(WI-FI / BLE / HID / LOG) on the 1024x600 DSI panel with the shared
`CrowDisplay` bring-up and `Widgets` toolkit (dark "ops" palette, FreeSans fonts,
cards / signal bars / pills).

### Safety boundary (product constraint, not a suggestion)

This tool joins, interrogates, and emits — but the destructive/abusive end is
deliberately excluded and is not to be added: **no** Wi-Fi deauth/jamming, **no**
evil-twin/captive-portal credential capture (captive handling is *detection*
only — a probe of a known 204 endpoint), and **no** unattended BadUSB autorun
(HID is operator-driven: a tap emits a keystroke; there are no autorun payloads).
On-panel 802.11 promiscuous capture is not reachable through the hosted C6 API on
this core and stays a companion-board job.

## Modules

| File | Role |
|---|---|
| `src/WifiOps.{h,cpp}` | Active Wi-Fi: probe scan (`scanNetworks(..., passive=false, ...)`, BSSID + band), `join()`/`leave()`/`maintain()` association state machine, and client tools — `checkCaptivePortal()` (HTTP 204 probe), `discoverServices()` (mDNS), `portScan()` (TCP connect sweep). Real path behind `USE_WIFI_ACTIVE`. |
| `src/BleC6.{h,cpp}` | On-panel BLE central over the C6 (NimBLE): active `scan()`, `connect()` + GATT service enumeration. Real path behind `USE_BLE_C6`, with a runtime `available()` probe. |
| `src/HidPad.{h,cpp}` | Operator-driven macro/keystroke pad on the shared `CrowHid` backend (USB + BLE). 8-tile macro table + `typeText`/`media`/output-toggle. |
| `src/ScanLog.{h,cpp}` | 16-entry ring log; entry types wifi / ble / net / hid / info. |
| `src/WirelessOpsUi.{h,cpp}` | The four-screen touch UI; `tick()` returns a typed `WirelessEvent` the sketch executes. |
| `src/WirelessTypes.h` | Shared record/status structs. |

The shared HID stack lives in `shared/CrowPanelShared/CrowHid*` (`CrowHidBackend`,
`CrowUsbTransport`, `CrowBleTransport`, `CrowHidKeycodes`, `CrowHidTypes`),
extracted from project 21 so both projects share one transport implementation.

## UI

`tick()` services the shared debounced `CrowTouch` (navigation keys off
`releasedEdge()` + `releaseX/Y()`) and returns a typed `WirelessEvent`
(`{ action, index }`) the sketch executes; the UI never mutates application
state. A `dirty_` flag repaints only on change, then one `CrowDisplay::flush()`
per frame (`manualFlush=true`). Header chrome carries an **ACTIVE FIELD TOOL**
banner and the live status pill.

| Screen | Contents / touch actions (all mirrored by a serial command) |
|---|---|
| **WI-FI** | `SCAN` (active) · `LEAVE` · link-status strip (state / SSID / IP / gateway, + captive pill) · network cards (tap → inspector) · `CAPTIVE` / `mDNS` / `PORTSCAN` tool buttons. Inspector: full fields + BSSID + signal arc-gauge + `JOIN`. |
| **BLE** | `SCAN` · device cards (tap to select) · `CONNECT`. When connected: a GATT-services list + `DISCONNECT`. |
| **HID** | Output strip (USB/BLE, live/mock) + `TOGGLE` · a 2×4 macro-tile grid · last-action readout. |
| **LOG** | The `ScanLog` ring, 6 rows/page, newest first, type chips + timestamps, `PREV`/`NEXT`. |

## Feature flags

Mock mode is the default. Enable one path at a time with `EXTRA_FLAGS` or raw
Arduino CLI `--build-property` defines.

| Flag | Default | What it enables | Proof limit |
|---|---:|---|---|
| `USE_WIFI_ACTIVE` | `0` | Active probe scan + join + client tools (captive detection, mDNS, port scan) through the hosted C6. Also configures the C6 SDIO pins via the shared `configureCrowPanelHostedWiFiPins()`. | compile-verified until run on the exact CrowPanel/C6 runtime |
| `USE_BLE_C6` | `0` | On-panel NimBLE **central** scan/GATT over the C6. **At-risk:** no project has proven C6 hosted NimBLE central on hardware; the P4 Arduino profile may not enable it. Degrades to mock if the stack does not come up. | compile-verified only; may stay compile-verified-mock until proven |
| `USE_USB_HID` | `0` | Native USB HID output. **Live only under an `USBMode=default` FQBN** (`ARDUINO_USB_MODE==0`); under the suite default `hwcdc` it falls back to MOCK and emits a compile-time `#warning`. | live path compile-verified under `USBMode=default` |
| `USE_BLE_HID` | `0` | BLE HID output via the C6 (NimBLE). Builds under the default `hwcdc` FQBN. | compile-verified |
| `USE_DISPLAY` | `0` | The `WirelessOpsUi` touch dashboard. Without it the project is Serial-only with identical behavior. | compile-verified unless uploaded and observed |

Example:

```sh
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI_ACTIVE=1 -DUSE_BLE_C6=1 -DUSE_BLE_HID=1" ./scripts/compile-all.sh
```

### The BLE-C6 central risk, in detail

`BleTransport` in project 21 proved BLE **peripheral** (HID) works over the hosted
C6 (NimBLE). BLE **central** (scan/connect/GATT) is a different code path that no
project in this suite has run on hardware, and project 17 gates its BLE page for
exactly this reason: the installed P4 Arduino esp_hosted NimBLE profile may not
enable central features. `BleC6` therefore keeps a runtime `available()` probe and
degrades to a clear mock state rather than hanging. Treat any "it works" claim
here as unproven until observed on a real panel.

### Native USB HID build

Live native USB HID requires the USB-OTG (TinyUSB) build:

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries shared --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --build-property "compiler.cpp.extra_flags=-DUSE_DISPLAY=1 -DUSE_USB_HID=1" \
  --build-property "compiler.c.extra_flags=-DUSE_DISPLAY=1 -DUSE_USB_HID=1" \
  --build-path _arduino-build/05-usbhid projects/05-cypherdrive-wireless-ops
```

`scripts/check-flag-matrix.sh` uses `USBMode=hwcdc`, so its `usb-hid-mock` row
builds only the MOCK USB path (with the `#warning`); the live path is a manual
build, not a matrix row.

## Per-machine config

Join credentials are gitignored. Copy `config/WiFiSecrets.example.h` to
`config/WiFiSecrets.h` and set `CYPHERDRIVE_JOIN_SSID` / `CYPHERDRIVE_JOIN_PASS`
for a network you are authorized to join; when the SSID you tap matches, the tool
uses that key. Open networks join with no key; any other secured SSID needs its
key added first. `CYPHERDRIVE_PORTSCAN_TARGET` optionally sets a default
port-scan target (empty falls back to the gateway). Never commit real credentials.

## Serial commands

115200 baud, line ending Newline. Every touch action has a Serial equivalent.

`help` · `status` · `history` · `scan wifi|ble` · `net <n>` · `join <n>` ·
`pass <key>` · `leave` · `captive` · `mdns` · `dns <hostname>` ·
`portscan [target]` · `oui <mac>` · `ble <n>` · `connect <n>` ·
`disconnect` · `hid <n>` · `type <text>` · `media play|mute|volup|voldown` ·
`out` · `save wifi|ble` · `logs` · `screen wifi|ble|hid|log` ·
`page next|prev|<n>` · `touch` · `selftest`.

`pass` sets the key for the next join; `dns` resolves a host; `oui` is a vendor
lookup from a MAC prefix; `save` exports the selected row to SD. All four are
passive/client-side and stay inside this project's receive-and-connect
boundary — no deauth, no evil twin, no credential capture.

> Before 2026-07-31 the shared router's table was capped at 12 commands and
> silently dropped the rest, so everything from `oui` onward in registration
> order never dispatched. See `CROW_SERIAL_MAX_COMMANDS` in `AppConfig.h`.

`selftest` drives the full mock flow headlessly (active scan, join + link,
client tools, BLE scan + connect/enumerate, HID fire, log readback, UI nav across
all four screens, log paging, active-banner check) and prints
`[selftest] RESULT PASS` when every check is green.

## Proof state

Current proof is **compile-ready**. Every feature-flag combination builds for the
repo's ESP32-P4 FQBN (baseline, display, `wifi-active`, `ble-c6`, `ble-hid`,
`usb-hid` mock under hwcdc, native USB HID under `USBMode=default`, and the
`kitchen-sink` combo). The headless `selftest` is compile-verified and callable
but has **not** been run this session (it executes on the board over Serial;
there is no host-side harness for it). **Nothing here has been seen on a panel in
this session.** The `USE_BLE_C6` central path is
the at-risk capability and may remain compile-verified-mock until proven on
hardware.

Use proof language precisely:

- `compile-ready` — Arduino CLI build passed.
- `uploaded` — the repo upload helper flashed the board and verified the hash.
- `field-proven` — Serial output and on-panel behavior (active scan, join, client
  tools, BLE central, HID output) were observed on the real panel.
