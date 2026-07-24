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
| 14 | Cypher Flock UART candidates IO48/IO47 and BW16 PB1/PB2 unverified | Project 16 needs two sustained bidirectional 3.3 V UART links across three separately powered boards | Confirm exact silks/revisions; ESP32 TX17→P4 RX48, RX16←P4 TX47; BW16 TX PB1/pin4→ESP32 RX32, RX PB2/pin5←ESP32 TX33; shared ground, no joined 5 V rails |
| 15 | BW16 raw 5 GHz promiscuous capture is compile-only | AmebaD APIs and channel 36 compile, but raw reception, hopping reliability, and fallback transitions have not run on this module | Prove AP scans first, then raw counters on channels 1 and 36, then target hopping; retain `scan-fallback` and the separate `dual-band-proven` state |
| 16 | Project 16 C6 witness async scan is compile-only | The hosted C6 link is proven for Project 14 HTTPS, but asynchronous passive scan completion and rich AP records have not been observed on this exact screen path | Enable only `USE_DISPLAY` + `USE_FLOCK_C6_WITNESS` first; prove rows, pagination, touch responsiveness, and zero changes to Flock counters before the full build |
| 13 | Mesh crypto mode / passphrase mismatch | cypher-chat runs compat-HMAC or AES-GCM; wrong passphrase or channel = the bridge hears nothing | Bridge reuses cypher-chat's `MeshManager` (handles both modes); match passphrase (default `123456`) and channel 1. Confirm at bring-up |
| 17 | Project 21 USB-OTG HID enumeration on the CrowPanel USB-C | `USE_USB_HID=1` (`USBMode=default`) compiles and links the TinyUSB HID stack, but whether the panel's USB-C presents a keyboard/mouse to a Mac was previously only inferred from the working `USBMode=default` upload path (row 7) | RESOLVED on the tested panel: the live build flashed and the Mac enumerated `ESP32P4_DEV` (VID 0x303A, PID 0x2) as a composite HID device with all three collections bound by `AppleUserHIDEventDriver` — Keyboard (usage page 1/6), Mouse+Pointer (1/2, 1/1), and Consumer Control (12/1). The USB-C therefore carries the P4 OTG DP/DM lines. Remaining for full `host-proven`: observe typed characters, macro-preset shortcuts, media keys, and cursor moves actually landing in a host app |

| 18 | Audio amp-enable (IO30) polarity | The repo's `AudioPins.controlActiveHigh` was set `true` (active-high) on a spec guess; every audio project (09/18/20) drives the amp from that flag, so all inherited it — the wrong polarity mutes the NS4168 while I2S streams, i.e. silent-but-"working" | RESOLVED (V1.2, 2026-07-23): IO30 is **active-LOW** — Elecrow `Lesson12` `board_config.h` sets `AUDIO_POWER_ENABLE (LOW)` for V1.0/V1.1/V1.2. Flipped `AUDIO_OUT.controlActiveHigh = false` in `shared/CrowPanelShared/HardwareProfile.cpp`; project 09 then played out of the speaker. One fix covers all three audio projects |

| 19 | Project 21 Bluetooth-LE HID via the C6 (`USE_BLE_HID`) | The P4 has no BT radio; BLE HID runs as NimBLE-on-P4 with the C6 as controller over esp_hosted VHCI. Compile-support depends on the C6 slave firmware including BT, and the `WiFi.setPins()` ordering | RESOLVED on the tested panel: core 3.3.8 enables it (`CONFIG_BT_NIMBLE_ENABLED` + `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE` + `CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI`); a standalone spike paired `CypherKeys BLE` with macOS (no passkey) and delivered keystrokes, so the C6 2.12.3 slave has BT. `BleTransport::begin()` calls `WiFi.setPins(hosted SDIO)` before `BLEDevice::init` (mandatory). Note: this core is NimBLE, not Bluedroid — bond-clear uses NimBLE `ble_store_clear()`, not `esp_ble_*`. Remaining: the *integrated* dual-mode build's on-device acceptance (toggle + type over BLE) |

**Correction (2026-07-24):** this register previously closed with "camera on P4
under Arduino — not a risk but a verified impossibility in core 3.3.8." That was
wrong, and the error is instructive: it generalised from `esp32-camera` having no
P4 port to the P4 having no Arduino camera path at all. The P4 does not use
`esp32-camera`. Core 3.3.8's own `esp32p4-libs` already ship and link
`libesp_driver_cam.a` (MIPI-CSI), `libesp_driver_isp.a`, `libesp_driver_jpeg.a`
and `libesp_driver_ppa.a`, with headers present — the camera is reachable from an
Arduino sketch with no third-party library. Only the SC2336 sensor register table
had to be written by hand. Project 02 is the rebuild that exercises this path.

The genuine open risks that replace it, all owned by `projects/02-cypher-vision-cam`:

| # | Risk | Why it is real | Status |
|---|---|---|---|
| 20 | AE/AWB must be hand-written | Core ships the ISP *statistics* engines (`isp_ae.h`, `isp_awb.h`) but not `esp_video`'s software pipeline controller that normally drives them. Fixed exposure on RAW8 means blown-out or black frames as light changes | OPEN — mitigated by manual exposure/gain as the committed fallback |
| 21 | RGB565 byte order into the DSI framebuffer | The CSI controller's `byte_swap_en` may not match what the DSI panel expects; Arduino_GFX carrying both `draw16bitRGBBitmap` and `draw16bitBeRGBBitmap` is the tell. One-line fix, but colour is garbled until it is right | OPEN — bring-up knob, resolve on first frame |
| 22 | MJPEG throughput over the hosted-C6 SDIO link | Streaming video is a far heavier load than any prior project put on esp_hosted. No measurement exists | OPEN — treat any fps figure as unproven until measured |
| 23 | Single DSI framebuffer vs. per-frame video | The panel has one framebuffer and no page flip, so a full-screen blit plus chrome redraw tears | MITIGATED by design — dirty-rect renderer flushes only the viewfinder region per frame |
| 24 | No documented battery ADC | The board has a PH2.0 battery connector and charger but no documented voltage-sense path, and "portable" is this project's whole point | OPEN — UI must report battery as *unmonitored* rather than estimate it |
