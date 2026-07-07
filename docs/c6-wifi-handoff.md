# Handoff: CrowPanel Advance 7" ESP32-P4 — get onboard C6 Wi‑Fi working

## Goal
Make Wi‑Fi work on an **Elecrow CrowPanel Advance 7.0" ESP32‑P4** (board rev V1.1/V1.2)
with **arduino‑esp32 core 3.3.8**, so project `14-adsb-flight-tracker-radar` can fetch
live ADS‑B data. **The radar app itself is finished and works perfectly offline** (mock
build renders smooth sweep + aircraft + touch). The ONLY blocker is the P4↔C6 Wi‑Fi link.

## Current outcome
Resolved on the tested panel. The mverch67 `crowpanel-p4-70-90-101` updater was flashed from
`/Users/cypher/Downloads/crowpanel-p4-70-90-101_v0`, then project 14 was flashed with
`EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1"`. The panel booted the ADS-B radar in `LIVE`
mode, showed `src airplanes.live`, and populated 60 aircraft contacts. The C6 firmware update
is board-level; the code-side SDIO pin remap now lives in shared code for all repo Wi-Fi paths.

## Hardware/host facts
- P4 has no radio; Wi‑Fi rides an onboard **ESP32‑C6 over SDIO via esp_hosted**.
- Core 3.3.8 bundles **esp_hosted host client 2.12.3** (IDF 5.5.4). Slave (C6) must match: **2.12.3**.
- Board shipped with C6 esp_hosted **slave v2.3.0** (old; has a known Wi‑Fi‑drop bug and a
  buggy OTA RPC).
- FQBN: `esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600`
- Bundled esptool: `~/Library/Arduino15/packages/esp32/tools/esptool_py/5.2.0/esptool`
- Every `arduino-cli compile` needs `--build-property tools.ctags.cmd.path=/usr/bin/true`
  (env `CTAGS_WORKAROUND=1` in the repo scripts) — broken local ctags.
- **USB‑CDC quirk:** with `USBMode=hwcdc`, the native serial port drops the moment an app
  runs. To (re)flash you must put the board in **download mode: hold BOOT, tap RESET,
  release BOOT** — then `/dev/cu.usbmodemNN01` reappears. Do NOT read the missing port as a
  crash; check the screen. Serial monitoring of a running app is effectively impossible here
  — diagnostics print to the **panel** instead.

