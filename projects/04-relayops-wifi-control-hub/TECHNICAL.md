# CrowPanel RelayOps WiFi Control Hub Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at projects/04-relayops-wifi-control-hub.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in projects/04-relayops-wifi-control-hub/TECHNICAL.md.
```

---

A Wi-Fi control hub on the CrowPanel Advanced 7-inch ESP32-P4 with **no radio
module** — just Wi-Fi. Two directions:

- **In:** remote ESP32 "nodes" POST sensor data to a web server running on the
  panel (`POST /sensor`). Readings land on the Sensors screen.
- **Out:** the hub sends HTTP commands to other ESP32s to toggle their GPIO
  pins — lights, relays, fans (`GET http://<node>/gpio?pin=<n>&state=<0|1>`).

No LoRa, no ESP-NOW. Default mode is a Serial-only mock demo:

- A synthetic source feeds sensor readings every few seconds
- Two demo devices you can toggle from Serial or touch (log-only, no network)
- A live event log

The real web server and HTTP controller live behind `USE_WIFI` — compile-ready,
not hardware-verified.

## UI architecture

All rendering is a project-local `ControlHubDashboard` drawing on
`CrowDisplay::canvas()` through the shared `Widgets::` toolkit (dark ops palette,
FreeSans fonts, `headerBar()` / `tabBar()` chrome, `panel/arcGauge/hBar/sparkline/
signalBars/pill/touchButton`). No LVGL, no raw `setTextSize` bitmap text, no
`OpsDashboard`. The panel is brought up with `manualFlush=true`, so a frame draws
into the cached framebuffer and is pushed with a single `CrowDisplay::flush()`.

Navigation and buttons key off the shared debounced `CrowTouch` — specifically
`releasedEdge()` + `releaseX/Y()`, so a drag that starts on one control and lifts
on another fires nothing. A `dirty_` flag coalesces repaints; between data changes
the screen still repaints once a second to keep uptime, heap, and ages live.

`ControlHubDashboard::tick()` returns a typed `HubUiEvent`
(`kHubNone` / `kHubSetDevice` / `kHubRefreshWorld`). The sketch executes it
against the real app objects (`DeviceController`, `WorldFeedClient`) and reflects
the outcome back through `onDevice()` / `onWorldFeeds()`. **The UI never mutates
application state itself** — it only asks.

## Screens

Five screens, keyed by an enum, navigated by the bottom tab strip (Detail is a
sub-screen of Devices):

1. **Devices** — a 2-column grid of controllable devices. Each card shows the
   online dot, name, target GPIO, host, and a relay toggle. Tap the card body to
   toggle its GPIO; tap DETAILS to open it.
2. **Detail** — one device: big commanded ON/OFF state, online pill, the full
   HTTP target URL (`http://<host><path>?pin=<n>&state=<0|1>`), the last command
   and its result (`ok` / `unreachable` / `pending`), and explicit ON / OFF /
   TOGGLE buttons plus BACK.
3. **Sensors** — a live list of sensor nodes on the left; tap one to pin it into
   the right panel's battery + temperature ring gauges, humidity / signal /
   motion stats, and a `sparkline()` of that node's temperature history.
4. **World** — weather, the newest M4.5+ earthquake, the aurora Kp verdict (with
   a rising/falling trend arrow), and air quality as four cards, plus a REFRESH
   button.
5. **Events** — the most recent hub events, newest first, with ages.

## Touch controls

| Screen | Control | Action | Serial equivalent |
|---|---|---|---|
| any | bottom tab | switch screen | `screen <name>` |
| Devices | device card body | toggle that device's GPIO | `set <id> toggle` |
| Devices | DETAILS button | open the device's Detail screen | `screen detail` |
| Detail | ON / OFF / TOGGLE | command the selected device | `set <id> on\|off\|toggle` |
| Detail | `< DEVICES` | back to the grid | `screen devices` |
| Sensors | sensor card | pin that node into the gauges + sparkline | `sensor <nodeName>` |
| World | REFRESH | re-pull the feeds | `world all` |

## Serial Commands

115200 baud, line ending **Newline**. Every touch action above has a serial
equivalent; these are the full set:

- `help` / `status` — shared commands
- `devices` — list controllable devices and their HTTP targets
- `set <deviceId> <on|off|toggle>` — command a device's GPIO. `set shop-light on`
  (mock build logs the request; `USE_WIFI` build sends the real HTTP GET).
- `feed <csv>` — inject a sensor reading through the same pipeline a real
  `POST /sensor` uses. `feed SENSOR,ATTIC,29.5,40,88,0,-58` adds a telemetry
  node; `feed PRESENCE,GARAGE,-70,heartbeat` adds a presence tile.
- `world [all|weather|quake|aurora|air]` — print / force-refresh the internet
  feeds. Mock mode returns canned Eugene, OR data; `USE_WIFI` fetches live.
- `screen [devices|detail|sensors|world|events]` — switch the on-screen view (or
  print the current one). Serial parity for the touch tabs.
