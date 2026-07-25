# Repository Guidelines

This is an Arduino-CLI-only monorepo of 20 standalone sketches for the Elecrow CrowPanel Advanced 7-inch ESP32-P4 HMI AI Display. No PlatformIO, no CMake, no IDE project. `CLAUDE.md` carries the long-form architecture notes; this file is the working summary.

## Project Structure & Module Organization

- `projects/NN-name/`: one sketch per folder. `scripts/project-registry.sh` is the canonical list — add new projects there or every build script skips them. Numbering gaps (no 06, 12) are intentional.
- `shared/CrowPanelShared/`: the Arduino library every build gets via `--libraries shared`. `HardwareProfile.{h,cpp}` holds all pins, polarities, and panel timings — read pins from the profile, never hardcode a GPIO.
- `companions/`, `espnow/`: firmware for helper boards (plain ESP32, BW16). Not built by `compile-all.sh`.
- `scripts/`: install, compile, flag-matrix, upload, and host-test helpers.
- `docs/`: bring-up checklist, risk register, proof matrix, hardware notes, security notes, troubleshooting.
- `signatures/`, `mock-api/`: Flock detection catalog; optional Express API for Wi-Fi demos.

Each project keeps its `.ino` at the project root, flags and tuning in `config/ProjectConfig.h`, implementation classes in `src/`, and a `README.md` (demo script) plus `TECHNICAL.md` (wiring and proof state).

## Build, Test, and Development Commands

- `./scripts/install-cores.sh`: installs esp32 core 3.3.8 (3.3.x is the minimum for the P4 target).
- `./scripts/install-libs.sh`: installs the verified library set; exact versions live in `libraries.txt`.
- `./scripts/compile-all.sh`: compiles every registered project against the default P4 FQBN.
- `./scripts/check-flag-matrix.sh`: compiles every supported flag combination — the required regression gate.
- `./scripts/upload-project.sh projects/22-cypher-boy /dev/cu.usbmodem101`: compiles and flashes in one step so the binary matches the script's FQBN and library set.
- `./scripts/test-litego.sh --quick`, `python3 scripts/test-flock-catalog.py`, `python3 scripts/test-flock-protocol.py`, `./scripts/compile-flock-system.sh`: host-side tests that need no board.
- `./scripts/build-espnow-companions.sh`, `./scripts/build-flock-bridge.sh`, `./scripts/build-flock-bw16.sh`: companion-board firmware.
- `cd mock-api && npm install && npm start`: optional local API on port `8787`.

Env vars the scripts honor: `FQBN`, `BUILD_ROOT`, `EXTRA_FLAGS`, `EXTRA_C_FLAGS`, `CTAGS_WORKAROUND`, `CORE_VERSION`.

`CTAGS_WORKAROUND=1` is required on this machine — a broken local ctags emits mangled prototypes ("expected constructor, destructor, or type conversion") during sketch preprocessing. The workaround skips prototype generation, so **every sketch must define functions before use**.

## Feature Flags

