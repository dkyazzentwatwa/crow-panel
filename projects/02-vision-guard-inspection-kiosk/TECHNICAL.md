# CrowPanel Vision Guard Inspection Kiosk Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at projects/02-vision-guard-inspection-kiosk.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in projects/02-vision-guard-inspection-kiosk/TECHNICAL.md.
```

---

A touch-first inspection console for the CrowPanel Advanced 7-inch ESP32-P4
(1024x600 MIPI-DSI, GT911 touch). The UI is a project-local `VisionGuardUi`
class that owns all rendering on `CrowDisplay::canvas()` through the shared
`Widgets::` toolkit (dark "ops" palette, FreeSans fonts, cards/bars/pills, the
`headerBar`/`tabBar` chrome). No LVGL, no bitmap `setTextSize` text, no
`CrowDisplay::setLine`.

The default build is a headless Serial demo (`USE_DISPLAY=0`); the same UI class
and state machine run without a panel and every action is reachable over Serial.
Adding `-DUSE_DISPLAY=1` compiles the panel renderer and touch.

## Architecture

- `InspectionWorkflow` — the domain model. Owns the static seven-item checklist,
  the current run under review, and an in-session ring of completed runs
  (`kHistoryCap = 16`, newest first, fixed-size POD entries — no heap churn).
  Runs are auto-evaluated deterministically from a hash of the QR string.
- `MockCameraManager` / `CameraManager` — the camera source. The mock feeds
  believable status; the real driver path stays behind `USE_CAMERA_DRIVER` and
  honestly reports `p4-csi-unavailable-in-arduino`.
- `QrScanner` — the background mock scanner (grows the audit log on a Throttle).
- `VisionAiClient` — outcome-aware note generator; POSTs to the summary endpoint
  only when Wi-Fi is built in and connected, otherwise composes a local note.
- `VisionGuardUi` — rendering + navigation only. `tick()` reads touch and the
  model and returns a typed `VisionEvent`; the sketch executes it and mutates
  the model. The UI never mutates inspection state directly.

## Screens

Navigated by the bottom tab bar (`Widgets::tabBar`/`tabHit`); the header shows a
live pass/fail pill for the current run.

| Screen | Purpose |
|---|---|
| **Live** | Camera status card + honest placeholder viewfinder (no fake frame; the CSI stub reason is drawn on it) |
| **Scan** | Mock decode panel (data glyph, symbology, length, checksum) + CAPTURE |
| **Checks** | Seven tappable checklist rows, progress bar, P/F/S counts, RE-EVALUATE / VIEW RESULT |
| **Result** | Pass/fail hero, verdict + AI note, per-item outcome summary, PASS/FAIL/SKIP counts |
| **History** | Paged audit log of past runs; tap a row to re-open it in Result |

## Touch controls

Navigation and buttons key off `CrowTouch` release edges, so a drag that starts
on one control and ends elsewhere fires nothing.

| Screen | Control | Action |
|---|---|---|
| any | bottom tab | switch screen |
| Live / Scan | CAPTURE button | capture next code, run a checklist, open Checks |
| Checks | checklist row | cycle that item pass → fail → skip |
| Checks | RE-EVALUATE | re-roll the current run's items |
| Checks | VIEW RESULT | open the Result screen |
| Result | RE-SCAN | capture a new code |
| History | run row | re-open that run in Result |
| History | PREV / NEXT | page the audit log (7 rows per page) |

## Serial commands

115200 baud, line ending **Newline**. Every touch action has a 1:1 command; all
commands run against the same model the panel renders.

- `help` / `status` — shared; `status` also prints the camera stub, run count,
  current screen, and the current run's verdict
- `history` — list recorded inspection runs, newest first
- `scan [text]` — capture a code (given text or an auto `INSPECT-N`), run the
  checklist, and open Checks (the touch CAPTURE path)
- `check <0-6> [pass|fail|skip]` — set a checklist item; omit the state to cycle
  it (the tap-a-row path)
- `eval` — re-evaluate the current run's checklist (the RE-EVALUATE button)
- `open <age>` — re-open a history run in Result, `0` = newest (the tap-a-row
  path in History)
- `screen [live|scan|checks|result|history]` — show or switch a screen and print
  its contents (the tab bar)
- `touch` — print raw + mapped touch coordinates, tap count, and current screen
- `selftest` — drive the mock flow end-to-end headlessly, printing explicit
  `PASS`/`FAIL` lines and a summary

### Serial smoke (mock, no panel)

```text
status
scan INSPECT-CUSTOM-1
check 2 fail
eval
screen result
history
open 3
touch
selftest
```

`selftest` records a run, cycles and forces item states, re-evaluates, re-opens
an older run, exercises navigation, and asserts the camera stub stays honest —
then prints `[selftest] summary: N passed, 0 failed -> PASS`.

## Feature flags

- `USE_DISPLAY=1` — the touch UI. Without it the project is Serial-only and runs
  the identical state machine.
- `USE_WIFI=1` — enables the network client; `VisionAiClient` POSTs to the mock
  API's `/summary` endpoint when connected, else falls back to a local note.
- `USE_CAMERA_DRIVER=1` — still compiles the honest stub (see below); it does not
  enable a real camera.

## Build

The local ctags is broken; the `tools.ctags.cmd.path` property is required.

Headless baseline (suite default FQBN):

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  projects/02-vision-guard-inspection-kiosk
```