- `sensor <nodeName>` — pin that node into the Sensors screen's gauges and
  temperature sparkline (serial parity for tapping a sensor card). Display build
  only: the headless build keeps no on-screen list, so it reports the node as not
  pinnable. Case-insensitive; feed a node first (`feed SENSOR,ATTIC,...`).
- `touch` — print the raw + mapped touch coordinates, tap count, and current
  screen. Live points require a display build; headless prints a clear note.
- `selftest` — drive the mock flow end-to-end and print explicit `PASS`/`FAIL`
  lines plus a summary. Runs headlessly with no panel attached — it exercises the
  real `DeviceController`, the sensor CSV pipeline, and the world feeds:

  ```text
  [selftest] device registry seeded                    PASS
  [selftest] device ON commanded                       PASS
  [selftest] device OFF commanded                      PASS
  [selftest] device TOGGLE flips state                 PASS
  [selftest] telemetry CSV parses                      PASS
  [selftest] telemetry increments event count          PASS
  [selftest] presence CSV parses                       PASS
  [selftest] malformed frame rejected                  PASS
  [selftest] world weather valid                       PASS
  [selftest] world aurora valid                        PASS
  [selftest] screen navigation reachable               PASS
  [selftest] overall PASS  (11 pass, 0 fail)
  ```

## Compile

Four flag combinations are kept green (all four are rows in
`scripts/check-flag-matrix.sh`):

```sh
# baseline (Serial-only) and display, via the suite helper
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/compile-all.sh
```

| Flags | What it builds | Result |
|---|---|---|
| _(none)_ | baseline, Serial-only mock | compiles |
| `-DUSE_DISPLAY=1` | the five-screen touch UI | compiles |
| `-DUSE_WIFI=1` | real web server + HTTP controller (needs ArduinoJson) | compiles |
| `-DUSE_DISPLAY=1 -DUSE_WIFI=1` | display + live Wi-Fi ("kitchen sink") | compiles |

The default (`USE_DISPLAY=0`) build runs identically over Serial: the dashboard
renderers are no-ops and the mock source + `feed`/`set` drive everything.

Everything is compile-ready; nothing is hardware-verified until it runs on your
CrowPanel.

## Proof state

- `compile-ready`: **done** — baseline, `display`, `wifi`, and `kitchen-sink`
  (`display`+`wifi`) all compile clean for `esp32:esp32:esp32p4`.
- `selftest`: **passes offline** — `selftest` drives the device controller, the
  sensor CSV pipeline, and the world feeds headlessly and reports all-PASS with no
  panel attached.
- **not yet observed on hardware**: no panel was attached to this session. No
  screen has been seen rendering, no touch has been observed, and no real
  `POST /sensor` or outbound GPIO `GET` has been exercised on device. Screen
  layout, touch mapping, and the `USE_WIFI` web-server / HTTP-controller paths are
  the remaining on-device smoke tests.

## Upload

```sh
arduino-cli board list
../../scripts/upload-project.sh projects/04-relayops-wifi-control-hub /dev/cu.usbmodem101
```

## Wi-Fi mode

The panel has no radio of its own — Wi-Fi rides the onboard ESP32-C6
(esp_hosted). With `-DUSE_WIFI=1`:

1. Copy `config/WiFiSecrets.example.h` → `WiFiSecrets.h`, fill in your SSID/pass.
2. Copy `config/Devices.example.h` → `Devices.h`, set each device's `host`,
   `path`, and `pin`. Nodes can also self-register at runtime (`POST /register`,
   or a `control_url` field on `POST /sensor`).
3. Build with `EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1"` and flash.
4. Find the hub's IP in the boot log (`wifi connected, ip=...`), then have a
   node POST to `http://<hub-ip>/sensor`. See `mock-api/README.md` for a
   hardware-free way to try both directions.

Follow `docs/hardware-bringup-checklist.md` Stage 5 for the C6 hosted-firmware caveats.

## World feeds (weather / earthquakes / aurora / air)

The World screen shows live internet data pulled directly by the panel over
HTTPS (`WiFiClientSecure`, no key): weather (Open-Meteo), the newest M4.5+
earthquake (USGS), the planetary Kp / aurora verdict (NOAA SWPC), and air
quality (Open-Meteo). Set your location in `config/Location.h` (copied from
`Location.example.h`). Fetches are staggered and non-blocking; with `USE_WIFI=0`
the cards show canned data so the demo runs offline. The REFRESH button and the
`world` serial command both force an immediate pull. Compile-ready, not
hardware-verified.

## What To Film

- Serial boot log, then mock sensor readings filling the Sensors screen.
- `selftest` printing an all-PASS block with no panel attached.
- On the Devices grid: tapping a card flipping its relay toggle ON, then DETAILS
  opening the device's target URL and ON/OFF/TOGGLE buttons.
- `set shop-light on` flipping the same device from Serial; `devices` listing targets.
- The World screen's four cards + REFRESH, and the Events log filling in.
- With `USE_WIFI`: a `curl` POST to `/sensor` appearing on the Sensors screen, and
  a device toggle logging the outbound `GET .../gpio?pin=..&state=1`.