Flags default to `0` in `shared/CrowPanelShared/AppConfig.h`. `config/ProjectConfig.h` overrides reach only the files that include it (the `.ino` and that project's `src/`) — flags gating shared code (`USE_DISPLAY`, `USE_WIFI`, …) must be passed as compiler defines:

```sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1" ./scripts/compile-all.sh
```

Two traps that produce a green build and a dead feature:

- `compiler.cpp.extra_flags` does not reach `.c` files. Projects with a flag-gated C core (project 22's vendored gwenesis) also need `compiler.c.extra_flags`; `EXTRA_C_FLAGS` appends C-only options.
- Never wrap a feature-flagged library include in `__has_include`. Verify linkage by checking `<build-path>/libraries/`, not by a green compile.

New flags need an `AppConfig.h` default **and** a row in `check-flag-matrix.sh`; a combination is only supported if it has a green row.

## Coding Style & Naming Conventions

Two-space indentation for Arduino/C++ and JavaScript. Headers guarded with `#ifndef` / `#define` / `#endif`. Classes `PascalCase`; methods and variables `camelCase`. Display code uses the Adafruit-GFX-style API through Arduino_GFX — **no LVGL, by design** — gated on `USE_DISPLAY` and `CONFIG_IDF_TARGET_ESP32P4`.

Two storage rules: transient formatting uses Arduino `String` (tutorial clarity, seconds cadence, 32 MB PSRAM); long-lived storage uses fixed buffers (`EventLog` is a fixed 16-entry ring, the command router reads into a fixed line buffer) so nothing grows the heap on a long-running panel.

Mock and real drivers must feed one pipeline per project (`processPacket` / `processScan` / `processTap` / `onSensor`) so an injected Serial event exercises the same code as a live packet.

## Testing Guidelines

`./scripts/check-flag-matrix.sh` is the required regression check; `compile-all.sh` is the quick baseline. LiteGo and Flock have real host-side suites (see the commands above) — extend those rather than flash-and-squint when logic is Arduino-free.

Serial smoke tests run at 115200 baud with line ending **Newline**; every sketch answers `help`, `status`, and `history`, and lines over 95 characters are dropped. For API changes, smoke-test `GET /health`, `POST /events`, `GET /events`, `POST /summary`, `GET /badges`, `POST /badges`, `POST /inspection`, and `GET|POST /gpio`.

## Proof Vocabulary

Keep claims literal. **compile-verified** means it builds green on the real target and proves nothing about the board; **hardware-verified** means it was observed working on a real CrowPanel. Per-project rows in `docs/full-port-proof-matrix.md` move `compile-ready` → `uploaded` → `field-proven`, and must never be upgraded past the evidence in the session log. Do not claim CrowPanel hardware support until the exact FQBN, board revision, upload, and runtime behavior are verified. When a stage goes green, update the proof matrix, the flag table in `README.md`, and the "NOT HARDWARE-VERIFIED" source comments together. Flip one flag at a time following `docs/hardware-bringup-checklist.md` with `docs/hardware-risk-register.md` open.

## Commit & Pull Request Guidelines

Use short imperative commit messages, for example `Add FieldOps demo docs`. PRs should include: summary, commands run, proof state (`compile-ready`, `uploaded`, or `field-proven`), affected project folder, and screenshots or Serial logs for UI/demo changes.

## Hardware Gotchas (verified on real hardware)

- **Audio amp enable (IO30) is ACTIVE-LOW.** The NS4168 is enabled by driving IO30 **LOW** — Elecrow's own `Lesson12` `board_config.h` defines `AUDIO_POWER_ENABLE (LOW)` on V1.0/V1.1/V1.2. Encoded once as `AUDIO_OUT.controlActiveHigh = false` in `shared/CrowPanelShared/HardwareProfile.cpp`; audio projects (09 MPC, 18 Cypher Desk, 20 Pip-Boy, 22 Cypher Boy) write `digitalWrite(profile.audio.control, controlActiveHigh ? HIGH : LOW)` and inherit the polarity — never hardcode `HIGH`. Driving it HIGH mutes the speaker while I2S keeps streaming (symptom: everything "works" but is silent). Verified on a V1.2 board 2026-07-23. Stream a block or two of silence *before* raising the enable to avoid a turn-on pop (see `AudioEngine::begin` in project 09).
- **Audio pins:** I2S LRCLK=21, BCLK=22, SDATA(DOUT)=23, amp enable=30; PDM mic CLK=24, DIN=26. NS4168 needs no codec/I2C init — raw I2S (`I2S_MODE_STD`, 16-bit stereo) is enough.
- **The P4 has no radio.** All Wi-Fi/BLE runs through the onboard ESP32-C6 over SDIO (esp_hosted). The CrowPanel wires the SDIO data lines *reversed* versus Espressif's reference board with C6 reset on IO32; `HostedWiFi` applies those pins before esp_hosted starts, and skipping it makes init failure reboot the board. The P4 also cannot be an ESP-NOW peer — that transport reads UART frames from a plain-ESP32 bridge (`espnow/README.md`).
- **The camera needs no third-party library.** Core 3.3.8 already links the ESP-IDF stack (`esp_driver_cam` / `isp` / `jpeg` / `ppa`) for the P4; `esp32-camera` does not ship for this target and is not needed. Only the SC2336 register table is hand-written (`Sc2336Sensor.cpp`). The sensor's native 1024x600 matches the panel, so the viewfinder is a 1:1 PPA blit.
- **The DSI panel is single-framebuffer.** Animated surfaces need an internal-SRAM offscreen canvas or they tear.
- **GT911 touch address is `0x5D` or `0x14`** depending on INT level during reset.
- **Arduino `FS` prepends the mount point; C stdio does not.** Mixing the two path namespaces looks exactly like a dead SD card.
- **USB HID (project 21) requires a `USBMode=default` FQBN.** Under the default `USBMode=hwcdc` it falls back to a Serial-logging mock with a `#warning`.

## Security & Configuration Tips

Do not commit secrets, Wi-Fi credentials, real coordinates, or a local `arduino-cli.yaml`. Per-machine config is gitignored (`**/config/WiFiSecrets.h`, `Pins.h`, `Location.h`, `LabProfile.h`, `Devices.h`, `CamSecrets.h`) — when adding a setting, edit the committed `.example.h` template.

Product safety boundaries are constraints, not defaults to relax: RF projects (05 CypherDrive, 11 CardRF, 13 SurveyOps, 17 littlehakr) are passive/receive-only with no TX, replay, jamming, injection, deauth, network joins, or credential capture. Project 19 Starbeam is the sole exception and all transmit is arm-gated behind a gitignored local `LabProfile.h` (`STARBEAM_TX_CONFIRMED`), disarmed by default. NFC/RFID (03, 07) is UID-only and read-only APDU lab inspection — demo-grade, not payment or credential work; preserve the warnings in `docs/security-notes.md` and the BadgeOps docs.