Display build (adds the panel renderer + touch):

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --build-property "compiler.cpp.extra_flags=-DUSE_DISPLAY=1" \
  projects/02-vision-guard-inspection-kiosk
```

Or use the suite scripts:

```sh
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/compile-all.sh
```

The shared flag matrix (`scripts/check-flag-matrix.sh`) exercises this project's
`baseline`, `display`, `wifi`, and `camera` rows.

## Upload

```sh
arduino-cli board list
../../scripts/upload-project.sh projects/02-vision-guard-inspection-kiosk /dev/cu.usbmodem101
```

## Camera integration notes

There is deliberately no camera driver: `esp32-camera` does not ship for the
ESP32-P4 in Arduino core 3.3.x (the P4 camera path is MIPI-CSI via ESP-IDF's
`esp_video`). The only official example is Elecrow's IDF lesson
`example/V1.0/idf-code/Lesson13-Camera_Real-Time`. `USE_CAMERA_DRIVER=1` still
compiles — the stub reports `p4-csi-unavailable-in-arduino` and the Live screen
draws that reason on the placeholder viewfinder — so the flag matrix stays
honest. The UI shows a synthetic status stream and a viewfinder frame; it never
renders a photographed image. Real camera work means waiting for core support or
porting the IDF lesson.

## AI vision API stub

`VisionAiClient` returns believable, outcome-aware notes. When `USE_WIFI` is
built in AND the client is connected it POSTs the prompt to the mock API's
`/summary` endpoint; otherwise it composes a local note (fail notes name the
failing check). Replace with a real API client only after timeouts and offline
fallback are designed.

## Proof states

- `compile-ready`: baseline, display, `wifi`, and `camera` rows all compile
  green under the suite FQBN. (This is the current state.)
- `panel-observed`: **not done** — no CrowPanel is attached to this workspace, so
  no screen, touch, or redraw has been seen on hardware. Every screen is drawn
  from mock data and has not been visually verified.
- `field-proven`: **not done** — end-to-end capture → checklist → result → audit
  on a physical panel with real touch.

## What to film

- Serial boot showing the hardware profile and the seeded audit log.
- The five screens via the tab bar; the honest Live placeholder with the CSI
  stub reason visible.
- A CAPTURE run: tap through the checklist, watch the progress bar and header
  pill update, then VIEW RESULT.
- `selftest` printing green `PASS` lines, then `history` and `open 2`.
- The honest-scaffold beat: why the P4 camera path is IDF-only for now.
