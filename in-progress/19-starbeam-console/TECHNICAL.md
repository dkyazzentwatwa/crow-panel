# Starbeam Console (Project 19) Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at in-progress/19-starbeam-console.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in in-progress/19-starbeam-console/TECHNICAL.md.
```

---

A full **1:1 port of [project-starbeam](https://github.com/) (starbeam_v2)** onto the
CrowPanel Advanced ESP32-P4, with a modern touch UI replacing the original
128×64 3-button OLED menu.

- The **P4 drives all seven radios natively** over one shared SPI bus:
  **5× nRF24L01+** and **2× CC1101**. Every nRF24/CC1101 feature — the jammers,
  the 2.4 GHz spectrum analyzer, the 433 MHz scan/RSSI, and raw record/replay —
  runs directly on the panel.
- The **Wi-Fi/BLE/attack half** of Starbeam (deauth, beacon/probe flood, PMKID,
  packet monitor, Wi-Fi scan/heatmap, BLE scan, captive portal, web server,
  flock detector) runs on a **UART-attached ESP32 dev module flashed with stock
  `starbeam_v2`** — whose `terminal.cpp` already exposes every feature as a
  serial command. The panel forwards commands and renders the telemetry. (The
  P4 has no native Wi-Fi/BLE, so this is how full parity is reached.)

## UI

Touch-first dashboard on the 1024×600 DSI panel, dark "ops" palette + a Starbeam
magenta accent, built on the shared `CrowDisplay` bring-up and `Widgets` toolkit:

- **Status bar** — 7 radio detect dots (5 nRF + 2 CC), co-processor **UART LINK**
  pill, and a **TX SAFE / TX ARMED** gate indicator.
- **Home** — six category cards: JAMMERS · SCANNERS · WIFI/BT SECURITY ·
  RECORDING · RADIOS & TEST · SETTINGS.
- **Operation screens** — animated nRF spectrum, CC1101 RSSI arc-gauge + sweep,
  14-channel Wi-Fi heatmap, scan counters, attack frame/client/capture counters,
  register-proof grid, with a persistent red **STOP**.
- **Legal-ack modal** — every transmit/attack action is gated behind an
  authorized-use confirmation (ported from Starbeam's legal warning).

## Wiring (P4 GPIO)

All 5 nRF24 + 2 CC1101 **share one SPI bus**; only CS/CE are unique.

| Signal | GPIO |
|---|---|
| Shared SPI SCK / MOSI / MISO | 6 / 7 / 8 |
| nRF24 R0–R4 CSN | 9 / 10 / 53 / 54 / 45 |
| nRF24 R0–R4 CE | 46 / 2 / 3 / 4 / 5 |
| CC1101 #1 CS / GDO0 | 49 / 27 |
| CC1101 #2 CS / GDO0 | 28 / 25 |
| Co-proc UART TX / RX (→ ESP32 dev RX / TX) | 51 / 50 |
| Freeze button / status LED | 52 |

**Power:** feed all seven radio VCC pins from a **dedicated 3V3 regulator** off the
external-header **5V**, with 10 µF + 100 nF decoupling at each module. Leave the
wireless header's own 3V3 pin unused; common all grounds. **Strapping:** verify
IO2 / IO45 / IO46 on the ESP32-P4 datasheet before soldering (CSN idles high = the
risk). CC1101 GDO2 is left unconnected (jam/scan/RSSI need only GDO0). Override
any pin in a gitignored `config/Pins.h` (see `Pins.example.h`).

## Feature flags

Everything compiles green in every combination; the heavy paths are gated.

| Flag | Default | Enables |
|---|---|---|
| `USE_DISPLAY` | 0 | the DSI touch dashboard (else headless + serial) |
| `USE_STARBEAM_RADIOS` | 0 | native nRF24 + CC1101 (needs `RF24`, `SmartRC-CC1101-Driver-Lib`) |
| `USE_STARBEAM_COPROC` | 0 | the UART link to the ESP32 Wi-Fi/BLE module |
| `STARBEAM_TX_CONFIRMED` | 0 | **arms all transmit** (jammers, replay, forwarded attacks) |

`STARBEAM_TX_CONFIRMED` must be set from a **local, gitignored `config/LabProfile.h`**
(copy `LabProfile.example.h`). Without it the panel runs receive/analysis only:
the UI still renders every screen, but transmit paths are refused. This mirrors
the repo's project-17 authorization gate.

## Co-processor

Flash the UART-attached ESP32 dev module with stock `starbeam_v2`. The console
sends its terminal commands (`wifi_scan`, `deauth_target`, `beacon_flood`,
`pmkid`, `web_on`, …). Three Starbeam menu items — **Flock Detector, Captive
Portal, Packet Monitor** — exist in the firmware but are **not** in stock
`terminal.cpp`'s command table; add one line each (`{"flock_detect", …}`,
`{"captive_portal", …}`, `{"pkt_monitor", …}`) to drive them. For richer live
counters, have the module emit a compact `TLM frames=… clients=… net=… rssi=…
ble=… ch=1:20,6:80,…` line, which `CoProcLink` parses; otherwise the panel shows
the module's most recent serial line as status text.

## Compile

From the repository root:

```sh
# Native radios + display (the real panel build)
CTAGS_WORKAROUND=1 arduino-cli compile \
  --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries "$PWD/shared" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --build-property "compiler.cpp.extra_flags=-DUSE_DISPLAY=1 -DUSE_STARBEAM_RADIOS=1 -DUSE_STARBEAM_COPROC=1" \
  in-progress/19-starbeam-console
```

Upload: `./scripts/upload-project.sh in-progress/19-starbeam-console /dev/cu.usbmodemXXXX`.

## Serial controls

`status`, `probe`, `stop`, `tx`, `fwd <command>` (forwards a raw Starbeam
terminal command to the co-processor), plus the shared `help` / `history`.

## Proof state

`compile-ready` (baseline, display, radios, coproc, and full builds all pass).
Live-radio proof still pending on hardware: per-radio register IDs, shared-SPI CS
isolation, touch zones, animated spectrum without tearing, and the UART
round-trip to the co-processor. Do not transmit until you have confirmed the
radios, antennas, and frequencies are yours or authorized (`LabProfile.h`).
