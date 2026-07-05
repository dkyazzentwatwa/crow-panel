# CrowPanel FieldOps Control Center

LoRa-powered AIoT dashboard concept for remote field sensors on the CrowPanel Advanced 7-inch ESP32-P4 display.

Default mode is a Serial-only mock demo:

- Generates fake LoRa sensor packets every few seconds
- Prints dashboard card updates
- Simulates warning and critical alerts
- Logs field events
- Produces AI-style summaries through a mock client

## Core Screens

- Dashboard
- Node Detail
- Alerts
- AI Summary
- Settings

## Compile

```sh
../../scripts/compile-all.sh
```

Or compile only this sketch:

```sh
arduino-cli compile --fqbn "${FQBN:-esp32:esp32:esp32}" --libraries ../../shared .
```

The generic FQBN is not a final CrowPanel hardware target. Verify the ESP32-P4/CrowPanel FQBN before uploading to real hardware.

## Upload

```sh
arduino-cli board list
../../scripts/upload-project.sh projects/01-fieldops-control-center /dev/cu.usbserial-0001
```

## LoRa / SX1262 Plan

The official README lists SX1262 pins, but V1.2 reallocates the wireless module socket. This project reads radio placeholders through `HardwareProfile` instead of hardcoding final pins.

Enable real LoRa only after:

1. Confirming the exact board revision.
2. Confirming the exact SX1262 module and Elecrow example.
3. Updating `config/Pins.example.h` into a real local pins file if needed.
4. Setting `USE_LORA_DRIVER 1`.

## What To Film

- Serial boot log showing `CROWPANEL_P4_7IN_V1_2`.
- Mock packets turning into dashboard rows.
- A simulated alert.
- The hardware profile warning explaining why pins are revision-aware.
