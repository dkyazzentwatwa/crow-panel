# Hardware Risk Register

Known unknowns going into live hardware testing, with mitigations. Each row
is something that compiles green today but could still fail on the bench —
see `docs/hardware-bringup-checklist.md` for the staged sequence that
isolates them.

| # | Risk | Why it's a risk | Mitigation / label |
|---|------|-----------------|--------------------|
| 1 | V1.2 wireless pin swap (IO53/54 ↔ IO27/28) unverified on silicon | Official README describes the swap, but no V1.2 example code exists upstream to confirm direction | Swap lives only in the `PROFILE_V1_2` data; Stage 2 gate; V1.0 pins are confirmed against Elecrow's `board_config.h`. If a V1.2 radio fails, try the V1_1 profile first |
| 2 | ESP32-C6 hosted firmware version vs core 3.3.8 esp_hosted client | P4 Wi-Fi rides the C6 over SDIO; a version mismatch breaks the link at runtime, not compile time | RESOLVED on the tested panel: mverch67 `crowpanel-p4-70-90-101` updater + C6 v2.12.3 produced live ADS-B traffic. Still verify each new board before claiming Wi-Fi support |
| 3 | esp_hosted SDIO pin routing on the CrowPanel | Compile can't see whether Elecrow wired the default SDIO slot | RESOLVED in code for V1.1/V1.2 via `HardwareProfile.hostedSdio` and `configureCrowPanelHostedWiFiPins()` before every repo WiFi path |
| 4 | Repo renders via the Adafruit-GFX-style API while Elecrow's official examples use LVGL/ESP32_Display_Panel | Divergence from the vendor's reference path means their example behavior isn't a 1:1 template | Intentional design choice (simpler for the tutorial). The DSI panel + timings are identical either way; if the Arduino_GFX path fails on hardware, the Lesson07 fallback in the checklist still applies |
| 5 | EK79007 panel init | Arduino_GFX ships no EK79007 init table; without one the panel resets but stays asleep (black screen) | RESOLVED in code: `DisplayBringup.cpp` supplies the EK79007 vendor init (lane-count + power regs + sleep-out) ported 1:1 from Espressif's `esp_lcd_ek79007` driver. Still not hardware-verified — if the screen stays black, Stage 3 fallback is porting Elecrow Lesson07 + `ESP32_Display_Panel`; only `DisplayBringup.cpp` needs replacing |
| 6 | `ChipVariant` prev3 vs postv3 | Wrong variant can affect boot on newer silicon | Stage 0: `esptool chip_id`, add `ChipVariant=postv3` to the FQBN if rev ≥ 3.00 |
| 7 | `USBMode=hwcdc` assumption | Chosen from typical P4 dev-board behavior, not from a CrowPanel | Stage 0 fallback: BOOT-hold, then `USBMode=default` (USB-OTG) |
| 8 | Shared buses: SPI (radio socket + MFRC522), I2C (GT911 + PN532) | Two transports on one bus can interfere during bring-up | "One transport at a time" rule in Stage 6; PN532 (0x24) and GT911 (0x5D/0x14) addresses don't collide; remove socket modules before MFRC522 tests |
| 9 | Arduino_GFX 1.6.5 provenance | The locally installed copy contains the P4 DSI classes; a reinstall from Library Manager should match, but wasn't proven | Version pinned in `libraries.txt`; if reinstalled, re-run `scripts/check-flag-matrix.sh` before flashing |
| 10 | Mock JSON built by string concatenation is unescaped | A `"` typed into a serial-injected QR/UID would break the posted JSON | Commented at each `postEvent` call; acceptable for mock demos; switch to ArduinoJson serialization when real Wi-Fi ingestion matters |
| 11 | ESP-NOW unavailable on the P4 | `CONFIG_ESP_WIFI_REMOTE_ENABLED=1` compiles the Arduino ESP-NOW library out — the panel can't be a mesh peer | By design: a plain-ESP32 bridge runs the radio and feeds the panel over UART (`espnow/`). Panel side is UART-only, so it compiles/flashes normally |
| 12 | ESP-NOW bridge UART pins on the CrowPanel unverified | `ESPNOW_UART_RX/TX` default to -1 (Serial1 defaults) until set | Set in `config/Pins.h` against the board silk; must not clash with DSI backlight/reset, touch I2C, or the wireless socket |
| 13 | Mesh crypto mode / passphrase mismatch | cypher-chat runs compat-HMAC or AES-GCM; wrong passphrase or channel = the bridge hears nothing | Bridge reuses cypher-chat's `MeshManager` (handles both modes); match passphrase (default `123456`) and channel 1. Confirm at bring-up |

Unlisted by design: camera on P4 under Arduino — not a risk but a verified
impossibility in core 3.3.8 (see checklist Stage 7).
