# crowpanel-aiot-arduino-suite

Arduino CLI tutorial scaffold for twenty-one Elecrow CrowPanel Advanced 7-inch ESP32-P4 HMI AI Display projects.

This repo is mock-first. Every sketch boots into a Serial-driven demo you can teach, film, and iterate on — and every real driver path (Wi-Fi, LoRa, NFC/RFID, display/touch) now exists behind a feature flag as a **compile-verified scaffold**. Display rendering uses the Adafruit-GFX-style API (no LVGL by design). Two words carry this repo's honesty contract:

- **compile-verified** — builds green on the real ESP32-P4 target with esp32 core 3.3.8. Proves nothing about the physical board.
- **hardware-verified** — observed working on a real CrowPanel. Only live testing moves code into this category. See `docs/hardware-bringup-checklist.md`.

## Hardware Target

Official grounding sources:

- Elecrow product page: https://www.elecrow.com/crowpanel-advanced-7inch-esp32-p4-hmi-ai-display-1024x600-ips-touch-screen-with-wifi-6-compatible-with-arduino-lvgl-micropython.html
- Elecrow GitHub examples: https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen

Key details:

- Product: CrowPanel Advanced 7-inch ESP32-P4 HMI AI Display
- Main chip: ESP32-P4NRW32 (RISC-V up to 400 MHz + low-power core), 16 MB flash, 32 MB PSRAM
- Display: 7.0-inch IPS 1024x600, MIPI-DSI (EK79007 controller), GT911 5-point capacitive touch
- Wireless: onboard ESP32-C6 (Wi-Fi 6 / BLE 5.3) reached over SDIO via esp_hosted — the P4 itself has no radio
- Optional modules: SX1262 LoRa, nRF24-style 2.4 GHz, Zigbee/Matter/Thread
- Audio: amplifier, dual microphones, dual speakers

## The Builds

The first four builds are hardware-path teaching projects:

1. `projects/01-fieldops-control-center` — AIoT dashboard for remote field sensors. Mock packets, alerts, event log, AI-style summaries; real SX1262 scaffold (RadioLib) behind `USE_LORA_DRIVER`, and an **ESP-NOW** transport behind `USE_ESPNOW` (fed by a plain-ESP32 bridge — see `espnow/README.md`).
2. `projects/02-cypher-vision-cam` — portable touch camera. Live **MIPI-CSI viewfinder** from the SC2336 sensor, PPA-blitted 1:1 to the panel; hardware-JPEG stills and Motion-JPEG/AVI clips to SD; and a password-protected soft-AP serving an MJPEG live feed off the onboard C6. The P4 drives the camera and the panel; the C6 drives the radio. **No third-party camera library** — core 3.3.8 already ships and links the ESP-IDF camera stack (`esp_driver_cam` / `isp` / `jpeg` / `ppa`) for the P4, so the only hand-written piece is the SC2336 register table. This replaces the former Vision Guard kiosk, whose camera stub rested on a mistaken "verified impossibility" claim.
3. `projects/03-badgeops-nfc-rfid-system` — badge enrollment, attendance, and lightweight access control. Mock badge taps and policy decisions; real PN532 (I2C) and MFRC522 (SPI) scaffolds behind flags.
4. `projects/04-relayops-wifi-control-hub` — Wi-Fi control hub reusing the FieldOps dashboard, no radio module. The panel **runs a web server** so remote ESP32 nodes POST sensor data in, and **sends HTTP GPIO commands out** to toggle their lights/relays. Mock source + `set`/`feed` serial commands drive it fully offline; the real server + controller live behind `USE_WIFI`.

The newer ports are mock-first CrowPanel product surfaces inspired by sibling GitHub repos. They compile as standalone sketches and mirror their Serial-driven state onto a touch dashboard when `USE_DISPLAY=1`:

