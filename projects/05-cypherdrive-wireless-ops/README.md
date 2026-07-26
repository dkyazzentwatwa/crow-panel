# 05 · CypherDrive Active Field Tool

A touch-first **active** wireless + HID field tool for the CrowPanel ESP32-P4,
built to use the onboard ESP32-C6 radio as fully as possible. Four tabbed screens
— **WI-FI / BLE / HID / LOG** — drawn with the shared `Widgets` toolkit, with a
full Serial command surface at 1:1 parity.

This replaces the project's earlier passive-only console. It now **joins,
interrogates, and emits**:

- **WI-FI** — active probe scan (rich metadata: BSSID/band/PHY/WPS), tap-to-join
  with an **on-screen keyboard** for the key, and once associated: captive-portal
  **detection**, mDNS/service discovery, TCP port + host sweep, DNS, HTTP banner
  grab, OUI vendor lookup. Every row/inspector has **SAVE TO SD** (CSV export).
- **BLE** — on-panel NimBLE **central** through the C6: active scan (with vendor /
  TX-power / address-type), then connect and enumerate a device's GATT services
  (read-only). SAVE TO SD exports the device.
- **HID** — an operator-driven macro/keystroke pad the panel sends to a host over
  USB or BLE, on the shared `CrowHid` stack (extracted from project 21).
- **PLD** — a **benign DuckyScript payload runner**: built-in presets plus any
  `.txt` payload on the SD card at `/cypherdrive/payloads/`. Tap to play over the
  active HID output, with a STOP + progress bar. No offensive payloads.
- **LOG** — a paged activity log (Wi-Fi / BLE / net / HID entry types).

Everything with hardware paths is **mock-first** behind a flag; with all flags off
the whole tool runs on believable mock data.

Everything is **mock-first**: with all feature flags off every screen fills with
believable data and the headless `selftest` passes, so the tool is fully
exercisable with no radio and no host attached.

## What it deliberately does NOT do

This is a field tool, not an attack platform. It excludes the destructive/abusive
end and those capabilities are not to be added:

- No Wi-Fi **deauth / jamming** (denial of service).
- No **evil-twin / captive-portal credential capture** — captive handling is
  *detection only* (a probe of a known 204 endpoint).
- No unattended **BadUSB autorun** — HID is operator-driven: you tap a tile, it
  types. No autorun payloads, no exfil.
- On-panel 802.11 **promiscuous capture** is not reachable through the hosted C6
  API on this core; that stays a companion-board job (see the Flock system).

## Feature flags (all default 0 — mock)

| Flag | Turns on |
|---|---|
| `USE_WIFI_ACTIVE` | Active Wi-Fi scan + join + client tools via the hosted C6 |
| `USE_BLE_C6` | On-panel NimBLE central scan/GATT via the C6 (**at-risk — see TECHNICAL.md**) |
| `USE_USB_HID` | Native USB HID output (**needs an `USBMode=default` FQBN** to go live) |
| `USE_BLE_HID` | BLE HID output via the C6 (builds under the default `hwcdc` FQBN) |
| `USE_CYPHERDRIVE_SD` | SD_MMC export (findings CSV) + SD-loaded DuckyScript payloads |
| `USE_DISPLAY` | The touch UI (else headless, serial-only) |

## Serial walkthrough (115200, Newline)

```
help                 list commands
status               uptime, heap, flags, link state, HID output, screen
scan wifi            active Wi-Fi scan
net 1                inspect Wi-Fi row 1
join 1               associate with row 1 (key from config/WiFiSecrets.h; open nets need none)
captive              captive-portal detection on the current link
mdns                 mDNS/service discovery on the joined LAN
portscan 192.168.1.1 TCP connect sweep of a target (empty = gateway)
leave                drop the Wi-Fi association
scan ble             on-panel BLE central scan
ble 1                select BLE row 1
connect 1            GATT connect + enumerate services for row 1
disconnect           drop the GATT connection
hid 1                fire HID macro tile 1
type hello world     send text over the active HID output
media volup          send a consumer-control key (play|mute|volup|voldown)
out                  toggle HID output USB <-> BLE
logs / page next     print / page the activity log
selftest             headless mock flow with a PASS/FAIL summary
```

Every touch action has a matching command, so the whole tool is drivable over
Serial with no panel.

## Per-machine config

Join credentials are gitignored. Copy `config/WiFiSecrets.example.h` to
`config/WiFiSecrets.h` and set the network you are **authorized** to join
(`CYPHERDRIVE_JOIN_SSID` / `CYPHERDRIVE_JOIN_PASS`); open networks join with no
key. Only scan/join/probe hosts and networks you are authorized to test. Never
commit real credentials.

## Build

```bash
# mock (default), display UI
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/compile-all.sh

# active Wi-Fi + on-panel BLE + BLE HID (hwcdc FQBN)
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI_ACTIVE=1 -DUSE_BLE_C6=1 -DUSE_BLE_HID=1" ./scripts/compile-all.sh
```

Native USB HID needs an `USBMode=default` FQBN (see TECHNICAL.md); it cannot be
built by the shared flag matrix, which uses `USBMode=hwcdc`.

## Status

The **core is field-proven on a real CrowPanel**: it boots to the UI, runs active
Wi-Fi scans (18 real networks), on-panel **C6 NimBLE central** scans (24 real
devices — the path that started as "may never work" now works on hardware), list
pagination, and **live USB HID** (typed Cmd+Shift+4 to a Mac). Still
**compile-ready but not yet exercised on hardware**: Wi-Fi join + the on-screen
keyboard, the client/recon tools (they need a joined network), GATT
connect/enumerate, SD export, and the payload runner. Every flag combination
builds for the ESP32-P4 target. See `docs/full-port-proof-matrix.md`.
