# Full Port Proof Matrix

This table tracks the newer port folders as they move from mock-first
scaffolds toward real hardware paths. Keep the proof state literal:

- `compile-ready`: the sketch and feature flags build on the ESP32-P4 target.
- `uploaded`: the matching binary flashed to a real CrowPanel.
- `field-proven`: the real peripheral or bridge was observed through Serial
  logs, display/touch behavior, and the project smoke commands.

Do not upgrade a row beyond the evidence captured in the session log.

The shared hosted ESP32-C6 Wi-Fi link is field-proven on the 7-inch panel by
project 14 (`LIVE`, airplanes.live, 60 contacts). Direct passive scan projects
still need their own scan logs before their rows move beyond compile-ready.

| Project | Hardware-gated flags | Real path | Current proof target |
|---|---|---|---|
| 01 FieldOps Control Center | `USE_DISPLAY`, `USE_LORA_DRIVER`, `USE_ESPNOW`, `USE_WIFI` | touch roster/detail/alerts/log console (ring gauges, temp sparkline, tap-to-pin, tap-to-ack) over mock packets; real SX1262 (RadioLib) and ESP-NOW-over-UART transports behind flags | baseline + display + all flag combos compile-ready; touch nav, gauge redraw, and the radio/ESP-NOW feeds each need on-panel acceptance |
| 02 Cypher Vision Cam | `USE_DISPLAY`, `USE_CAMERA_DRIVER`, `USE_WIFI`, `USE_CAM_SD` | portable touch camera: real SC2336 MIPI-CSI (2-lane, 288 Mbps, RAW8 1024x600@30) through the CSI + ISP drivers core 3.3.8 already links, PPA-blitted 1:1 to the DSI panel; hardware-JPEG stills and MJPEG/AVI clips to SD; soft-AP + MJPEG live feed off the onboard C6. Four-tab console (Live/Gallery/Stream/Settings). Replaces the former inspection kiosk, whose camera stub rested on a mistaken impossibility claim — see risk register | **field-proven (V1.2, 2026-07-24)** end to end: SC2336 identified at 0x30 (id 0xCB3A), live viewfinder ~21 fps with correct colour, hardware-JPEG stills and MJPEG/AVI clips written to SD and verified on a computer (clip plays and seeks), BOOT button wired as a physical shutter, and a LAN browser watching the MJPEG feed at 15-20 fps. Getting there fixed five real bugs — missing black-level correction, an AWB window too narrow to observe its own distortion, blocking ISP statistics reads that made the touchscreen appear dead, a per-loop HTTP handler leak, and a stream URL hardcoded to the AP address. **Still unverified: soft-AP association** (advertises but no client associates; station mode is the proven path) and AE convergence across a wide brightness range |
| 03 BadgeOps NFC/RFID System | `USE_DISPLAY`, `USE_PN532_DRIVER`, `USE_MFRC522_DRIVER`, `USE_WIFI` | Tap/Result/Registry/Attendance/Readers touch terminal over the mock reader; PN532 (I2C) and MFRC522 (SPI) scaffolds behind flags | baseline + display + both readers + kitchen-sink compile-ready; touch nav and real tag reads each need on-panel acceptance |
| 04 RelayOps Wi-Fi Control Hub | `USE_DISPLAY`, `USE_WIFI` | Devices/Detail/Sensors/World/Log touch hub with tap-to-toggle relays and per-sensor sparklines; real web server + HTTP GPIO controller and world feeds behind `USE_WIFI` | baseline + display + wifi + kitchen-sink compile-ready; touch toggles, the server/controller round-trip, and the live world feeds each need on-panel acceptance |
| 05 CypherDrive Wireless Ops | `USE_WIFI_SCAN`, `USE_BLE_UART_BRIDGE`, `USE_QR_PERSISTENCE` | Wi-Fi/BLE/Log/QR touch console (RSSI signal bars, on-panel QR handoff block, persistent passive banner); hosted/C6 passive Wi-Fi scan, UART BLE sidecar feed, persisted QR URL behind flags | baseline + display + all flag combos compile-ready; passive scan, sidecar, and touch nav logs still to be captured |
| 07 NFC Field Lab / BadgeOps Pro | `USE_DISPLAY`, `USE_PN532_DRIVER`, `USE_MFRC522_DRIVER` | Scan/NDEF/APDU/Badge/Files touch console with a step-through read-only APDU trace; UID reads, NDEF preview, safe Type 4/NDEF APDU path behind flags | baseline + display + both readers compile-ready; reader wiring, tag taps, and touch nav still to be captured |
| 08 Cypher Gamer Arcade | `USE_DISPLAY`, `USE_SD_HIGHSCORES` | touch-playable Pong, Snake, 2048 with a `Widgets::` catalog/chrome, pause overlay, and per-game high-score screen (animated playfield via internal-SRAM offscreen canvas); optional SD high-score persistence | baseline + display + SD combos compile-ready; touch play and SD-persisted scores need on-panel acceptance |
| 09 Cypher Tune MPC | `USE_AUDIO`, optional `USE_MPC_SD` | polyphonic (8-voice) I2S sample engine driving the NS4168 speaker: boot-synthesized built-in kit + hot-swappable SD WAV kits, 4-pattern/16-step sequencer with per-step velocity, swing, metronome, record-quantize; velocity-sensitive 4x4 touch pad grid + step lane + pad-edit (vol/pitch/choke) UI; silent millis-clock fallback when `USE_AUDIO=0` | **field-proven (V1.2, 2026-07-23): display, multi-touch pads, and audible sample playback out of the speaker all confirmed on hardware.** This is what surfaced the amp-enable polarity fix (IO30 active-LOW); latency feel and `engine` underrun counter under a dense 240 BPM pattern still worth capturing |
| 10 LiteGo Touch Coach | `USE_DISPLAY` | playable 9x9 Go: Monte-Carlo opponent with difficulty levels, positional superko, komi scoring, undo/resign, preview-then-confirm touch placement, dirty-region redraw, plus Serial `selftest`/`bench`/`touchcal` | uploaded (USE_DISPLAY=1 flashed to /dev/cu.usbmodem1101, hash verified) and host-tested (28/28 rules fixtures, AI hygiene green); needs on-panel screen/touch observation, `bench`-tuned level budgets, and a game played to a result for `field-proven` |
| 11 CardRF Spectrum Console | `USE_RF_UART_BRIDGE` | receive-only host/HackRF `SCANROW` and `POWER` serial bridge | compile-ready until host bridge feed is captured |
| 13 SurveyOps Wardriver Panel | `USE_GPS_DRIVER`, `USE_WIFI_SCAN`, `USE_SD_WIGLE_LOG` | GPS parser, passive Wi-Fi scan with shared SDIO pin remap, WiGLE-style CSV logging/rotation | compile-ready until GPS, passive scan, and storage logs are captured |
| 14 ADS-B Flight Tracker Radar | `USE_DISPLAY`, `USE_WIFI` | touch radar plus airplanes.live / adsb.fi, weather, quake, aurora, and air-quality feeds | field-proven for the tested live Wi-Fi path; new changes still need their own proof |
| 15 Pokedex Panel | `USE_DISPLAY`, `USE_SD_POKEDEX` | touch Pokedex UI plus source `esp32-pokedex` SD catalog streaming | compile-ready until SD_MMC mount, touch navigation, and JSON detail browsing are captured |
| 16 Cypher Flock Panel | `USE_DISPLAY`, `USE_FLOCK_UART_BRIDGE`, `USE_FLOCK_PERSISTENCE`, `USE_FLOCK_C6_WITNESS` | BW16 passive 2.4/5 GHz Wi-Fi, ESP32 passive BLE/aggregation, C6 passive AP witness, evidence catalog, five-screen P4 UI and CRC v2 FFat sessions | compile-ready; needs all three uploads/UART plus live C6 witness rows for `uploaded`, real raw 2.4+5 GHz for `dual-band-proven`, and >=90% recall with no unexplained high-tier false positives for `field-proven` |
| 18 Cypher Desk OS | `USE_DISPLAY`, `USE_CYPHER_DESK_SD`, optional `USE_WIFI`, `USE_CYPHER_DESK_AUDIO`, `USE_CYPHER_DESK_MEDIA`, `USE_CYPHER_DESK_RECORDER` | 3x4 OS launcher, reusable app router/status/keyboard, complete Writer, Today, local calendar, contacts, clock, calculator, files, Wi-Fi settings, and guarded recorder/music/podcast/weather surfaces | baseline and display+SD+Wi-Fi builds compile-ready; launcher touch, local data persistence, hosted-C6 states, SD recovery, and microphone each need new device acceptance. Speaker hardware path now unblocked by the shared amp-enable active-LOW fix (proven on project 09); this project's own ambience/key-SFX playback still needs on-panel acceptance |
| 19 Starbeam Console | `USE_DISPLAY`, `USE_STARBEAM_RADIOS`, `USE_STARBEAM_COPROC`, `STARBEAM_TX_CONFIRMED` | full 1:1 project-starbeam port: native 5x nRF24 + 2x CC1101 on one shared SPI bus (jammers, 2.4 GHz spectrum, 433 MHz scan/RSSI, raw record/replay); Wi-Fi/BLE/attack half proxied over UART to an ESP32 running stock starbeam_v2; touch console UI. Transmit is arm-gated behind a local `LabProfile.h` | baseline/display/radios/coproc/full builds compile-ready; needs per-radio register IDs, shared-SPI CS isolation, touch zones, tear-free animated spectrum, and the UART co-processor round-trip captured on hardware before `uploaded` |
| 21 Cypher Keys HID Deck | `USE_DISPLAY`, `USE_USB_HID` (needs `USBMode=default`), `USE_BLE_HID` (C6 Bluetooth), `USE_CYPHER_KEYS_AUDIO`, `USE_CYPHER_KEYS_SD` | dual-mode HID: multi-touch keyboard with real Mac modifier chording + hold-repeat, switchable macro presets (macOS / ChatGPT-Codex / Media / Apps launcher), and a trackpad, sent as keyboard + consumer-control + mouse over **USB or Bluetooth** (on-screen `OUT` toggle); mechanical key-click audio out of the NS4168 speaker (synthesized Blue/Brown/Red profiles + real switch sample packs from SD); settings screen, boot splash, idle dim; Serial-logging mock by default | **field-proven (V1.2, 2026-07-24)** on the full `USBMode=default`+USB+BLE+audio+SD build (~1.2 MB, 38% flash / 25% RAM): macOS binds `ESP32P4_DEV` (VID 0x303A) as composite keyboard 1/6 + mouse 1/2 + consumer 12/1 and typing/macros/app-launcher/media land in host apps; BLE pairs as `Cypher Keys` (no passkey) and types wirelessly; key clicks audible from both synthesized profiles and SD sample packs; multi-touch chording, hold-repeat, settings screen and USB trackpad all confirmed on glass. **Known limit: the trackpad is USB-only** — notifying the mouse HID report over BLE panics this NimBLE/esp_hosted stack, so BLE mouse output is disabled by design (risk register row 20). Not separately confirmed by ear: the ragged-pack fallback (`mxblue`) and per-row sample pitch mapping (both host-tested) |
| 22 Cypher Boy | `USE_DISPLAY`, `USE_GB_SD`, `USE_GB_AUDIO`, `USE_GENESIS_CORE`, `USE_NES_CORE` | Game Boy / GBC player on the vendored GPLv2 gnuboy core (retro-go `4ced120`): ROMs and battery saves off SD, centred screen with a derived touch gamepad (D-pad left, round A/B right, START/SELECT below), APU sound out of the NS4168 I2S amp, 3 save-state slots with screenshot thumbnails, fast-forward, 5 themes, volume/brightness steppers, idle backlight dim, and an animated boot splash with a synthesized chime | **field-proven (2026-07-24): Pokemon Blue loaded from SD and played on the panel with touch and sound; splash, themes, save-state thumbnails, idle dim, play-time library sort and the centred gamepad all confirmed on hardware.** All 6 flag combos compile-ready with linkage verified under `<build-path>/libraries/`. Not yet exercised: GBC colour on a real `.gbc` title, cartridge RTC (Pokemon G/S/C day-night), and a battery save surviving a power cycle **Multi-system (2026-07-24):** Sega Genesis via vendored gwenesis is **playing on hardware** (fixed by using the complete 65536-entry `_full` M68K tables — the truncated ones dispatched past the end of the array — plus host-allocated VRAM). NES via vendored nofrendo is **compile-ready, never run**: `nes_emulate()` turned out to be frame-driven, so no FreeRTOS task was needed after all. All three cores in one binary: 1.34 MB flash (43% of 3 MB), 181 KB internal RAM (55%). Touch gamepad is now multi-touch (hold a direction + press a button). Unverified: any NES ROM booting, Genesis/NES audio (both deliberately silent), NES save states. |

