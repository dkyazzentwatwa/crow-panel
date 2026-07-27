# CrowPanel SurveyOps Wardriver Panel Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at projects/13-surveyops-wardriver-panel.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in projects/13-surveyops-wardriver-panel/TECHNICAL.md.
```

---

Passive GPS/Wi-Fi survey dashboard inspired by `esp32-gps-wifi-wigle`.

V1 is mock-first. It visualizes GPS fix state, Wi-Fi AP rows, logging state,
rotation, and storage health. Hardware paths are opt-in compile scaffolds only
until the exact CrowPanel, wiring, upload, and runtime behavior are verified.

The `USE_DISPLAY=1` path now uses a SurveyOps-specific full-screen UI rather
than the shared generic tile grid: an animated passive survey scope, AP row
list, GPS fix card, WiGLE/logging detail panel, touch row selection, and a
compact footer with session scan, AP observation, logging, and rotation counts.
The passive-only boundary remains documented here and in the README.

This port is passive survey/logging only. It does not join networks, capture
credentials, inject packets, deauth clients, or run active tests.

## Feature Flags

Build flags are passed through `EXTRA_FLAGS`:

```sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_GPS_DRIVER=1" ./scripts/compile-all.sh
```

| Flag | What it gates | Proof state |
|---|---|---|
| `USE_DISPLAY=1` | SurveyOps full-screen scope, AP list, GPS card, logging panel, and touch row selection | compile-ready only unless uploaded and observed |
| `USE_GPS_DRIVER=1` | TinyGPSPlus NMEA parsing from `Serial1` when available | compile-ready; not field-proven |
| `USE_WIFI_SCAN=1` | Passive station Wi-Fi scan rows through `WiFi.scanNetworks(... passive=true ...)` | compile-ready; not field-proven |
| `USE_SD_WIGLE_LOG=1` | WiGLE-style CSV file creation, row append, rotation, and storage health on SD | compile-ready; not field-proven |

With no flags, the project stays fully mock/passive and uses demo GPS, AP, and
CSV state.

## Wiring And Storage Assumptions

Project-local hardware defaults:

```cpp
SURVEYOPS_GPS_RX_PIN=52
SURVEYOPS_GPS_TX_PIN=51
SURVEYOPS_SDMMC_1BIT=1
SURVEYOPS_GPS_SERIAL_BAUD=9600
SURVEYOPS_WIGLE_FILE_PREFIX="/wigle"
SURVEYOPS_WIGLE_ROTATE_ROWS=200
```

When `USE_GPS_DRIVER=1`, TinyGPSPlus reads `Serial1` with GPS TX connected to
CrowPanel IO52 and GPS RX connected to CrowPanel IO51. The ESP32-P4 GPIO matrix
can route UART signals to these pins, but IO51/IO52 are also used by other
external-bus conventions in this repo. Confirm that no attached module uses
those pins, and use common ground with a 3.3 V UART GPS module. The `nmea`
Serial command can still smoke-test parsing without wiring a receiver.

When `USE_SD_WIGLE_LOG=1`, the logger mounts the internal CrowPanel card with
`SD_MMC.begin("/sdcard", true)`, matching the proven 1-bit bring-up used by
projects 22 and 09. It does not require a project-local CS pin. The CSV header
is WiGLE-style:

```text
WigleWifi-1.4,...
MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type
```

Real Wi-Fi scans can expose nearby SSIDs and BSSIDs. Treat logs as local lab
artifacts and avoid publishing them raw.

## Serial Commands

- `help` / `status` / `history`
- `gps`
- `scan`
- `log on` / `log off`
- `feed ap <ssid> <rssi>`
- `rotate`
- `storage`
- `nmea <gps sentence>`

With `USE_DISPLAY=1`, the panel performs one passive scan during boot and
rescans automatically every 10 seconds. The **SCAN NOW** button and the `scan`
Serial command run the same scan pipeline. The AP list keeps up to 40 rows from
each scan and presents four rows per page with **PREV** and **NEXT** controls.
Touch also selects AP rows or cycles the highlighted AP on the scope; it does
not start logging or any other network action.

## Serial Smoke Script

Use 115200 baud with Newline line ending.

```text
status
gps
scan
log on
scan
feed ap DemoAP -55
storage
rotate
log off
nmea $GPRMC,092751.000,A,5321.6802,N,00630.3372,W,0.06,31.66,280511,,,A*43
gps
history
```

Expected proof language:

- `compile-ready`: Arduino CLI build passed for the selected flags.
- `uploaded`: the sketch was flashed to a detected CrowPanel port.
- `field-proven`: GPS, passive scan, SD logging, and/or display behavior was
  observed on real hardware with the exact wiring and storage media documented.

Current state: **compile-ready, not uploaded or field-proven**. The default GPS
mapping and SD_MMC mount are configured, but this session has no captured GPS
fix, passive scan rows, SD CSV, or display/touch observation.
