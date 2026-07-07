# 14 — ADS-B Flight Tracker Radar Dashboard

A live multi-screen dashboard for the CrowPanel: a rotating scope plots real
aircraft around your location, then `NEXT` pages through weather, earthquakes,
aurora/Kp, and air-quality screens. Data comes from free, keyless public APIs:
[airplanes.live](https://airplanes.live) point API (with [adsb.fi](https://adsb.fi)
as a fallback), Open-Meteo, USGS, and NOAA SWPC — just Wi-Fi and your home
coordinates, no SDR.

Rendered with Arduino_GFX only (no LVGL), like the rest of the suite.

## What it shows

- **Radar scope** (left): concentric range rings (labelled to the selected range),
  a rotating green sweep with a fading comet-tail, and aircraft as heading-rotated
  triangles that flare as the sweep passes and fade until the next pass. Altitude
  colors: blue ≥30,000 ft · green ≥10,000 · amber ≥0 · red unknown.
- **Detected aircraft** (right): the nearest aircraft, each with callsign, type,
  altitude, speed, distance, bearing, and an altitude dot.
- **Location + clock**: your site name and coordinates, and an NTP-synced clock.
- **Header/footer**: contact count, a tappable `RANGE` pill, LIVE/MOCK state, data
  source, and the current sweep bearing.
- **World dashboards**: Weather, Earthquakes, Aurora Watch, and Air Quality, each
  with last-good data plus visible fetch errors while a feed is waiting/failing.

Tap an aircraft (on the scope or in the list) for a detail card; tap the `RANGE`
pill to cycle the outer ring through 20 / 40 / 60 / 80 / 100 km. Tap `NEXT` to
advance through the dashboard screens.

## Data source

`https://api.airplanes.live/v2/point/<lat>/<lon>/<radiusNM>` — keyless, ~1 req/s,
ADSBExchange-v2 JSON. HTTPS is handled by the shared `CrowNetworkClient::httpGet`
(WiFiClientSecure + setInsecure). Fetching runs on a background FreeRTOS task
(core 0) so the sweep stays smooth; set `-DADSB_POLL_TASK=0` for an in-loop poller.

**Mock-first:** with no flags the project synthesizes ~8 aircraft (`MockAdsbSource`)
so the radar and list are fully alive offline. Both the mock and the live client
feed the same mutex-protected `AircraftStore`, so the display code is identical.

## Serial commands

| Command | Description |
|---|---|
| `status` | uptime, heap, flags, contact count, range, Wi-Fi + poll-task stack |
| `planes` | dump current aircraft, nearest first |
| `range <km>` | set the outer radar ring (5–400; rings are drawn at km/5) |
| `poll <sec>` | live fetch interval (1–120; the feed allows 1 req/s) |
| `mock` | report the active data source |
| `screen next\|radar\|weather\|quake\|aurora\|air` | switch dashboard screens |

## Compile

Build from the repo root (needs `CTAGS_WORKAROUND=1` for the local ctags issue):

```bash
# Offline radar (synthetic aircraft, renders on the panel)
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/compile-all.sh

# Live radar (real aircraft over Wi-Fi)
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1" ./scripts/compile-all.sh

# Serial-only mock (no display), the default
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
```

Flags: `USE_DISPLAY=1` (render to the panel), `USE_WIFI=1` (live feed),
`ADSB_POLL_TASK` (default 1; `0` = in-loop poller).

## Upload

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" \
  ./scripts/upload-project.sh projects/14-adsb-flight-tracker-radar /dev/cu.usbmodemXXXX
```

Note: on this P4 the native USB-CDC serial port drops out once the app takes over
the port, so a serial monitor may lose the connection after boot — the app keeps
running. Re-plug / reset to get the port back before re-flashing.

## Live data setup

Copy the two example config files (both gitignored) and edit them:

```bash
cd projects/14-adsb-flight-tracker-radar/config
cp WiFiSecrets.example.h WiFiSecrets.h   # your SSID / password
cp Location.example.h    Location.h      # your home lat/lon + query radius (NM)
```

Optional overrides live in `config/ProjectConfig.h`: `ADSB_SITE_NAME`, `ADSB_TZ`
(clock timezone), `ADSB_RANGE_KM`, `ADSB_POLL_INTERVAL_MS`,
`ADSB_WORLD_KP_THRESHOLD`, and the API base URLs. The committed default home is a
JFK placeholder.

## Hardware / rendering notes

- Target: CrowPanel ESP32-P4, 1024×600 MIPI-DSI, GT911 touch; Wi-Fi via the onboard
  ESP32-C6. `FIELD-PROVEN` on the tested panel after updating the C6 to hosted v2.12.3:
  LIVE mode populated from airplanes.live with 60 aircraft contacts.
- The DSI panel is a single directly-scanned framebuffer, so the scope is composited
  in an **offscreen buffer** (`RadarScope`) and blitted once per frame. The buffer is
  allocated in **internal SRAM** when it fits (much faster than PSRAM); the scope size
  is kept modest for that reason. If the sweep still tears on your unit, the next
  lever is double-buffering the shared DSI panel (`num_fbs=2`).

## What to film

1. Boot into the offline build — the sweep rotates and ~8 aircraft light up and fade
   as it passes; the list on the right sorts by distance.
2. Tap a plane → detail card. Tap the `RANGE` pill → rings rescale 20→100 km.
3. Add `WiFiSecrets.h` + `Location.h`, rebuild with `-DUSE_WIFI=1`, and watch real
   traffic around you appear — cross-check against the airplanes.live map.
4. Tap `NEXT` through Weather, Earthquakes, Aurora, and Air Quality; each should
   show live values or a concrete feed error.
