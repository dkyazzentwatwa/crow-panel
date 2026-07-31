# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

An **Arduino-CLI-only** monorepo of 20 standalone sketches for the Elecrow **CrowPanel Advanced 7-inch ESP32-P4 HMI AI Display** (ESP32-P4NRW32, 16 MB flash / 32 MB PSRAM, 1024x600 MIPI-DSI + GT911 touch, onboard ESP32-C6 radio over SDIO). There is no PlatformIO, no CMake, no IDE project. Everything is driven by `scripts/*.sh` wrapping `arduino-cli`.

Every project is **mock-first**: it boots into a Serial-driven demo with all hardware flags off, and each real driver path lives behind a feature flag as a compile-verified scaffold.

## Commands

```bash
./scripts/install-cores.sh      # esp32:esp32@3.3.8 (3.3.x minimum for the P4 target)
./scripts/install-libs.sh       # verified library set; exact versions in libraries.txt
./scripts/compile-all.sh        # compile every project in scripts/project-registry.sh
./scripts/check-flag-matrix.sh  # compile EVERY supported flag combo — the real regression gate
```

Single project, compile only (no per-project script exists):

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared --build-path _arduino-build/22-cypher-boy projects/22-cypher-boy
```

Compile + flash one project (always use this rather than `arduino-cli upload`, so the binary matches the script's FQBN and library set):

```bash
arduino-cli board list && ./scripts/upload-project.sh projects/22-cypher-boy /dev/cu.usbmodem101
```

Host-side tests (no board needed) and companion firmware:

```bash
./scripts/test-litego.sh --quick        # LiteGo rules fixtures + AI hygiene + bench (g++, ~1s)
./scripts/test-cypher-desk.sh           # project 18 WAV/AVI/word-wrap parsers (g++, ~2s)
./scripts/test-cypher-desk.sh clip.avi  # run the shipping demuxer over a real file
./scripts/compile-flock-system.sh       # Flock catalog checks + protocol fixtures + both companions
python3 scripts/test-flock-catalog.py   # one catalog/policy suite on its own
python3 scripts/test-flock-protocol.py  # one UART/session edge-case suite on its own
./scripts/build-espnow-companions.sh    # plain-ESP32 ESP-NOW bridge + sensor node
./scripts/build-flock-bridge.sh         # ESP32 BLE aggregator companion
./scripts/build-flock-bw16.sh           # BW16 dual-band Wi-Fi scanner companion
./scripts/convert-crowpanel-video.sh in.mp4   # MJPEG/AVI clip for project 18's video player
```

Env vars honored by the build scripts: `FQBN`, `BUILD_ROOT`, `EXTRA_FLAGS`, `EXTRA_C_FLAGS`, `CTAGS_WORKAROUND`, `CORE_VERSION`.

**`CTAGS_WORKAROUND=1` is required on this machine.** A broken local ctags emits mangled prototypes during Arduino's sketch preprocessing; symptoms are `expected constructor, destructor, or type conversion` or `'cmdX' was not declared in this scope` on valid sketch functions. The flag passes `tools.ctags.cmd.path=/usr/bin/true`, skipping prototype generation — which is why **every sketch must define functions before use**. Preserve that ordering when editing `.ino` files.

## Feature flags — the three-layer rule

This is the single most error-prone part of the repo. A flag has to reach three different compilation domains:

1. `shared/CrowPanelShared/AppConfig.h` defines every flag to `0` under `#ifndef`. This is the default.
2. `projects/NN-*/config/ProjectConfig.h` may override defaults, but **only for translation units that include it** — the `.ino` and that project's `src/`. The shared library's `.cpp` files never see it.
3. Flags gating shared code (`USE_DISPLAY` in `DisplayBringup.cpp`, `USE_WIFI` in `CrowNetworkClient.cpp`) must therefore be passed as compiler defines:

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1" ./scripts/compile-all.sh
```

The scripts inject these via `--build-property compiler.cpp.extra_flags`, never `build.extra_flags` (the esp32p4 platform owns `build.extra_flags.esp32p4` for USB defines; overriding it clobbers them).

**`compiler.cpp.extra_flags` does not reach `.c` files.** Projects with flag-gated C (project 22's vendored gwenesis core) also need `compiler.c.extra_flags` — `upload-project.sh` and `check-flag-matrix.sh` set both, and `EXTRA_C_FLAGS` appends C-only options such as gwenesis's `-Wno-incompatible-pointer-types`. Without it the C core compiles to nothing and the build is still green.

**Never wrap a feature-flagged library include in `__has_include`.** It silently disables the feature and still builds green (fixed in c7207bb). Verify real linkage by checking `<build-path>/libraries/` for the library directory, not by a green compile.

New flags must be added to `AppConfig.h` *and* get a row in `scripts/check-flag-matrix.sh`; a combination is only "supported" if it has a green row there.

## Layout and architecture

```
projects/NN-name/
  NN-name.ino          sketch: globals, serial command registrations, setup/loop
  config/ProjectConfig.h  flag + tuning overrides, included FIRST (before AppConfig defaults)
  config/*.example.h   templates for gitignored per-machine files (Pins.h, WiFiSecrets.h,
                       Location.h, LabProfile.h, Devices.h, CamSecrets.h)
  src/                 implementation classes
  README.md            demo script / serial-command walkthrough
  TECHNICAL.md         wiring, drivers, proof state
in-progress/NN-name/     same layout; still being worked on, not part of the featured set
shared/CrowPanelShared/  Arduino library passed to every build via --libraries shared
companions/, espnow/     firmware for helper boards (plain ESP32, BW16) — NOT built by compile-all
signatures/, mock-api/, docs/
```

`scripts/project-registry.sh` is the canonical project list — add new projects there or the build scripts skip them. Note the numbering gaps (no 06, 12); that is intentional.

**Two tiers, one build.** `projects/` is release-facing and documented up front in `README.md`; `in-progress/` (01, 03, 11, 16, 19) is work that is not yet featured. The registry exposes `crowpanel_release_projects`, `crowpanel_inprogress_projects`, and `crowpanel_projects` (both tiers) — build scripts iterate the last one, so in-progress projects are still compiled and flag-matrixed exactly like the rest. Numbering is shared across both tiers so a project keeps its number when it graduates; promoting one is a `git mv` plus moving its line between the two registry lists, then updating the `README.md` tables and any `docs/` paths.

`shared/CrowPanelShared/` is the spine. Include it as `#include <CrowPanelShared.h>` (umbrella header). The pieces that matter most:

- **`HardwareProfile.{h,cpp}`** — every board-revision pin group and panel timing in one place: touch, wireless socket, audio, display, hosted-C6 SDIO, camera. **Read pins from the profile; never hardcode a GPIO in a project.** Polarity lives here too (see below).
- **`AppConfig.h`** — all feature-flag defaults plus GT911 touch-mapping defaults.
- **`SerialCommandRouter` + `EventLog` + `StatusReport`** — the shared Serial UX. Every sketch answers `help`, `status`, `history` at 115200 baud with **Newline** line endings; lines over 95 chars are dropped. The command table holds `CROW_SERIAL_MAX_COMMANDS` (128, `AppConfig.h`) entries; `on()` refuses the rest, and `status`/`help`/the boot log report the drop count. **Never override that macro per project** — it sizes an array inside the class, and since shared `.cpp` files never see `ProjectConfig.h`, an override gives the sketch and the library different object layouts. It was 12 until 2026-07-31, which silently killed 18 of project 09's 30 commands (including the whole transport) and broke seven other projects the same way.
- **`DisplayBringup` / `TouchInput` (`CrowTouch`) / `DashboardWidgets` (`namespace Widgets`) / `OpsDashboard` / `UiTheme`** — the touch UI foundation. Widgets' touch-chrome names are `kChrome*`-prefixed because projects pull the namespace in wholesale.
- **`HostedWiFi`** — must run before any Wi-Fi path; see below.
- **`CameraBringup` + `Sc2336Sensor`** — the MIPI-CSI path.

Mock and real drivers feed **one pipeline per project** (`processPacket` / `processScan` / `processTap` / `onSensor`), so an injected Serial event exercises the same code as a live packet. Keep that property when adding sources.

## Hardware invariants (verified — do not "fix" these)

- **Rendering is Arduino_GFX (Adafruit-GFX-style API). No LVGL, by design.** No `lv_conf.h`, no LVGL install. Gate display code on `USE_DISPLAY` and `CONFIG_IDF_TARGET_ESP32P4`.
- **The P4 has no radio.** All Wi-Fi/BLE goes through the onboard ESP32-C6 over SDIO (esp_hosted). The CrowPanel wires the SDIO data lines *reversed* versus Espressif's reference board, with C6 reset on IO32 — `HostedWiFi` applies those pins before esp_hosted starts. Skip it and init failure reboots the board. The P4 also cannot be an ESP-NOW peer; that transport reads UART frames from a plain-ESP32 bridge.
- **Audio amp enable (IO30) is ACTIVE-LOW.** Encoded once as `AUDIO_OUT.controlActiveHigh = false` in `HardwareProfile.cpp`; write it as `digitalWrite(profile.audio.control, controlActiveHigh ? HIGH : LOW)`. Driving it HIGH mutes the speaker while I2S keeps streaming — everything "works" and is silent. Stream a block of silence before raising the enable to avoid a turn-on pop.
- **Audio pins:** I2S LRCLK 21, BCLK 22, DOUT 23, amp enable 30; PDM mic CLK 24, DIN 26. The NS4168 needs no codec/I2C init — raw I2S (16-bit stereo) is enough.
- **Camera needs no third-party library.** Core 3.3.8 already links the ESP-IDF stack (`esp_driver_cam` / `isp` / `jpeg` / `ppa`) for the P4; `esp32-camera` does not ship for this target and is not needed. Only the SC2336 register table is hand-written. The sensor's native 1024x600 matches the panel, so the viewfinder is a 1:1 PPA blit.
- **The DSI panel is single-framebuffer.** Animated surfaces need an internal-SRAM offscreen canvas, or they tear.
- **GT911 address is 0x5D or 0x14** depending on INT level during reset.
- **Arduino `FS` prepends the mount point; C stdio does not.** Mixing the two path namespaces looks exactly like a dead SD card.
- **USB HID (project 21) needs a `USBMode=default` FQBN.** Under the default `USBMode=hwcdc` it falls back to a Serial-logging mock with a `#warning`.

## The honesty contract

This repo's value proposition is that claims match evidence. Two vocabularies, both load-bearing:

- **compile-verified** — builds green on the real target. Proves nothing about the board.
- **hardware-verified** — observed working on a real CrowPanel.

Per-project proof states in `docs/full-port-proof-matrix.md`: `compile-ready` → `uploaded` → `field-proven`. **Never upgrade a row past the evidence in the session log**, and never claim hardware support without the exact FQBN, board revision, upload, and runtime behavior verified. When a stage goes green, update the proof matrix, the flag table in `README.md`, and the "NOT HARDWARE-VERIFIED" source comments together.

`docs/hardware-bringup-checklist.md` is the staged sequence (Stage 0 toolchain → 7 camera); flip **one flag at a time** with `docs/hardware-risk-register.md` open.

## Safety boundaries baked into the projects

These are product constraints, not suggestions — do not add capabilities past them:

- RF projects (11 CardRF, 13 SurveyOps, 17 littlehakr) are **passive/receive-only**: no TX, replay, jamming, injection, deauth, network joins, or credential capture.
- 05 CypherDrive is an **active** field tool — Wi-Fi active scan + join + client tools (captive-portal detection, mDNS, TCP connect sweep), on-panel BLE central scan/GATT, and USB/BLE HID output, all through the onboard C6. It still excludes the destructive/abusive end: **no** Wi-Fi deauth/jamming, **no** evil-twin/captive-portal credential capture (captive handling is detection only), and **no** unattended BadUSB autorun (HID is operator-driven — you tap, it types). Do not add those past this line.
- Project 19 Starbeam is the one exception, and all transmit is arm-gated behind a **gitignored local `LabProfile.h`** (`STARBEAM_TX_CONFIRMED`), disarmed by default.
- NFC (03, 07) is **UID-only / read-only APDU lab inspection** — demo-grade, not payment or credential work. Preserve the warnings in `docs/security-notes.md` and the BadgeOps docs.
- Never commit secrets, Wi-Fi credentials, real coordinates, or `arduino-cli.yaml`. Per-machine config is gitignored (`**/config/WiFiSecrets.h`, `Pins.h`, `Location.h`, `LabProfile.h`, `Devices.h`, `CamSecrets.h`) — edit the `.example.h` template when adding a setting.

## Code style

2-space indent; `#ifndef`/`#define`/`#endif` guards; `PascalCase` classes, `camelCase` methods and variables. Two storage rules, chosen deliberately for a tutorial repo on a 32 MB-PSRAM chip:

- **Transient formatting uses Arduino `String`** — `snprintf` boilerplate would hurt on-camera clarity for no measurable gain at seconds cadence.
- **Long-lived storage uses fixed buffers** — `EventLog` is a fixed 16-entry ring of char arrays; the command router reads into a fixed line buffer. No heap growth on a long-running panel.

Commits: short imperative subjects. PRs state summary, commands run, proof state (`compile-ready` / `uploaded` / `field-proven`), affected project folder, and Serial logs or screenshots for UI changes.

`AGENTS.md` covers the same ground as a scannable one-pager for other agents; keep the two in sync when build commands, flags, or hardware invariants change.