5. `projects/05-cypherdrive-wireless-ops` — safe Wi-Fi/BLE visibility console inspired by `cypher-drive`; no HID or captive-portal capture in v1.
7. `projects/07-nfc-field-lab-badgeops-pro` — combined PN532/Cypherbox Mini NFC lab; UID-only and APDU/payment boundaries stay visible.
8. `projects/08-cypher-gamer-arcade` — large touch arcade launcher inspired by `cardputer-games`; Pong, Snake, and 2048 are the v1 playable set.
9. `projects/09-cypher-tune-mpc` — touch MPC/groovebox inspired by `cardputer-mpc`; silent/mock audio until `USE_AUDIO` becomes a real hardware path.
10. `projects/10-litego-touch-coach` — playable offline 9x9 Go with a Monte-Carlo opponent, touch placement, and full rules including superko and komi.
11. `projects/11-cardrf-spectrum-console` — receive-only spectrum console inspired by `cardputer-hackrf`; no TX/replay/jamming controls.
13. `projects/13-surveyops-wardriver-panel` — passive GPS/Wi-Fi survey dashboard inspired by `esp32-gps-wifi-wigle`; no network joins or active testing.
14. `projects/14-adsb-flight-tracker-radar` — live/mock aircraft radar inspired by ADS-B tracker projects; public APIs and world-feed panels behind Wi-Fi.
15. `projects/15-pokedex-panel` — large touch Pokedex inspired by `esp32-pokedex`; offline mock catalog by default, source SD catalog behind `USE_SD_POKEDEX`.
16. `projects/16-cypher-flock-panel` — passive dual-band detector UI; a BW16 scans 2.4/5 GHz Wi-Fi, an ESP32 scans BLE and aggregates both UART streams, and the P4 renders/persists derived detections.
17. `projects/17-littlehakr-rf-lab` — touch-first nRF24 and CC1101 register-proof lab with fixed, authorized receive-only activity profiles; no TX, payload reads, IDs, replay, or jamming. The onboard C6 has separate aggregate Wi-Fi and firmware-gated BLE status pages.
18. `projects/18-cypher-desk-panel` — Cypher Desk OS creator workstation with a 3x4 app grid, reusable touch keyboard, the complete legacy Writer, local calendar/contacts/clock/calculator/files, hosted-C6 Wi-Fi setup, and honest hardware-gated media apps.
19. `projects/19-starbeam-console` — full 1:1 port of project-starbeam: native 5x nRF24 + 2x CC1101 on one shared SPI bus (jammers, 2.4 GHz spectrum, 433 MHz scan/RSSI, raw record/replay) with a touch console UI, plus the Wi-Fi/BLE/attack half proxied over UART to an ESP32 running stock starbeam_v2. Transmit is arm-gated behind a local `LabProfile.h`.
20. `projects/20-pipboy-terminal` — unofficial Pip-Boy 3000-style showpiece using only the panel's touch display, SD_MMC, hosted-C6 Wi-Fi, and onboard I2S speaker; no radio, enclosure, or sensors required.
21. `projects/21-cypher-keys-hid-deck` — HID showcase: the panel is a native keyboard, macro pad, and trackpad for a Mac. Reuses the Cypher Desk touch keyboard, adds switchable macro presets (a macOS set and a ChatGPT/Codex keypad), and drives the host over **USB (TinyUSB) or Bluetooth-LE (via the onboard C6)** with an on-screen `OUT` toggle. Real output is gated behind `USE_USB_HID` (needs `USBMode=default`) and `USE_BLE_HID`; the default build is a Serial-logging mock.

## Toolchain

```sh
./scripts/install-cores.sh   # esp32:esp32@3.3.8 (minimum 3.3.x for the P4 target)
./scripts/install-libs.sh    # RadioLib, Arduino_GFX, SensorLib, PN532, MFRC522, ...
./scripts/build-flock-bridge.sh  # generic ESP32 companion for Project 16
```

No third-party board package URL is needed — `esp32:esp32:esp32p4` ships in the official Espressif index. Verified library versions are listed in `libraries.txt`.

## Compile

The default FQBN targets the real board:

```sh
./scripts/compile-all.sh
```

```
esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600
```

Enable driver paths per build with `EXTRA_FLAGS` (these `-D` defines beat the `#ifndef` defaults and also reach the shared library):

```sh
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1" ./scripts/compile-all.sh
```

Project 16's complete three-board panel build is:

```sh
CTAGS_WORKAROUND=1 \
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_FLOCK_UART_BRIDGE=1 -DUSE_FLOCK_PERSISTENCE=1 -DUSE_FLOCK_C6_WITNESS=1" \
./scripts/upload-project.sh projects/16-cypher-flock-panel <DETECTED_PANEL_PORT>
```

