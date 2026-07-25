# CrowPanel FieldOps Control Center Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at in-progress/01-fieldops-control-center.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in in-progress/01-fieldops-control-center/TECHNICAL.md.
```

---

LoRa-powered AIoT dashboard concept for remote field sensors on the CrowPanel Advanced 7-inch ESP32-P4 display.

Default mode is a Serial-only mock demo:

- Generates fake LoRa sensor packets every few seconds
- Prints dashboard card updates
- Simulates warning and critical alerts
- Logs field events
- Produces AI-style summaries through a mock client

## UI

A bespoke, touch-first dashboard on the 1024×600 DSI panel, built entirely on
the shared `CrowDisplay` bring-up and `Widgets` toolkit (dark "ops" palette,
FreeSans fonts, cards/gauges/sparklines/chrome) — **Adafruit-GFX API only, no
LVGL**. The old single-screen `OpsDashboard`/`setLine` rendering is gone.

A shared `headerBar()` (title, per-screen subtitle, transport pill, unacked-alert
badge) sits at the top and a `tabBar()` at the bottom drives navigation. Screens
are an enum with a `dirty_` flag, so a changed frame paints once and calls
`CrowDisplay::flush()` once (manual-flush bring-up — no per-pixel cache sync). The
whole data model (nodes, alert store, rolling log, pinned node, log page) and its
logic compile in **every** build so `selftest` drives the flow with no panel.

`tick()` reads debounced GT911 touch (`CrowTouch`, keying navigation off
`releasedEdge()` + `releaseX/Y()`) and returns a typed `FieldOpsUiEvent`
(`PIN_NODE` / `ACK_ALERT`) that the sketch executes — the UI never writes the
shared `EventLog` itself.

## Screens

| Screen | What it shows | Touch |
|---|---|---|
| **Roster** | 2×3 grid of field-node cards: status dot, temp, battery bar, humidity, signal bars, age. Presence (chat) nodes render as telemetry-less tiles. AI shift-summary strip along the bottom. | Tap a card to pin that node and jump to Detail |
| **Detail** | The pinned (or auto-followed) node: battery / temperature / humidity `arcGauge()` rings, a temperature-trend `sparkline()`, and a signal + motion + last-seen stat strip. | Read-only (use the tab bar) |
| **Alerts** | Warning/critical stream, newest first, each row a severity chip (WARN amber / CRIT red), the alert text, node, and age. Acknowledged rows dim to an `ACKED` chip. | Tap an unacked row to acknowledge it |
| **Log** | Scrollable rolling event log (timestamp + line, 8 per page) fed by every packet, alert, ack, and pin. | `< NEWER` / `OLDER >` paging buttons |

## Touch controls

| Control | Location | Action | Serial equivalent |
|---|---|---|---|
| Tab bar | Bottom strip | Switch screen | `screen <roster\|detail\|alerts\|log>` |
| Node card | Roster grid | Pin node → Detail | `pin <name\|index>` |
| Alert row | Alerts stream | Acknowledge alert | `ack [n]` |
| `< NEWER` / `OLDER >` | Log footer | Page the log | `log <prev\|next\|N>` |

Every touch action has a serial command, and both go through the same UI methods,
so an offline serial demo and the panel behave identically.

## Serial Commands

115200 baud, line ending **Newline**:

- `help` / `status` / `history` — shared commands (`status` also prints the live UI state: screen, node/alert/log counts, active node).
- `inject [node 0-3] [tempC] [batteryPct]` — simulate a packet through the same pipeline the mock and real drivers use. `inject 1 40 12` fires TEMP_WARNING and LOW_BATTERY on demand.
- `feed <csv>` — inject a raw ESP-NOW bridge frame (bench-test the ESP-NOW path with no radio). `feed SENSOR,ATTIC,29.5,40,88,0,-58` adds a telemetry node; `feed PRESENCE,CYPHER_NODE,-70,chat` adds a presence tile.
- `screen <roster|detail|alerts|log>` — switch the active screen (the tab bar).
- `pin <node-name|index>` — pin a node to Detail (a roster-card tap).
- `ack [n]` — acknowledge the newest unacked alert, or the nth on-screen row.
- `log <prev|next|N>` — page the on-screen event log.
- `touch` — print raw + mapped touch coordinates, tap count, and the current screen.
- `selftest` — drive the mock flow end-to-end headlessly (inject → alert → ack → pin → navigate → page) and print explicit `PASS`/`FAIL` lines plus a summary. Works with no panel attached.

### Serial smoke (mock, no panel)

```text
selftest
inject 1 40 12
screen alerts
ack
screen log
log next
pin ATTIC
status
history
```

`selftest` should print six `PASS` lines and `0 failed`.

## Compile

Headless (default, Serial-only mock — must stay green):

```sh
CTAGS_WORKAROUND=1 arduino-cli compile \
  --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries "$PWD/shared" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  in-progress/01-fieldops-control-center