## Safety Boundaries

- Wireless scan paths are passive visibility tools only.
- NFC APDU support is read-only lab inspection, not payment or credential work.
- CardRF is receive-only; no RF transmit, replay, jamming, or injection controls.
- SurveyOps must not join networks, deauth clients, inject frames, or collect
  credentials.
- Pokedex Panel must stay offline/source-only; it must not connect to Pokemon GO,
  scrape accounts, or imply official game-service integration.
- Cypher Flock must remain detection-only: no network joins, deauthentication,
  credential capture, payload forwarding, or claims that RSSI indicates location.
- Starbeam Console is the one transmit-capable port (a faithful 1:1 of the
  owner's own project-starbeam). Every transmit/attack path — nRF24/CC1101
  jammers, raw replay, and forwarded co-processor attacks — is disarmed unless a
  local, gitignored `config/LabProfile.h` sets `STARBEAM_TX_CONFIRMED 1`, and the
  touch UI additionally requires an authorized-use acknowledgement before arming.
  Operate only on radios, frequencies, and networks you own or are authorized to
  test.

## Coordinator Acceptance

Before calling a port done, the coordinator should have:

1. A project-local driver or parser behind explicit feature flags.
2. Mock mode still compiling and behaving as the default.
3. Serial commands exercising the same state path used by the dashboard.
4. README wiring, flags, smoke commands, and proof-state notes.
5. Green per-project compile rows and the full `check-flag-matrix.sh`.