Prove every supported flag combination still builds:

```sh
./scripts/check-flag-matrix.sh
```

If compilation fails with mangled prototype errors ("expected constructor, destructor, or type conversion"), your local ctags is broken — retry with `CTAGS_WORKAROUND=1` (see `docs/troubleshooting.md`).

## Upload

```sh
arduino-cli board list
./scripts/upload-project.sh projects/03-badgeops-nfc-rfid-system /dev/cu.usbmodem101
```

The script compiles and flashes in one step so the binary always matches the FQBN and shared library. First flash tips (BOOT-hold, USB mode fallback) are in `docs/hardware-bringup-checklist.md`, Stage 0.

## Serial Commands

Every sketch runs an interactive command router on Serial (115200 baud, line ending **Newline**). Shared commands: `help`, `status` (uptime, heap, profile, flags), `history` (event ring buffer, oldest first).

| Project | Command | What it does |
|---|---|---|
| 01 FieldOps | `inject [node 0-3] [tempC] [batteryPct]`, `pin`, `ack`, `screen`, `touch`, `selftest` | Simulate a packet (`inject 1 40 12` fires TEMP + LOW_BATTERY), pin/ack from Serial, drive the touch roster/detail/alerts/log screens |
| 02 Vision Cam | `cam`, `shot`, `rec`, `gallery`, `stream`, `screen`, `touch`, `selftest` | Start/stop the camera, capture a still, record a clip, list media, report soft-AP + stream state — mirrors the Live/Gallery/Stream/Settings touch screens |
| 03 BadgeOps | `tap [uid]`, `badges`, `reader`, `screen`, `touch`, `selftest` | Simulate a badge tap (`tap C2:44:10:AA` suspended, `tap 11:22:33:44` unknown), pick reader, drive the Tap/Result/Registry/Attendance/Readers screens |
| 04 RelayOps | `set <id> <on\|off\|toggle>`, `feed <csv>`, `devices`, `sensor`, `world`, `screen`, `touch`, `selftest` | Command GPIO (mock log-only; `USE_WIFI`: real HTTP), inject a reading, pin a sensor, drive the Devices/Detail/Sensors/World/Log screens |
| 05 CypherDrive | `scan wifi`, `scan ble`, `qr set/show`, `logs`, `net`, `page`, `screen`, `touch`, `selftest` | Passive Wi-Fi/BLE visibility and QR handoff across the Wi-Fi/BLE/Log/QR touch screens |
| 07 NFC Lab | `scan`, `tap`, `ndef`, `apdu`, `step`, `files`, `badges`, `screen`, `touch`, `selftest` | Safe NFC lab: UID scan, NDEF preview, step the read-only APDU trace, badge decision, files list |
| 08 Arcade | `catalog`, `play`, `move`, `step`, `reset`, `score`, `scores`, `cal`, `touch`, `selftest` | Launch and play Pong/Snake/2048, pause overlay, per-game high-score screen |
| 09 MPC | `pad`, `step`, `bpm`, `play`, `stop`, `record`, `pattern` | Touch groovebox mock transport and pattern state |
| 10 LiteGo | `play`, `pass`, `undo`, `resign`, `new`, `level`, `komi`, `score`, `bench` | Playable 9x9 Go vs a Monte-Carlo opponent |
| 11 CardRF | `scan`, `feed`, `power`, `preset`, `stop` | Receive-only mock spectrum rows |
| 13 SurveyOps | `gps`, `scan`, `log`, `feed`, `rotate` | Passive GPS/Wi-Fi survey mock console |
| 14 ADS-B Radar | `planes`, `range`, `poll`, `mock`, `screen`, `world` | Mock/live aircraft radar plus weather, quake, aurora, and air screens |
| 15 Pokedex | `browse`, `search`, `open`, `page`, `demo`, `source` | Offline Pokedex catalog browser and detail cards |
| 16 Cypher Flock | `demo`, `inject`, `bridge`, `witness`, `screen`, `filter`, `source`, `save`, `session`, `stealth` | Detector feed, C6 nearby-Wi-Fi witness, UART controls, and sessions |
| 18 Cypher Desk | `files`, `new`, `open`, `type`, `save`, `back`, `demo`, `touch`, `page`, `daily`, `scrap`, `focus`, `ritual`, `theme`, `sound`, `stats`, `time`, `storage` | Offline-first lofi notebook, scraps, focus sessions, prompt rituals, and SD-backed plain-text workspace |
| 21 Cypher Keys | `hid`, `key`, `combo`, `tap`, `preset`, `mode`, `mouse`, `click`, `scroll`, `media`, `out`, `ble` | HID deck: type, fire macro-preset shortcuts, drive the trackpad, and pick output (`out usb\|ble`); mock logs the reports, `USE_USB_HID`/`USE_BLE_HID` send them for real over USB or Bluetooth |

