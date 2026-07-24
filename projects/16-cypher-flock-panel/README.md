# Cypher Flock Panel

A passive dual-band detector console for the Elecrow CrowPanel Advanced 7-inch
display.

It watches for documented device signatures across 2.4 and 5 GHz Wi-Fi and BLE,
shows them on a proximity scope and an evidence feed, tracks per-band health, and
saves sessions so a reboot does not lose your work. The default build runs
entirely on simulated contacts, so the whole five-screen interface works with no
companion boards attached.

> This is Project 16 in the [CrowPanel Arduino suite](../../README.md).

## Status

Compile-ready only. Getting further is staged deliberately: all three boards must
upload and exchange UART hello streams before this counts as *uploaded*, real 2.4
and 5 GHz frames are needed for *dual-band-proven*, and a documented recall and
false-positive threshold is needed for *field-proven*. The C6 witness path is
separately gated — a green compile does not prove the panel returns hosted scan
records. See the [technical reference](TECHNICAL.md).

## How the three boards split the work

- **BW16** — passive dual-band Wi-Fi scanning, 2.4 and 5 GHz
- **A generic ESP32** — passive BLE scanning, plus aggregating both streams
- **The panel's onboard C6** — a passive 2.4 GHz witness, observation only
- **The P4 panel itself** — the touch UI, history, and persistence

Witness rows are never detections. They are shown on their own screen, kept in
RAM, and never counted toward detection confidence.

## What you get

- **Scope** — RSSI mapped to proximity rings, with a stable MAC hash setting the
  angle
- **Feed** — current and previous contacts with their supporting evidence
- **C6 Wi-Fi** — up to 64 nearby access points with SSID, BSSID, RSSI, channel,
  security, PHY, bandwidth, country, and more when the scan reports them
- **Stats** — separate aggregator, BLE, and BW16 health plus per-band counters
- **Control** — scan, hopping, band, channel, profile, diagnostics, calibration,
  stealth, save, and a two-tap reset
- Session persistence with CRC validation before a save is promoted, no
  auto-formatting, and a visible warning if it falls back to RAM
- A stealth mode that blacks out the display while scanning continues
- An offline demo that injects deterministic Wi-Fi, BLE, and Raven contacts
  through the exact same parser real events use

## The scope is not a map

It maps signal strength to a ring and a hash to an angle so contacts sit
somewhere stable and readable. It is explicitly not a location or direction
display, and RSSI does not indicate where anything is.

## Privacy and responsible use

This is a passive field-visibility tool. It inspects management and data frame
headers and BLE advertisements for documented signatures. It does not join
networks, deauthenticate clients, capture credentials, or store packet payloads.

MAC addresses and SSIDs are still sensitive: they describe other people's
devices and, over time, their movements. Use this only where monitoring is lawful
and you are authorized, and keep session files local.

## Technical reference

For installation, build flags, configuration, upload commands, device details,
file layout, troubleshooting, safety boundaries, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md). The three-board architecture and the staged setup
and evidence checklist have their own guides:
[architecture](../../docs/cypher-flock-three-board.md),
[setup and test guide](../../docs/cypher-flock-test-setup-guide.md).
