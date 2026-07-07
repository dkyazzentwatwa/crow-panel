# CrowPanel Vision Guard Inspection Kiosk

Camera-based inspection and check-in kiosk concept for the CrowPanel Advanced 7-inch ESP32-P4 display.

Default mode is a Serial-only mock demo:

- Simulates camera status
- Simulates QR scans
- Runs a fake inspection checklist
- Generates pass/fail results
- Logs kiosk events
- Calls an AI vision API stub

## Core Screens

- Live Camera / Status
- Scan QR
- Inspection Checklist
- Result
- Event History
- Settings

## Serial Commands

115200 baud, line ending **Newline**:

- `help` / `status` / `history` — shared commands
- `scan [text]` — simulate a QR scan through the same pipeline the mock scanner uses, e.g. `scan INSPECT-CUSTOM-1`. Every 4th scan fails the checklist.

## Compile

```sh
../../scripts/compile-all.sh
```

The default FQBN targets the real ESP32-P4 (see the root README).

## Upload

```sh
arduino-cli board list
../../scripts/upload-project.sh projects/02-vision-guard-inspection-kiosk /dev/cu.usbmodem101
```

## Camera Integration Notes

There is deliberately no camera driver: `esp32-camera` does not ship for the ESP32-P4 in Arduino core 3.3.x (the P4 camera path is MIPI-CSI via ESP-IDF's `esp_video`). The only official example is Elecrow's IDF lesson `example/V1.0/idf-code/Lesson13-Camera_Real-Time`. `USE_CAMERA_DRIVER=1` still compiles — the stub reports `p4-csi-unavailable-in-arduino` — so the flag matrix stays honest. Real camera work means waiting for core support or porting the IDF lesson.

## AI Vision API Stub

`VisionAiClient` returns deterministic mock results. Once `USE_WIFI` is hardware-verified it POSTs to the mock API's `/summary` endpoint; replace with a real API client only after timeouts and offline fallback are designed.

## What To Film

- Serial boot showing hardware profile.
- Mock live camera status.
- `scan INSPECT-CUSTOM-1` producing a live checklist run, then `history`.
- Explanation of why the P4 camera path is IDF-only for now (honest-scaffold beat).
