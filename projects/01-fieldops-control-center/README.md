# FieldOps Control Center

A remote-sensor operations dashboard for the Elecrow CrowPanel Advanced 7-inch
display.

It gives you a touch dashboard with four screens — a tappable roster of field
nodes, a per-node detail view with ring gauges and a temperature sparkline, a
warning/critical alert stream you acknowledge with a tap, and a paged event log.
A tab bar switches screens; tap a node card to pin it, tap an alert to clear it.
Out of the box it runs entirely on simulated packets, so you can explore the
whole interface with no radio module, no gateway, and no network — and every
touch control has a matching serial command.

> This is Project 1 in the [CrowPanel Arduino suite](../../README.md).

## Status

This project is compile-verified only: it builds green for the real ESP32-P4
target, but no part of it has been observed running on a physical CrowPanel yet.
The LoRa and ESP-NOW paths in particular are scaffolds waiting for a bring-up
session. See the [technical reference](TECHNICAL.md) for the exact proof
boundaries and the staged bring-up order.

## What you get

- **Roster** — a 2×3 grid of node cards with temperature, humidity, battery, and signal; tap a card to pin it
- **Detail** — battery/temperature/humidity ring gauges and a temperature-trend sparkline for the pinned node
- **Alerts** — warning and critical alerts driven by real thresholds, not canned text; tap a row to acknowledge it
- **Log** — a scrollable rolling event log with paging controls
- Serial parity for every touch action, plus a `selftest` that drives the whole flow with no panel attached
- Mock AI shift summaries through a swappable client, and an offline demo that generates sensor traffic every few seconds

## Two ways to get real data in

- **LoRa** — an SX1262 module driven by RadioLib, using Elecrow's Lesson13 radio
  parameters (915 MHz by default; EU boards override to 868).
- **ESP-NOW** — a spare ESP32 runs the radio and bridges frames to the panel over
  UART, because the P4 has no radio of its own. Sensor nodes appear as telemetry
  cards and chat nodes as presence tiles. See
  [`espnow/README.md`](../../espnow/README.md).

Both are off by default and share the exact same code path as the mock source, so
a demo you filmed offline behaves identically once the hardware is attached.

## Responsible use

This project receives and displays sensor telemetry. It does not transmit on the
LoRa band, join networks, or capture credentials. Keep your radio parameters
legal for your region — the 915 MHz default is not licence-free everywhere — and
keep the files holding your Wi-Fi password and pin assignments out of Git.

## Technical reference

For installation, build flags, configuration, upload commands, device details,
file layout, troubleshooting, safety boundaries, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
