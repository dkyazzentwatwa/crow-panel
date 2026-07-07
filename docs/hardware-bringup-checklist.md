# Hardware Bring-Up Checklist

Staged sequence for the first live sessions on a real CrowPanel Advanced
7-inch ESP32-P4. One stage at a time, one flag at a time. Every stage has a
success check and a rollback. Do not combine stages — if two things change
and the board misbehaves, you no longer know which one did it.

Terminology used throughout this repo:

- **compile-verified** — builds green on `esp32:esp32:esp32p4` with core
  3.3.8. Proves nothing about the physical board.
- **hardware-verified** — observed working on the real CrowPanel. Only your
  live sessions can move code into this category.

Flags are enabled per stage via `EXTRA_FLAGS` so nothing is edited in git
while testing:

```sh
EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/upload-project.sh projects/01-fieldops-control-center /dev/cu.usbmodemXXXX
```

Rollback for every stage is the same: drop the flag from `EXTRA_FLAGS`,
re-upload, confirm the previous stage's success check still passes.

## Stage 0 — Toolchain and board identification

1. `./scripts/install-cores.sh` and `./scripts/install-libs.sh` (once).
2. Connect the board. `arduino-cli board list` should show a port
   (`/dev/cu.usbmodem*` on macOS).
3. Record the silicon revision:
   ```sh
   esptool --port /dev/cu.usbmodemXXXX chip_id
   ```
   If it reports chip revision v3.00 or newer, add `ChipVariant=postv3` to
   the FQBN for all later builds.
4. If flashing fails to start: hold BOOT while tapping RESET, then retry.
   If it still fails, switch the FQBN to `USBMode=default` (USB-OTG) —
   the default `hwcdc` assumption is compile-verified, not hardware-verified.

**Success:** a flash completes and the port re-enumerates.

## Stage 1 — Mock firmware (all flags 0)

Upload any of the three projects unmodified.

**Success:** at 115200 baud you see the boot banner, the
`printHardwareProfile` pin dump, and periodic mock ticks. Type `help` then
`status` into the Serial monitor (line ending: **Newline**) — the command
router answering is the smoke test for everything else in this repo.

## Stage 2 — Board revision confirmation

Check the silkscreen / order paperwork for the hardware revision (V1.0,
V1.1, V1.2).

- The repo default profile is `CROWPANEL_P4_7IN_V1_2` (`120`).
- If your board is V1.0/V1.1, override in the project's
  `config/ProjectConfig.h`:
  `#define CROWPANEL_HARDWARE_PROFILE CROWPANEL_P4_7IN_V1_0`
- **V1.2 warning:** the official README says V1.2 swaps the wireless socket
  pins IO53/IO54 ↔ IO27/IO28. This repo models that swap, but no V1.2
  example code exists upstream yet, so the swap direction is UNVERIFIED on
  silicon. If a V1.2 radio fails in Stage 6, the first suspects are these
  four pins — try the V1_1 profile before debugging anything else.

**Success:** the `status` command reports the profile matching the physical
board.

## Stage 3 — Display (`-DUSE_DISPLAY=1`, touch comes next stage)

Panel: EK79007 controller, MIPI-DSI 2-lane @ 1000 Mbps, 1024x600 RGB565,
backlight IO31 (PWM, active high), LCD reset IO41. The scaffold draws with
the Adafruit-GFX-style API through Arduino_GFX's DSI classes
(`shared/CrowPanelShared/DisplayBringup.cpp`) — no LVGL by design.

**Success:** backlight lights and the status screen shows the same lines the
Serial output prints.

The scaffold sends the EK79007 vendor init sequence (lane-count, power
registers, sleep-out) ported from Espressif's `esp_lcd_ek79007` driver, so
the panel should come out of sleep on its own.

**If the screen stays black** with a running sketch (Serial still ticking):
first confirm you flashed with `-DUSE_DISPLAY=1` (without it, no display
code runs at all). If the flag is set and it's still black, fall back to
porting Elecrow's own display
path — `example/V1.0/Arduino_Code/Lesson07-Turn_on_the_screen/` plus the
vendored `ESP32_Display_Panel`, `esp-lib-utils`, and `ESP32_IO_Expander`
libraries from the official repo. Keep the rest of this repo unchanged; only
`DisplayBringup.cpp` should need replacing.

