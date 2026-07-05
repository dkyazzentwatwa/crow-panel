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

## Compile

```sh
../../scripts/compile-all.sh
```

Or compile only this sketch:

```sh
arduino-cli compile --fqbn "${FQBN:-esp32:esp32:esp32}" --libraries ../../shared .
```

## Upload

```sh
arduino-cli board list
../../scripts/upload-project.sh projects/02-vision-guard-inspection-kiosk /dev/cu.usbserial-0001
```

## Camera Integration Notes

Camera support is a placeholder until the official Elecrow Arduino camera examples are verified against the exact CrowPanel revision and FQBN. The `esp_camera` include is guarded behind `USE_CAMERA_DRIVER`.

## AI Vision API Stub

`VisionAiClient` returns deterministic mock results. Replace it with a real API client only after Wi-Fi setup, secrets handling, timeout behavior, and offline fallback are designed.

## What To Film

- Serial boot showing hardware profile.
- Mock live camera status.
- QR scan event.
- Checklist result.
- Explanation that camera integration is waiting on official Elecrow examples.
