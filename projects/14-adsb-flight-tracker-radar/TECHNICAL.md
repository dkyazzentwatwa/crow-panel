# ADS-B Flight Tracker Radar: Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the CrowPanel ADS-B Flight Tracker Radar project at
projects/14-adsb-flight-tracker-radar.

Read the repository AGENTS.md first and preserve its Arduino CLI-only structure,
mock-first behavior, and hardware proof boundaries. Do not edit unrelated
worktree changes. Keep Wi-Fi credentials and real coordinates out of Git.

Prepare the project for the Elecrow CrowPanel Advanced 7-inch ESP32-P4 with
1024x600 MIPI-DSI display and GT911 touch. The default build must remain
offline and use simulated aircraft. The optional live build may use the
onboard ESP32-C6 hosted Wi-Fi link and the public airplanes.live ADS-B feed,
with adsb.fi as the fallback. Do not add an SDR, network joining beyond the
configured Wi-Fi client, credential capture, radio transmission, or unsupported
hardware claims.

If local live testing is requested, copy and edit the ignored files
config/WiFiSecrets.example.h -> config/WiFiSecrets.h and
config/Location.example.h -> config/Location.h. Never print or commit their
contents. Use USE_DISPLAY=1 for the panel UI and USE_WIFI=1 for live feeds.

Run the appropriate compile command from the repository root:

  CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/compile-all.sh

For the live path, add -DUSE_WIFI=1. Report the result precisely as
compile-ready, uploaded, or field-proven. A successful compile is not proof of
an upload or runtime behavior. If uploading, use only a detected CrowPanel
serial port and report the exact uploader result.

At the end, summarize files changed, commands run, and the remaining proof
gaps. Keep the project README user-facing and put implementation details in
this TECHNICAL.md file.
```

This file contains the implementation, build, configuration, upload, and
verification details for
[`projects/14-adsb-flight-tracker-radar`](.). The top-level [README](README.md)
is intentionally non-technical.

## Build

Build from the repository root. The local ctags workaround is needed on the
current macOS development setup.

Offline mock build with the panel UI enabled:

```sh
CTAGS_WORKAROUND=1 \
EXTRA_FLAGS="-DUSE_DISPLAY=1" \
./scripts/compile-all.sh
```

With no feature flags, the project builds as a Serial-only mock sketch. The
mock source generates aircraft and feeds the same `AircraftStore` used by the
live client, so the display path does not change between offline and live mode.

Live build:

```sh
CTAGS_WORKAROUND=1 \
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1" \
./scripts/compile-all.sh
```

The relevant flags are:

| Flag or setting | Purpose |
| --- | --- |
| `USE_DISPLAY=1` | Enables the CrowPanel display and touch UI |
| `USE_WIFI=1` | Enables live aircraft and world-data feeds |
| `ADSB_POLL_TASK=1` | Polls live data in a background FreeRTOS task |
| `ADSB_POLL_TASK=0` | Uses the in-loop polling fallback |

## Live Wi-Fi configuration

The example files keep credentials and your real location out of Git:

```sh
cd projects/14-adsb-flight-tracker-radar/config
cp WiFiSecrets.example.h WiFiSecrets.h
cp Location.example.h Location.h
```

Edit the copied files with your Wi-Fi credentials, latitude, longitude, query
radius, and optional time zone. Both files are gitignored. The committed
coordinates are a JFK placeholder and should not be used as a home location.

Useful project settings in `config/ProjectConfig.h` include:

| Setting | Purpose |
| --- | --- |
| `ADSB_SITE_NAME` | Label shown in the location panel |
| `ADSB_TZ` | POSIX time-zone string for the clock |
| `ADSB_RANGE_NM` | Live API query radius, up to 250 NM |
| `ADSB_RANGE_KM` | Initial on-screen radar range |
| `ADSB_POLL_INTERVAL_MS` | Live aircraft polling interval |
| `ADSB_WORLD_KP_THRESHOLD` | Aurora-screen activity threshold |

The aircraft feed uses the keyless point-query format provided by
[airplanes.live](https://airplanes.live), with [adsb.fi](https://adsb.fi) as a
same-schema fallback. Aircraft polling is rate-limited to at least one request
per second; the default interval is five seconds.

The world-data screens use:

- [Open-Meteo](https://open-meteo.com/) for weather and air quality
- [USGS](https://earthquake.usgs.gov/) for recent earthquakes
- [NOAA SWPC](https://www.swpc.noaa.gov/) for geomagnetic and aurora data

## Upload

Use the exact detected USB port for the panel:

```sh
CTAGS_WORKAROUND=1 \
EXTRA_FLAGS="-DUSE_DISPLAY=1" \
./scripts/upload-project.sh \
  projects/14-adsb-flight-tracker-radar \
  /dev/cu.usbmodemXXXX
```

For live mode, add `-DUSE_WIFI=1` to `EXTRA_FLAGS`.

On the tested ESP32-P4 setup, the native USB-CDC port can disappear after the
application takes over the device. That can disconnect a Serial monitor while
the display continues running. Reconnect or reset the panel before attempting
another upload. A successful compile is not an upload or runtime proof.

## Serial commands

Use 115200 baud with newline line endings.

| Command | Description |
| --- | --- |
| `status` | Show uptime, memory, mode, contact count, range, and feed state |
| `planes` | Dump current aircraft, nearest first |
| `range <km>` | Set the display range from 5 to 400 km |
| `poll <sec>` | Set the live polling interval from 1 to 120 seconds |
| `mock` | Report the active aircraft source |
| `screen next` | Advance to the next dashboard screen |
| `screen radar` | Return to the radar screen |
| `screen weather` | Open the weather screen |
| `screen quake` | Open the earthquake screen |
| `screen aurora` | Open the aurora screen |
| `screen air` | Open the air-quality screen |

## Hardware and implementation

- Target: CrowPanel Advanced 7-inch ESP32-P4, 1024×600 MIPI-DSI display with GT911 touch.
- The P4 uses the onboard ESP32-C6 for Wi-Fi through the hosted link. The P4 itself has no radio.
- Rendering uses Arduino_GFX through the shared CrowPanel library. LVGL is not required.
- Aircraft polling runs on a background FreeRTOS task by default so network delays do not stop the radar animation.
- `RadarScope` composites the scope into an offscreen buffer before blitting it to the directly scanned DSI framebuffer.

## Project layout

```text
14-adsb-flight-tracker-radar.ino   Application entry point and Serial commands
config/ProjectConfig.h              Feed, location, timing, and UI defaults
config/*.example.h                  Safe templates for local credentials/location
src/AdsbClient.*                    Live aircraft API client
src/MockAdsbSource.*                Offline aircraft generator
src/AircraftStore.*                 Shared, mutex-protected aircraft state
src/RadarScope.*                    Animated scope renderer
src/RadarUi.*                       Display and touch integration
```

## Proof language

- `compile-ready`: Arduino CLI compilation passed for the selected flags.
- `uploaded`: the matching binary was flashed to a real CrowPanel port.
- `field-proven`: the relevant display, touch, Wi-Fi, and feed behavior was
  observed on real hardware.

The tested live Wi-Fi path is field-proven on the reference panel. New changes,
different board revisions, and other hardware paths need fresh verification.
See [`docs/full-port-proof-matrix.md`](../../docs/full-port-proof-matrix.md)
for the repository-wide proof matrix.
