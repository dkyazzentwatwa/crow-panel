# CrowPanel FieldOps Control Center

LoRa-powered AIoT dashboard concept for remote field sensors on the CrowPanel Advanced 7-inch ESP32-P4 display.

Default mode is a Serial-only mock demo:

- Generates fake LoRa sensor packets every few seconds
- Prints dashboard card updates
- Simulates warning and critical alerts
- Logs field events
- Produces AI-style summaries through a mock client

## Core Screens

- Dashboard
- Node Detail
- Alerts
- AI Summary
- Settings

## Serial Commands

115200 baud, line ending **Newline**:

- `help` / `status` / `history` — shared commands
- `inject [node 0-3] [tempC] [batteryPct]` — simulate a packet through the same pipeline the mock and real drivers use. `inject 1 40 12` fires TEMP_WARNING and LOW_BATTERY on demand.
- `feed <csv>` — inject a raw ESP-NOW bridge frame (bench-test the ESP-NOW path with no radio). `feed SENSOR,ATTIC,29.5,40,88,0,-58` adds a telemetry node; `feed PRESENCE,CYPHER_NODE,-70,chat` adds a presence tile.

## Compile

```sh
../../scripts/compile-all.sh
```

The default FQBN targets the real ESP32-P4 (see the root README). Everything is compile-verified; nothing is hardware-verified until it runs on your CrowPanel.

## Upload

```sh
arduino-cli board list
../../scripts/upload-project.sh projects/01-fieldops-control-center /dev/cu.usbmodem101
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
  ../../scripts/upload-project.sh projects/01-fieldops-control-center <PORT>
```

## What To Film

- Serial boot log showing `CROWPANEL_P4_7IN_V1_2`.
- Mock packets turning into dashboard rows.
- `inject 1 40 12` firing LOW_BATTERY live, then `history` replaying the event log.
- The hardware profile warning explaining why pins are revision-aware.
