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
EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/upload-project.sh in-progress/01-fieldops-control-center /dev/cu.usbmodemXXXX
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
- **Cypher Flock three-board bridge** (`-DUSE_FLOCK_UART_BRIDGE=1`, project 16):
  build the ESP32 BLE aggregator and BW16 Wi-Fi scanner with
  `scripts/build-flock-bridge.sh` and `scripts/build-flock-bw16.sh`. Power all
  boards separately over USB. Connect ESP32 TX17→P4 RX48, ESP32 RX16←P4 TX47,
  BW16 PB1/pin4 TX→ESP32 RX32, BW16 PB2/pin5 RX←ESP32 TX33, and shared ground.
  IO48/IO47 and PB1/PB2 are initial candidates and must
  be checked against the board silk and Elecrow UART example before use.
  **Success:** both `hello` streams arrive, BLE reports continue during BW16
  scanning, raw counters rise first on channels 1 and 36, and controls receive
  acknowledgments. This is `uploaded`; dual-band and field proof remain separate.
- **Cypher Flock C6 witness** (`-DUSE_FLOCK_C6_WITNESS=1`, project 16): open the
  `C6 WIFI` screen or run `witness scan`, then `witness list`. **Success:** the
  hosted C6 returns passive 2.4 GHz AP rows and the screen pages without freezing
  touch. Confirm the same APs do not change Flock alerts or lifetime counters.

## Stage 7 — Camera (project 02)

**This stage previously read "Not available in Arduino." That was wrong.** The
reasoning was: `esp32-camera` has no ESP32-P4 port, therefore the P4 has no
Arduino camera path. The premise holds; the conclusion does not, because the P4
never uses `esp32-camera`. Core 3.3.8 already ships and links the ESP-IDF camera
stack for this target — `libesp_driver_cam.a` (MIPI-CSI), `libesp_driver_isp.a`,
`libesp_driver_jpeg.a` and `libesp_driver_ppa.a`, headers included. Only the
SC2336 register table had to be written by hand, and it now lives in
`shared/CrowPanelShared/Sc2336Sensor.cpp`.

Sensor facts (Elecrow `Lesson13-Camera_Real-Time` + Espressif
`esp-video-components`): SC2336 at SCCB address **0x30** on **SCL IO13 / SDA
IO12**, MIPI-CSI **2 lanes @ 288 Mbps**, **RAW8 1024x600 @ 30 fps**, no reset
pin, no power-down pin, and **no host XCLK** — the module self-clocks.

Bring-up order is load-bearing: **SD_MMC → DSI display → CSI camera.** The
camera shares the D-PHY rail (LDO channel 3, 2500 mV) that the display already
powers, and the renderer blits into the framebuffer the display owns.

Work through it in this order, and do not skip step 1:

1. **Identify the sensor before trusting any driver.** Flash the SCCB probe
   (scans `Wire1` on IO12/IO13, reads ID registers `0x3107`/`0x3108`, expects
   `0xCB3A`). Results render on the panel, because `USBMode=hwcdc` drops the
   serial port once an app runs. If the address differs from 0x30, update
   `CameraPins` in `HardwareProfile.cpp` — the driver also self-corrects and
   logs a warning, so a mismatch is visible rather than fatal.
2. **First frame.** `cam begin` then `cam grab`. This prints dimensions, corner
   pixels and a sampled mean luma, which is what distinguishes a real image from
   a buffer of zeros without a screen. Expect wrong colour and wrong brightness
   at this point — exposure is fixed and the ISP is untuned. That is correct for
   this step.
3. **Viewfinder.** `screen live`. If the image is garbled with transposed
   channels, flip `byte_swap_en` in `CameraBringup.cpp` — that is the known
   RGB565 byte-order knob (risk register #21). Read the on-screen HUD: it says
   `PPA` or `CPU`, and a `CPU` reading means the hardware blitter failed to
   register and the frame rate will be poor for that reason and no other.
4. **Exposure.** Bright room and dim room. Manual `cam exp <n>` must produce a
   usable image in both before the automatic loop is worth trusting.

`-DUSE_CAMERA_DRIVER=1` now compiles real hardware code, not a stub. Verify it
**linked**, not merely that the build was green:

```sh
grep -c "esp_cam_new_csi_ctlr" <build-path>/02-cypher-vision-cam.ino.map
```

See `projects/02-cypher-vision-cam/TECHNICAL.md` for the full pipeline.

## After every green stage

Note what was verified (board revision, flag, library versions) in your
test log. When a stage is hardware-verified, the matching "NOT
HARDWARE-VERIFIED" comment in the source can be updated — those comments
are this repo's honesty contract with viewers.