Injected events run the exact same pipeline as mock (and future real) drivers — one code path per project (`processPacket` / `processScan` / `processTap` / `onSensor`).

## Enable Real Hardware

Each flag gates a compile-verified scaffold. Flip ONE at a time following `docs/hardware-bringup-checklist.md`, with `docs/hardware-risk-register.md` open next to it:

| Flag | Gates | Status |
|---|---|---|
| `USE_DISPLAY` | MIPI-DSI panel + GT911 touch; status screen drawn with the Adafruit-GFX-style API (Arduino_GFX — no LVGL by design) | compile-verified |
| `USE_WIFI` | STA connect via hosted ESP32-C6 + HTTP/HTTPS clients/servers | C6 link field-proven by project 14; per-project network flows still need smoke tests |
| `USE_LORA_DRIVER` | SX1262 via RadioLib, Elecrow Lesson13 parameters (915 MHz default; EU: override to 868) | compile-verified |
| `USE_ESPNOW` | FieldOps reads sensor/presence frames over UART from an ESP-NOW↔UART bridge (the P4 can't be an ESP-NOW peer; a plain ESP32 runs the radio). See `espnow/README.md` | compile-verified |
| `USE_PN532_DRIVER` | PN532 over I2C (shares the touch bus; refuses to start until pins are set in `config/Pins.h`) | compile-verified |
| `USE_MFRC522_DRIVER` | MFRC522 over SPI (wireless-socket pins — remove socket modules first) | compile-verified |
| `USE_CAMERA_DRIVER` | Real SC2336 MIPI-CSI camera (2-lane, 288 Mbps, RAW8 1024x600@30) through the ESP-IDF CSI + ISP drivers core 3.3.8 already links. `esp32-camera` is neither used nor needed | compile- + linkage-verified; needs hardware |
| `USE_CAM_SD` | Vision Cam stills and Motion-JPEG/AVI clips to SD_MMC | compile-verified |
| `USE_AUDIO` | Cypher Tune MPC I2S/audio path, with silent mock fallback | hardware-gated |
| `USE_WIFI_SCAN` | Passive Wi-Fi scan rows for CypherDrive and SurveyOps | C6 pin/firmware path applied; scan rows still need field proof |
| `USE_BLE_UART_BRIDGE` | CypherDrive BLE sidecar feed over UART | hardware-gated |
| `USE_QR_PERSISTENCE` | CypherDrive persisted QR URL state | hardware-gated |
| `USE_SD_HIGHSCORES` | Arcade SD high-score persistence | hardware-gated |
| `USE_RF_UART_BRIDGE` | CardRF receive-only host/HackRF row bridge | hardware-gated |
| `USE_GPS_DRIVER` | SurveyOps GPS parser | hardware-gated |
| `USE_SD_WIGLE_LOG` | SurveyOps WiGLE-style CSV logging and rotation | hardware-gated |
| `USE_SD_POKEDEX` | Pokedex Panel streams the source `/pokemon/index.csv` and detail JSON from SD_MMC | hardware-gated |
| `USE_FLOCK_UART_BRIDGE` | Project 16 reads the ESP32 BLE/BW16 Wi-Fi aggregate stream and sends routed controls over UART | compile-verified; both links and pins need field proof |
| `USE_FLOCK_PERSISTENCE` | Project 16 stores CRC-v2 current/previous sessions and sanitized calibration observations in FFat, while loading v1 sessions | compile-verified; reboot recovery needs hardware proof |
| `USE_FLOCK_C6_WITNESS` | Project 16 runs asynchronous passive 2.4 GHz AP scans through the hosted C6 and displays ephemeral metadata on a separate screen | compile-verified; live scan rows need panel proof and never affect Flock confidence |
| `USE_CYPHER_DESK_SD` | Project 18 mounts SD_MMC and reads/writes the source-compatible `/cypher-puter/desk/notes/` workspace | compile-verified; mount, save, reboot persistence, and cross-device card use need hardware proof |
| `USE_CYPHER_DESK_AUDIO` | Project 18 enables generated key sounds and 16 kHz mono WAV ambience with silent failure fallback | compile target only until the speaker and mixing bench pass |
| `USE_CYPHER_DESK_MEDIA` | Project 18 indexes local music/podcast/recording folders and enables the guarded media surface | compile-ready; speaker playback remains bench-gated |
| `USE_CYPHER_DESK_RECORDER` | Project 18 compiles the recorder surface without claiming an unverified microphone path | compile-ready guard only; microphone pins, input format, record, reboot, and playback need device proof |
| `USE_STARBEAM_RADIOS` | Project 19 compiles the native 5x nRF24 + 2x CC1101 stack (RF24 + SmartRC-CC1101 libraries) on one shared SPI bus | compile-ready; per-radio register IDs and shared-SPI CS isolation need hardware proof |
| `USE_STARBEAM_COPROC` | Project 19 enables the UART link to the ESP32 dev module running stock starbeam_v2 (Wi-Fi/BLE/attack half) | compile-ready; UART round-trip and telemetry parsing need hardware proof |
| `STARBEAM_TX_CONFIRMED` | Project 19 arms all transmit (nRF24/CC1101 jammers, raw replay, forwarded attacks); must be set from a local gitignored `LabProfile.h` | disarmed by default; receive/analysis and the full UI still run without it |
| `USE_USB_HID` | Project 21 makes the panel a native USB keyboard + consumer-control + mouse (TinyUSB). **Requires a `USBMode=default` FQBN** (USB-OTG); under the default `USBMode=hwcdc` it falls back to a Serial-logging mock with a `#warning` | mock + real builds green; **host-enumerated** on a Mac (composite HID keyboard/mouse/consumer bound) |
| `USE_BLE_HID` | Project 21 also drives the host over **Bluetooth-LE** via the onboard C6 (NimBLE-on-P4, C6 as controller). Works with or without USB-OTG; combine with `USE_USB_HID` for dual-mode + an on-screen `OUT` toggle. Pairing is passkey-free (Just Works) | compile-ready (dual build ~1.0 MB); **BLE spike-proven** (macOS paired, keystrokes received); integrated dual-mode on-device acceptance pending |

Per-machine settings live in gitignored files copied from templates: `config/Pins.example.h` → `Pins.h` (radio params, reader pins) and `config/WiFiSecrets.example.h` → `WiFiSecrets.h` (credentials).

## Code Style

Two storage rules keep this readable on camera and safe on a long-running panel:

- **Transient formatting uses Arduino `String`** — events happen at seconds cadence on a chip with 32 MB PSRAM; `snprintf` boilerplate would hurt tutorial clarity for no measurable gain.
- **Long-lived storage uses fixed buffers** — the shared `EventLog` is a fixed 16-entry ring buffer of char arrays (no heap growth, ever), and the command router reads into a fixed line buffer.
- Known limit, on purpose: mock JSON built by concatenation is unescaped (commented at each `postEvent` call). Swap in real serialization before a backend ingests it.

## Mock API

`mock-api/` is a small Express server the panels can POST to once `USE_WIFI` is hardware-verified. Not required in mock mode. See `mock-api/README.md`.

## Content Roadmap

- Build a Serial-first product story (film the mock demo with serial-command beats — each project README has a script).
- Enable one hardware path at a time, on camera, following the bring-up checklist.
- Update the "NOT HARDWARE-VERIFIED" source comments as stages go green — that's the payoff arc of the series.

See `docs/content-plan.md` for the filming outline.

For the newer port folders, use `docs/full-port-proof-matrix.md` as the
source of truth for which feature flags exist, what hardware path they imply,
and what proof is still missing before any project can be called uploaded or
field-proven.

## Security Note

BadgeOps is for demos, check-in, attendance, and low-risk prototypes unless redesigned with stronger credentials and backend validation. UID-only RFID is not secure. See `docs/security-notes.md` before using RFID/NFC language in public material.