```

Touch UI (the real panel build) — add `-DUSE_DISPLAY=1`:

```sh
CTAGS_WORKAROUND=1 arduino-cli compile \
  --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries "$PWD/shared" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --build-property "compiler.cpp.extra_flags=-DUSE_DISPLAY=1" \
  in-progress/01-fieldops-control-center
```

Or `../../scripts/compile-all.sh` for every project. The shared flag matrix
(`scripts/check-flag-matrix.sh`) exercises this project's `baseline`, `display`,
`wifi`, `lora`, `espnow`, `espnow-display`, and `kitchen-sink` rows; all draw/touch
code is gated on `USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4`, so the default build
stays headless and green. The default FQBN targets the real ESP32-P4 (see the root
README).

## Upload

```sh
arduino-cli board list
../../scripts/upload-project.sh in-progress/01-fieldops-control-center /dev/cu.usbmodem101
```

## LoRa / SX1262

A real RadioLib SX1262 scaffold lives in `src/LoRaGateway.cpp` behind `USE_LORA_DRIVER` — compile-verified, not hardware-verified. Pins come from the active `HardwareProfile`; radio parameters mirror Elecrow's Lesson13 example (915 MHz default — EU boards must override to 868 in `config/Pins.h`, copied from `Pins.example.h`).

Enable it only per `docs/hardware-bringup-checklist.md` Stage 6:

1. Confirm the board revision (Stage 2) — the V1.2 wireless pin remap is unverified upstream.
2. Fit the SX1262 module and an antenna.
3. Build with `EXTRA_FLAGS="-DUSE_LORA_DRIVER=1"`.
4. Have a second device transmitting (Elecrow's Lesson13 TX example).

## ESP-NOW

An alternative transport (`USE_ESPNOW`) feeds the same dashboard from an ESP-NOW mesh of plain ESP32s — sensor nodes plus cypher-chat chat nodes. The ESP32-P4 can't be an ESP-NOW peer (WiFi is remote on the C6), so a spare ESP32 runs the radio and bridges to the panel over UART. The dashboard shows sensor nodes with telemetry and chat nodes as presence tiles; tap a node to pin it. Full architecture, wiring, and flashing: [`espnow/README.md`](../../espnow/README.md).

```sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_ESPNOW=1 -DUSE_DISPLAY=1" \
  ../../scripts/upload-project.sh in-progress/01-fieldops-control-center <PORT>
```

## What To Film

- Serial boot log showing `CROWPANEL_P4_7IN_V1_2`.
- `selftest` printing its `PASS` lines and `0 failed`.
- Mock packets filling the Roster grid; tapping a card to pin it and land on Detail.
- `inject 1 40 12` firing LOW_BATTERY, the Alerts badge incrementing, then tapping the row to acknowledge it.
- Paging the Log with `< NEWER` / `OLDER >`, and `history` replaying the shared event log.
- The hardware profile warning explaining why pins are revision-aware.

## Proof state

`compile-ready`. The `baseline`, `display`, `wifi`, `lora`, `espnow`,
`espnow-display`, and `kitchen-sink` builds all compile clean for the ESP32-P4
target, and `selftest` exercises the full mock flow headlessly. **Nothing here has
been observed on a physical CrowPanel** — no panel was attached to the build
session. Not yet observed on hardware: DSI rendering of the four screens, GT911
touch zones (tab bar, roster cards, alert rows, log paging), single-flush repaint
without tearing, and the LoRa/ESP-NOW transports. Treat every screenshot-worthy
claim as pending a bring-up session (`docs/hardware-bringup-checklist.md`).
