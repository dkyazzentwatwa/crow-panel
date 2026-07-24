# Vision Guard Inspection Kiosk

A touch-first check-in and inspection kiosk for the Elecrow CrowPanel Advanced
7-inch display.

Scan a code, walk a tappable checklist, get a pass or fail with a per-item
breakdown, and keep an auditable history of every run. It ships as a full
offline demo — simulated camera status, simulated scans, and a deterministic
checklist — so you can build and film the whole kiosk flow before any camera
exists.

> This is Project 2 in the [CrowPanel Arduino suite](../../README.md).

## Status

Compile-ready only: the sketch builds green for the real ESP32-P4 target
(baseline, display, Wi-Fi, and camera-flag rows) but has **not been observed
running on a physical CrowPanel** — no panel is attached to this workspace.
Nothing here has been touch-verified on hardware.

The camera is a deliberate stub, not an unfinished feature. `esp32-camera` does
not ship for the ESP32-P4 in Arduino core 3.3.x — the P4's camera path is
MIPI-CSI through ESP-IDF's `esp_video`, and the only official example is an
Elecrow IDF lesson. The Live screen therefore renders an honest placeholder
viewfinder that says `p4-csi-unavailable-in-arduino` on its face; it never shows
a fabricated image. Real camera support means waiting for core support or
porting the IDF lesson. See the [technical reference](TECHNICAL.md).

## Screens

The panel is a five-tab console (bottom tab bar, live header pill):

- **Live** — camera status card plus an honest placeholder frame; the stub
  reason is printed inside the viewfinder, never a fake picture.
- **Scan** — QR/code capture with a mock decode panel and a CAPTURE action.
- **Checks** — the inspection items as tappable rows that cycle
  pass → fail → skip, with a live progress bar and running P/F/S counts.
- **Result** — a large pass/fail hero with the verdict, the AI vision note, and
  a per-item outcome summary.
- **History** — an auditable, paged list of past runs; tap a row to re-open its
  result.

Every screen fills with believable data at boot (the history is pre-seeded) and
every control responds with no hardware attached.

## What you get

- A live camera / status screen backed by a simulated feed and an honest stub
- QR scan entry you can drive from touch or Serial with any payload you like
- A seven-item inspection checklist with per-item state and notes
- A pass/fail result that summarizes every check, not just one line
- An in-session audit history you can page through and re-open
- A mock AI vision client that becomes a real API call once Wi-Fi is proven
- A `selftest` command that drives the whole mock flow headlessly

## Responsible use

This kiosk records inspection results locally on the panel. It does not capture
images, upload footage, or identify people. If you later wire it to a real
backend, treat the inspection log as operational data and decide deliberately
what leaves the device.

## Technical reference

For installation, build flags, configuration, upload commands, device details,
file layout, troubleshooting, safety boundaries, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
