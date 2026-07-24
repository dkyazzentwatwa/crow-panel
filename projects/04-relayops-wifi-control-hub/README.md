# RelayOps Wi-Fi Control Hub

A Wi-Fi hub for the Elecrow CrowPanel Advanced 7-inch display that both collects
sensor data and controls other devices — no radio module required.

Remote ESP32 nodes POST their readings to a small web server running on the
panel, and the panel sends HTTP commands back out to toggle their GPIO pins:
lights, relays, fans. A dedicated World screen adds live weather, earthquake,
aurora, and air-quality data. Everything runs offline first on a simulated feed,
so the whole touch UI works before a single node is built.

> This is Project 4 in the [CrowPanel Arduino suite](../../README.md).

## Status

Compile-ready, **not yet observed on hardware**. The sketch builds green for the
real ESP32-P4 target (baseline, display, Wi-Fi, and display+Wi-Fi), but no panel
has been attached to this session — no screen, touch, or HTTP call has been seen
running. The hosted-C6 Wi-Fi link itself was proven on this panel by Project 14,
so the underlying transport is known to work, but this project's own web server,
GPIO controller, and touch UI still need their own on-device smoke test. The
`selftest` serial command is the offline functional check that runs with no panel
attached. See the [technical reference](TECHNICAL.md).

## What you get

A touch-driven, five-screen control surface (tap the bottom tabs to switch):

- **Devices** — a grid of controllable devices; tap a card to toggle its
  relay/GPIO, or tap DETAILS to open one up
- **Detail** — one device's live state, HTTP target URL, last command + result,
  and explicit ON / OFF / TOGGLE buttons
- **Sensors** — incoming node readings with battery/temperature ring gauges and a
  per-sensor temperature sparkline
- **World** — live weather, the newest M4.5+ earthquake, the aurora Kp verdict,
  and air quality, with a REFRESH button
- **Events** — a scrolling log of everything the hub has done

Every touch action has a matching serial command, so the whole demo drives from a
keyboard too. An inbound web server lets nodes POST readings and self-register at
runtime; an outbound HTTP controller flips remote GPIO pins. Offline, a synthetic
sensor source and two demo devices keep every screen alive.

## Data sources

The world strip pulls directly over HTTPS with no API key from
[Open-Meteo](https://open-meteo.com/), [USGS](https://earthquake.usgs.gov/), and
[NOAA SWPC](https://www.swpc.noaa.gov/). Set your location in `config/Location.h`.
With Wi-Fi off, the strip shows canned data so the demo still runs.

## Privacy and responsible use

This hub controls devices you own on your own network. It does not scan for
networks, join anything it was not given, or capture credentials. The panel's web
server has no authentication — keep it on a trusted LAN, and keep the files
containing your Wi-Fi password, device hosts, and real coordinates out of Git.

## Technical reference

For installation, build flags, configuration, upload commands, device details,
file layout, troubleshooting, safety boundaries, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