## Stage 4 — Touch (GT911)

Enabled together with `USE_DISPLAY` (the touch driver lives in
`DisplayBringup.cpp`). GT911 on I2C: SDA=45, SCL=46, INT=42, RST=40. The
controller boots at address 0x5D or 0x14 depending on INT strapping — the
scaffold auto-probes both.

**Success:** tapping the screen logs coordinates on Serial AND draws a dot
where you touched (proves the coordinate mapping).

## Stage 5 — Wi-Fi (`-DUSE_WIFI=1`)

The ESP32-P4 has no radio of its own — Wi-Fi rides the onboard ESP32-C6
over SDIO (esp_hosted). Copy `config/WiFiSecrets.example.h` to
`config/WiFiSecrets.h` and fill in credentials first. Start the mock API on
the same network: `cd mock-api && npm start`, and point the project's
`*_API_ENDPOINT` at your machine's LAN IP.

**Success:** Serial shows an IP address, and `POST /events` entries appear
in the mock API (`curl localhost:8787/events?limit=5`).

**If the link never comes up:** the C6's hosted firmware version may not
match core 3.3.8's esp_hosted client. Elecrow ships an upgrade path — see
`example/CrowPanel_Advance_ESP32-P4 Display_Firmware_Upgrade_Guide/` and
`example/V1.0/Upgrade P4 to C6 firmware/` in their repo.

## Stage 6 — One radio/reader at a time

Never enable two new transports in the same flash. The wireless socket and
the readers share buses (SPI: SCK=8, MOSI=6, MISO=7; I2C: SDA=45, SCL=46).

- **LoRa** (`-DUSE_LORA_DRIVER=1`, project 01): needs a second device
  transmitting — Elecrow's `Lesson13_TX_SX1262_Wireless_Module` is the
  reference TX. Defaults: 915.0 MHz / BW 125 / SF7 / CR7 / 22 dBm (Elecrow's
  own values). EU boards: override `FIELDOPS_LORA_FREQ_MHZ` to 868.0 in
  `config/Pins.h` — transmitting on the wrong band is a regulatory issue.
  **Success:** RX log lines for each TX packet.
- **PN532** (`-DUSE_PN532_DRIVER=1`, project 03): I2C mode shares the touch
  bus (GT911 at 0x5D/0x14, PN532 at 0x24 — no conflict).
  **Success:** tapping a card prints its UID; a UID added to
  `BadgeRegistry.cpp` gets a `granted` decision.
- **MFRC522** (`-DUSE_MFRC522_DRIVER=1`, project 03): SPI, SS=10, RST=9 —
  same pins the wireless socket uses; physically remove any socket module
  first. **Success:** same as PN532.
- **ESP-NOW** (`-DUSE_ESPNOW=1`, project 01): the panel can't join ESP-NOW —
  flash the `espnow/bridge` sketch to a spare ESP32 and wire its UART to the
  panel (`ESPNOW_UART_RX/TX` in `config/Pins.h`; bridge TX→panel RX, etc.).
  Bench-test first with no radio: on the panel serial monitor type
  `feed SENSOR,ATTIC,29.5,40,88,0,-58`. **Success:** a sensor node's telemetry
  appears on the dashboard; a running cypher-chat node shows as a presence
  tile. Full flow and wiring in `espnow/README.md`.

## Stage 7 — Camera (project 02)

Not available in Arduino. `esp32-camera` does not ship for the P4 in core
3.3.8; the P4 camera path is MIPI-CSI via ESP-IDF (`esp_video`). The only
official example is `example/V1.0/idf-code/Lesson13-Camera_Real-Time`.
`-DUSE_CAMERA_DRIVER=1` compiles (the stub reports
`p4-csi-unavailable-in-arduino`) so the flag matrix stays green, but real
camera work means either waiting for core support or porting the IDF lesson.

## After every green stage

Note what was verified (board revision, flag, library versions) in your
test log. When a stage is hardware-verified, the matching "NOT
HARDWARE-VERIFIED" comment in the source can be updated — those comments
are this repo's honesty contract with viewers.
