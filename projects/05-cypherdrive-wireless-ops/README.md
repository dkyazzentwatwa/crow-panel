# CypherDrive Wireless Ops

A passive wireless-visibility console for the Elecrow CrowPanel Advanced 7-inch
display.

It shows you what is around you — nearby Wi-Fi networks and BLE advertisements —
as readable cards, keeps a local scan log you can review, and holds a QR handoff
URL for passing a link to a phone. It is deliberately a look-don't-touch tool.
The offline demo fills every screen with simulated rows so you can explore it
with no radio activity at all.

> This is Project 5 in the [CrowPanel Arduino suite](../../README.md).

## Status

Compile-ready: the sketch, the touch UI, and every feature flag build for the
real ESP32-P4 target (baseline, display, and all hardware-flag combinations).
The passive Wi-Fi scan, the BLE sidecar bridge, QR persistence across a reboot,
and the on-panel display and touch behavior have not been observed on hardware
yet. See the [technical reference](TECHNICAL.md) for the wiring assumptions and
what each flag still owes in proof.

## Screens

A touch-first console on the 1024x600 panel with a bottom tab bar and a
persistent **PASSIVE - RECEIVE ONLY** banner in the header on every screen:

- **Wi-Fi** — nearby networks as cards with a signal-strength glyph, RSSI,
  channel, band (2.4/5 GHz) and security. Tap a card to inspect one network.
- **BLE** — advertisement list fed by the UART sidecar (parser only; the panel
  runs no BLE stack).
- **Log** — the local scan log, paged, newest first.
- **QR** — the QR handoff URL rendered as a large readable block with its
  persistence state (a decorative placeholder stands in until a real QR
  renderer is added).

Every touch action also has a Serial command, so the whole tool drives headless.

## What you get

- Wi-Fi scan cards from a passive scan through the onboard ESP32-C6
- A tap-to-inspect Wi-Fi detail view with a signal arc-gauge
- BLE advertisement rows fed by a separate scanner over UART
- A QR handoff screen with an optional persisted URL
- A paged local scan log and the shared event history
- An offline demo that fills every screen with simulated Wi-Fi and BLE rows
- A `selftest` command that drives the whole mock flow with PASS/FAIL output

## Why BLE comes from a sidecar

The panel never runs a BLE stack itself. A small ESP32 does the scanning and
writes newline-delimited CSV frames to the panel over UART, which keeps the radio
work off the P4 and makes the whole feed easy to bench-test — you can paste a
frame straight into Serial and watch it parse.

## Privacy and responsible use

This is a passive visibility tool and nothing else. It does not join networks,
collect credentials, start an access point or captive portal, emit HID payloads,
deauthenticate clients, replay traffic, or transmit BLE controls. The only thing
it writes anywhere is local demo state on the panel, and the QR path actively
rejects credential-looking query fields.

Scan results describe other people's devices. Treat the logs accordingly, and do
not publish captures of a space you do not own.

## Technical reference

For installation, build flags, configuration, upload commands, device details,
file layout, troubleshooting, safety boundaries, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
