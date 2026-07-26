# CypherDrive → Active Wireless + HID Field Tool

**Date:** 2026-07-25
**Project:** 05 (`projects/05-cypherdrive-wireless-ops`), rewritten in place
**Status:** implemented, compile-verified (not hardware-verified)

## Context

Project 05 CypherDrive shipped as a deliberately *passive, receive-only* wireless
console (Wi-Fi scan via the hosted C6, BLE via an external UART sidecar, QR handoff,
scan log), with a persistent "PASSIVE — RECEIVE ONLY" banner and a safety identity that
excluded joining, HID, capture, and transmit.

The owner has redirected it: this is now an **active field tool** that maximizes use of
the onboard ESP32-C6 radio, consistent with the heavy C6 work already done elsewhere in
this workspace. The rewrite happens in place and keeps project number 05.

## Decisions (from brainstorming)

- **Capabilities:** Active Wi-Fi ops, on-panel BLE via the C6, and USB-HID output.
  (ESP-NOW explicitly *not* included.)
- **Gating:** No arm-gate. Active/outbound actions are always available, consistent with
  how Project 21 already ships HID. (No `LabProfile.h` confirmation flag.)
- **HID transport:** USB + BLE, reusing Project 21's transport stack, which is
  **extracted into a shared `CrowHid` component** so both P05 and P21 consume one copy.
- **Location:** rewrite Project 05 in place; update the honesty-contract docs to the new
  active posture.

## Excluded (out of scope, by design)

Not built even though a wireless "active tool" could imply them:
- Wi-Fi deauth / jamming (denial-of-service).
- Evil-twin / captive-portal *credential harvesting* (captive-portal *detection* is fine).
- Unattended BadUSB autorun payloads. HID is operator-driven (tap → type), like P21.
- On-panel 802.11 promiscuous capture — not reachable through the hosted C6 API on this
  core (BW16 companion only); stays a companion-board job.

## Architecture

Preserve the existing separation: the `<Proj>Ui` class renders state and returns typed
intents; the `.ino` owns app state and executes; every touch action mirrors a serial
command; all driver paths are mock-first behind feature flags.

**Screens** (tab bar, mirroring `projects/02-cypher-vision-cam/src/VisionCamUi` —
`enum` + `kTabs[]` + `tabHit()`):

| Screen | Function |
|---|---|
| WIFI | Active probe scan with rich `wifi_ap_record_t` metadata; tap-to-join; once joined: captive-portal detection, mDNS/service discovery, gateway info, TCP port-scan of an entered target |
| BLE  | On-panel C6 NimBLE **central**: active scan + GATT connect / enumerate / read (optional write) |
| HID  | Macro/keystroke pad + trackpad; output toggle USB↔BLE |
| LOG  | Existing `ScanLog` ring, expanded entry types (wifi/ble/hid/net) |

The old QR screen and the `BleUartBridge` UART sidecar are removed (superseded by
on-panel BLE). `QrStateStore` is dropped.

## Modules (reuse-first)

- **WifiOps** — rework `WifiScanner` to active scan (`WiFi.scanNetworks(..., passive=false, ...)`)
  and reuse `shared/CrowPanelShared/CrowNetworkClient` for join + HTTP. Port-scan
  (non-blocking TCP connect sweep) and mDNS discovery are small new pieces on top.
  Model the rich scan-record extraction on `in-progress/16-cypher-flock-panel/src/FlockC6Witness.cpp`.
- **BleC6** — new on-panel NimBLE central via the C6, reusing the C6 pin+init sequence
  (`WiFi.setPins(...)` → `BLEDevice::init`) proven in
  `projects/21-cypher-keys-hid-deck/src/BleTransport.cpp`. Replaces `BleUartBridge`.
- **CrowHid (shared)** — extract `HidTransport` / `UsbTransport` / `BleTransport` /
  `HidBackend` out of Project 21 into `shared/CrowPanelShared`, renamed off the
  `CYPHER_KEYS_*` macros to a neutral `CROW_HID_*` prefix. Migrate P21 to the shared copy;
  build P05's deck on it. Close P21's documented-but-missing compile-time `#warning`
  (the `#else`/mock branch of `UsbTransport.h`) while in there.

## Feature flags

Added to `shared/CrowPanelShared/AppConfig.h` (default 0), overridden in
`projects/05.../config/ProjectConfig.h`, each with a green row in
`scripts/check-flag-matrix.sh`:

- `USE_WIFI_ACTIVE` — active scan + join + client tools (also needs `USE_WIFI` so the
  shared join/HTTP path compiles).
- `USE_BLE_C6` — on-panel NimBLE central scan/GATT.
- `USE_USB_HID`, `USE_BLE_HID` — HID transports (shared with P21's flags).

Everything mock-first: with all flags off, every screen fills with believable mock data
and the headless `selftest` passes.

## Key risk — on-panel BLE via C6

No project in the repo does C6 BLE *central/scan* today; only BLE *peripheral* (HID) is
proven, and Project 17 gates its BLE page because esp_hosted NimBLE central may not be
enabled in the installed P4 Arduino profile. Mitigation: implement mock-first behind
`USE_BLE_C6` with a **runtime capability probe** that degrades to a clear "BLE central
unavailable" state instead of crashing. This capability may remain compile-verified-mock
until proven on real hardware; the proof matrix will say so honestly.

## Native USB HID build constraint

Live USB HID needs an `USBMode=default` FQBN; `check-flag-matrix.sh` builds with
`USBMode=hwcdc`, so it exercises only the mock path (same known limitation as P21). Live
USB is a documented manual-build / hardware step, not a matrix row.

## Honesty-contract updates (land with the code)

- `CLAUDE.md` — remove 05 from the passive/receive-only RF list; state the new active
  posture and the excluded-capabilities line.
- `docs/full-port-proof-matrix.md` — rewrite the 05 row and its safety-boundary note;
  proof state stays `compile-ready` (nothing hardware-verified); BLE-C6 flagged as the
  at-risk capability.
- `projects/05.../README.md`, `TECHNICAL.md`, `docs/security-notes.md` — rewrite to the
  active identity; drop the passive banner language.
- Per-machine config via gitignored templates (`WiFiSecrets.h`, `Devices.h`) with
  `.example.h` files.

## Implementation phases

1. Shared `CrowHid` extraction + P21 migration (verify P21 flag-matrix rows still green).
2. WifiOps active (scan/join/client tools).
3. BleC6 central (mock-first + capability probe).
4. HID deck + full UI rebuild on the shared chrome.
5. Flags + `check-flag-matrix.sh` rows + honesty-contract docs.

## Verification

- `./scripts/compile-all.sh` across mock and real flag combos (`EXTRA_FLAGS`).
- `./scripts/check-flag-matrix.sh` — new 05 rows green, P21 rows still green.
- Updated headless `selftest` PASS.
- Native USB HID: documented manual `USBMode=default` build (not matrix-able).
- Docs updated in the same change as the code.
