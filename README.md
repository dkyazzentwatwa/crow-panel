# crowpanel-aiot-arduino-suite

Arduino CLI tutorial scaffold for three Elecrow CrowPanel Advanced 7-inch ESP32-P4 HMI AI Display projects.

This repo is intentionally mock-first. Every sketch boots into Serial-driven demo behavior so you can teach, film, and iterate before enabling real LVGL, LoRa, camera, NFC/RFID, Wi-Fi, or audio drivers.

## Hardware Target

Official grounding sources:

- Elecrow product page: https://www.elecrow.com/crowpanel-advanced-7inch-esp32-p4-hmi-ai-display-1024x600-ips-touch-screen-with-wifi-6-compatible-with-arduino-lvgl-micropython.html
- Elecrow GitHub examples: https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen

Known target details to keep visible while building:

- Product: CrowPanel Advanced 7-inch ESP32-P4 HMI AI Display
- Main chip: ESP32-P4NRW32
- CPU: ESP32-P4 RISC-V high-performance cores up to 400 MHz plus low-power core up to 40 MHz
- Memory: 16 MB Flash and 32 MB PSRAM
- Display: 7.0-inch IPS, 1024x600, capacitive 5-point touch
- Wireless: onboard ESP32-C6 module with 2.4 GHz Wi-Fi 6, Bluetooth 5.3, and BLE
- Optional modules: Zigbee, LoRa/SX1262, nRF24-style 2.4 GHz module, Matter, and Thread
- Interfaces: USB2.0, UART, I2C, GPIO headers, SD card holder, battery socket, speaker jack, camera header, and module headers
- Audio: amplifier, dual microphones, and dual speakers
- Official LVGL dependency listed by Elecrow: `lvgl/lvgl@9.2`

## The Three Builds

1. `projects/01-fieldops-control-center`
   - LoRa-powered AIoT dashboard for remote field sensors.
   - Mock mode generates sensor packets, alerts, event logs, and AI-style summaries over Serial.

2. `projects/02-vision-guard-inspection-kiosk`
   - Camera check-in and inspection kiosk.
   - Mock mode simulates camera status, QR scans, checklist results, and event history.

3. `projects/03-badgeops-nfc-rfid-system`
   - Badge enrollment, attendance, check-in, and lightweight access-control kiosk.
   - Mock mode simulates badge taps, registry lookup, policy decisions, and event logs.

## Hardware Revision Warning

The official Elecrow README lists hardware/software V1.2 as the latest revision and notes a wireless module socket pin reallocation:

- Original IO53 and IO54 adjusted to IO27 and IO28.
- IO27 and IO28 adjusted to IO53 and IO54.

This repo defaults to `CROWPANEL_P4_7IN_V1_2`, but every wireless module pin definition lives inside `shared/CrowPanelShared/HardwareProfile.*`. Treat those mappings as documented placeholders until you verify your exact shipped board revision and the matching Elecrow Arduino examples.

## Arduino CLI Setup

Install Arduino CLI, then install the ESP32 core:

```sh
./scripts/install-cores.sh
```

Optional library notes are in `libraries.txt`. Hardware-specific libraries are not required by default.

## Compile

The default FQBN is intentionally generic:

```sh
./scripts/compile-all.sh
```

For real CrowPanel ESP32-P4 work, replace it after installing the correct ESP32 Arduino core and any Elecrow-supported board package:

```sh
FQBN="verified:vendor:crowpanel_p4_target" ./scripts/compile-all.sh
```

If Arduino CLI fails before code compilation with a local `ctags` architecture error, retry:

```sh
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
```

## Upload

Detect your board first:

```sh
arduino-cli board list
```

Then upload one project:

```sh
./scripts/upload-project.sh projects/03-badgeops-nfc-rfid-system /dev/cu.usbserial-0001
```

Uploads are not field proof by themselves. A project is only field-proven after the real CrowPanel runs the expected Serial, display, touch, radio, camera, or badge behavior.

## Enable Real Hardware Later

Each project has `config/ProjectConfig.h` with these flags:

- `MOCK_MODE`
- `USE_LVGL`
- `USE_WIFI`
- `USE_LORA_DRIVER`
- `USE_CAMERA_DRIVER`
- `USE_PN532_DRIVER`
- `USE_MFRC522_DRIVER`
- `USE_AUDIO`
- `CROWPANEL_HARDWARE_PROFILE`

Default behavior is `MOCK_MODE = 1` and every real driver disabled. Hardware-specific includes are guarded behind those flags.

## Content Roadmap

Use this suite as a three-part tutorial series:

- Build a Serial-first product story.
- Enable one hardware path at a time.
- Film the mock demo before driver integration.
- Replace placeholders only after matching the board revision, pin map, and official Elecrow examples.

See `docs/content-plan.md` for a filming and publishing outline.

## Security Note

BadgeOps is for demos, check-in, attendance, and low-risk prototypes unless redesigned with stronger credentials and backend validation. See `docs/security-notes.md` before using RFID/NFC language in public material.
