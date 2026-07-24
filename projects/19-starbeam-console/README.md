# Starbeam Console

A full 1:1 port of project-starbeam (`starbeam_v2`) onto the Elecrow CrowPanel
Advanced 7-inch display, trading the original 128×64 three-button OLED menu for a
modern touch dashboard.

The panel drives seven radios natively over one shared SPI bus — five nRF24L01+
and two CC1101 — so the jammers, the 2.4 GHz spectrum analyzer, the 433 MHz
scan/RSSI, and raw record/replay all run right on the P4. The Wi-Fi/BLE/attack
half runs on a UART-attached ESP32 flashed with stock `starbeam_v2`, with the
panel forwarding commands and rendering telemetry.

This is the one transmit-capable project in the suite, and it is a faithful port
of the owner's own tool. Every transmit path is disarmed by default.

> This is Project 19 in the [CrowPanel Arduino suite](../../README.md).

## Status

Compile-ready: the baseline, display, radios, co-processor, and full builds all
pass. Nothing has been proven on hardware yet — per-radio register IDs, shared-SPI
CS isolation, touch zones, a tear-free animated spectrum, and the UART round-trip
to the co-processor all still need to be captured on a real panel. See the
[technical reference](TECHNICAL.md).

## What you get

- A touch dashboard on the 1024×600 panel with a dark ops palette
- A status bar with seven radio-detect dots, a co-processor UART link pill, and a
  TX SAFE / TX ARMED gate
- Home categories: jammers, scanners, Wi-Fi/BT security, recording, radios & test,
  and settings
- Operation screens: animated nRF spectrum, CC1101 RSSI gauge and sweep, a
  14-channel Wi-Fi heatmap, attack and capture counters, and a register-proof grid
- A persistent red STOP and a legal-acknowledgement modal before any armed action
- Native control of all seven radios plus a forwarding path to the ESP32
  co-processor for the Wi-Fi/BLE features

## Transmit is disarmed by default

Out of the box the console runs receive and analysis only. Every screen renders,
but transmit paths — the nRF24 and CC1101 jammers, raw replay, and forwarded
co-processor attacks — are refused. Arming requires two things: setting
`STARBEAM_TX_CONFIRMED` in a local, gitignored `config/LabProfile.h`, and then
acknowledging the authorized-use modal in the UI.

## Responsible use

This project can transmit. Operate only on radios, antennas, frequencies, and
networks you own or are explicitly authorized to test — transmitting, jamming,
and attacking devices you do not own is illegal in most places. Confirm your
setup is yours before you set the arming flag, and keep `LabProfile.h` and
`Pins.h` out of Git.

## Technical reference

For installation, build flags, configuration, wiring, upload commands, device
details, file layout, troubleshooting, safety boundaries, and proof terminology,
see [TECHNICAL.md](TECHNICAL.md).