## Two confirmed root causes
1. **SDIO pins are wrong in the core's default P4 map.** Core defaults assume Espressif's
   Function‑EV board. The CrowPanel 7" V1.1/V1.2 wires the data lines **reversed** and the C6
   reset on a different GPIO. Correct values (verified against Elecrow's V1.2 schematic and
   their C6 upgrade guide):

   | signal | CLK | CMD | D0 | D1 | D2 | D3 | C6 reset |
   |---|---|---|---|---|---|---|---|
   | GPIO | 18 | 19 | **17** | **16** | **15** | **14** | **32** |

   Set via `WiFi.setPins(clk,cmd,d0,d1,d2,d3,rst)` = `WiFi.setPins(18,19,17,16,15,14,32)`
   **before any Wi‑Fi/hosted call** (it errors after hosted init). Note `esp_hosted_init()`
   returns 0 even with wrong pins (it only sets up the SDIO peripheral); the failure shows up
   as a hang in `esp_hosted_connect_to_slave()` (→ watchdog reboot / "blue flash").

   **This is already fixed in the repo** (so it's permanent for all 14 projects):
   - `shared/CrowPanelShared/HardwareProfile.h` — new `HostedSdioPins` struct + field on
     `HardwareProfile`.
   - `shared/CrowPanelShared/HardwareProfile.cpp` — `HOSTED_SDIO_V1_0/V1_1` values; V1.1/V1.2
     use `{18,19,17,16,15,14,32}`.
   - `shared/CrowPanelShared/HostedWiFi.cpp` — `configureCrowPanelHostedWiFiPins()` calls
     `WiFi.setPins(...)` from the active profile under `#if defined(CONFIG_IDF_TARGET_ESP32P4)`
     before `WiFi.mode()` / scans.
   - `CrowNetworkClient`, CypherDrive Wi-Fi scan, and SurveyOps Wi-Fi scan call the helper
     before touching Arduino `WiFi`.

   **Verified:** with `setPins`, `esp_hosted_init() -> 0 (link up)` and the OTA path is
   reachable — so the pin fix is correct.

2. **C6 firmware too old (2.3.0 vs required 2.12.3), and factory 2.3.0 refuses modern OTA.**
   Our Arduino OTA attempt (below) got `esp_hosted_slave_ota_begin() -> -1` from the factory
   firmware. This is a documented factory‑2.3.0 bug.

## What was tried and the results
- **Attempt A — Arduino OTA sketch** `scratchpad/c6update/c6update.ino` (sets pins, then
  `esp_hosted_slave_ota_begin/write/end/activate` with the embedded 2.12.3 image `c6fw.h`).
  Panel showed: `WiFi.setPins -> OK`, `esp_hosted_init -> 0 (link up)`,
  `C6 slave fw: unreadable (old RPC)`, **`ota_begin failed: -1`** → factory refused OTA. Safe:
  C6 untouched.
- **Attempt B — community prebuilt upgrader** `mverch67/crowpanel-advanced-p4-c6-upgrade`
  v0.0.5‑rc1, variant `crowpanel-p4-70-90-101` (a temporary P4 app that force‑OTAs the C6 over
  SDIO with a factory‑2.3.0‑compatible method). Flashed via esptool:
  `firmware.factory.bin @0x0` + `littlefs.bin @0x410000` (littlefs carries C6 v2.12.3). Flash
  verified OK, board hard‑reset, waited ~2 min. The serial output was not captured, but success
  is confirmed by the later live ADS-B build: HTTPS data populated through the hosted C6 link.
- **Verification** with `scratchpad/c6diag/c6diag.ino` (reads C6 slave version). First run was
  buggy (missing `setPins`) so it hung at `connect_to_slave()` as expected. It was then fixed
  to call `WiFi.setPins(18,19,17,16,15,14,32)` and recompiled. **User reports the corrected
  diag is "not working"** — the exact line‑4 result was not captured. **This is the critical
  missing data point.**

## Re-run diagnostic if another board fails
Reflash the corrected diagnostic and record what line 4 says (this decides everything):
```bash
# board in download mode (BOOT+RESET) first
SCR=<this scratchpad>            # or rebuild c6diag from scratchpad/c6diag/
FQBN="esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600"
arduino-cli upload --fqbn "$FQBN" --port /dev/cu.usbmodemNN01 --input-dir "$SCR/c6diag-build" "$SCR/c6diag"
```
Panel lines: 0=host client ver, 2=`esp_hosted_init`, 3=`connect_to_slave`, 4=**`C6 slave fw: vX.Y.Z`**.
Interpret line 4:
- **`v2.12.3`** → C6 is updated; the mverch OTA worked. Go flash the radar (below) — done.
- **`v2.3.0`** → mverch OTA didn't stick → do Fallback 1 or 2, and monitor its output this time.
- **Hangs at `connect_to_slave()`** (reboots) even WITH setPins → C6 not handshaking at all;
  suspect the mverch flash left the C6 half‑written, or a deeper SDIO issue → Fallback 2/3.

## Fallbacks (in order)
1. **Re‑run mverch67 and actually watch it.** Its OTA may need a full uninterrupted run or a
   specific target. Flash it, then immediately open a serial monitor on the reappearing port
   (`arduino-cli monitor -p <port> -c baudrate=115200` — it may drop, retry). Its `main.cpp`
   prints OTA phases; confirm success/failure and the reported slave version. Repo:
   `https://github.com/mverch67/crowpanel-advanced-p4-c6-upgrade` (release v0.0.5‑rc1). Assets
   already downloaded to `scratchpad/mverch/` (`firmware.factory.bin`, `littlefs.bin`,
   `install.sh`). Partition offsets: ota_0 0x10000, ota_1 0x210000, storage/littlefs 0x410000.
2. **lboshuizen tool** `https://github.com/lboshuizen/crowpanel-p4-c6-sdio-ota` — purpose‑built
   for the buggy factory **2.3.0** (uses **1‑bit SDIO @ 10 MHz**, chunked, "safe to re‑run").
   Default targets V1.0 wiring — **adjust its SDIO pins to `{CLK18,CMD19,D0=17,D1=16,D2=15,
   D3=14,reset32}`** and point it at the **2.12.3** slave binary (its default is 2.9.7). This is
   the tool most likely to beat the factory‑2.3.0 OTA refusal.
3. **Hardware UART flash of the C6 (last resort).** From the V1.2 schematic (`scratchpad/
   elecrow/sch_v12.sch/.pdf`), C6 UART test pads: **P25=C6_TXD0, P21=C6_RXD0, P36=C6_IO9(BOOT)**.
   Pull P36 low, reset the C6, then
   `esptool --chip esp32c6 --port <uart> write-flash 0x0 network_adapter_esp32c6.bin`
   (**offset 0x0**, not 0x1000). Pads may be under the screen on some units.
4. **Elecrow official ESP‑IDF kit** (`scratchpad/elecrow/host_ota/` = `host_performs_slave_ota`).
   Needs `idf.py` + ESP‑IDF 5.5.x. Edit `sdkconfig.defaults` to the V1.1/V1.2 pin block
   (`CONFIG_ESP_HOSTED_SDIO_SLOT_1=y`, `..._4_BIT_BUS=y`, `..._CLOCK_FREQ_KHZ=40000`,
   `..._PIN_CMD_SLOT_1=19`, `..._CLK_SLOT_1=18`, `..._D0_SLOT_1=17`, `..._D1_4BIT_BUS_SLOT_1=16`,
   `..._D2_4BIT_BUS_SLOT_1=15`, `..._D3_4BIT_BUS_SLOT_1=14`, `..._GPIO_RESET_SLAVE=32`), then
   `idf.py set-target esp32p4 && idf.py -p <port> flash monitor`.

## After the C6 shows 2.12.3 — flash the radar (the finished deliverable)
```bash
ROOT=/Users/cypher/Documents/GitHub/crow-panel
FQBN="esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600"
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1" \
  ./scripts/upload-project.sh projects/14-adsb-flight-tracker-radar /dev/cu.usbmodemNN01
```
(WiFi build already includes the `setPins` fix.) Also put your real coordinates in
`projects/14-adsb-flight-tracker-radar/config/Location.h` — it currently shows the JFK
placeholder (`40.6413,-73.7781`). The header will read LIVE and planes should populate within
~15–20 s. If Wi‑Fi connects but the sketch reboots, first suspect: the poll task is bringing
Wi‑Fi up on core 0 (`src/AdsbClient.cpp taskLoop`) — that's intentional so a flaky link can't
blank the render loop; a persistent reboot means the C6 handshake still fails.

## Prebuilt 2.12.3 slave binaries (re‑downloadable; scratchpad copies are session‑only)
- Espressif's own feed (what the core uses): `https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.12.3.bin`
- esphome mirror: `https://github.com/esphome/esp-hosted-firmware/releases/download/v2.12.3/network_adapter_esp32c6.bin`
- Elecrow's bundled copy is inside `host_performs_slave_ota.zip` → `components/ota_littlefs/slave_fw_bin/network_adapter.bin` (verified 2.12.3).

## Key references
- Elecrow 7" repo + `example/CrowPanel_Advance_ESP32-P4 Display_Firmware_Upgrade_Guide/`
  (`https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen`)
- Elecrow forum (staff‑confirmed upgrade): `https://forum.elecrow.com/discussion/28352`
- Elecrow issue #5 (factory OTA ESP_FAIL) and HA community thread
  `https://community.home-assistant.io/t/esp32-c6-co-processor-firmware-on-the-crowpanel-7-esp32-p4/986735`
- esp_hosted host API headers: `~/Library/Arduino15/packages/esp32/tools/esp32p4-libs/3.3.8/include/espressif__esp_hosted/host/`
  (`esp_hosted.h`, `esp_hosted_ota.h`, `esp_hosted_api_types.h`).

## Repo changes already made for project 14 (unrelated to the C6 blocker; all compile green)
`projects/14-adsb-flight-tracker-radar/` (full project), shared `CrowNetworkClient::httpGet`,
`HardwareProfile` HostedSdioPins, `scripts/project-registry.sh` + `scripts/check-flag-matrix.sh`
(P14 rows), `.gitignore` (`**/config/Location.h`). Nothing committed yet.
