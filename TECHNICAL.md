# Technical Reference

How this repo is built, why it is shaped the way it is, and the hardware facts you
should not have to rediscover. For what each project *is*, see
[README.md](README.md); for per-project wiring and flags, see that project's own
`TECHNICAL.md`.

---

## Toolchain

Arduino CLI only. No PlatformIO, no CMake, no IDE project files.

```bash
./scripts/install-cores.sh   # esp32:esp32@3.3.8 — 3.3.x is the minimum for ESP32-P4
./scripts/install-libs.sh    # the pinned library set; exact versions in libraries.txt
```

`esp32:esp32:esp32p4` ships in Espressif's official index, so no third-party board
URL is needed.

### The FQBN

```
esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600
```

`app3M_fat9M_16MB` is the 16 MB-native scheme with an app partition big enough for
the largest sketches here, and no OTA slot. Override the whole thing with the
`FQBN` environment variable.

**Project 21 is the exception:** USB HID requires `USBMode=default` (USB-OTG /
TinyUSB, `ARDUINO_USB_MODE==0`). Under the default `hwcdc` the P4's native USB is
the CDC/JTAG bridge and no HID is possible, so the build falls back to a
Serial-logging mock and emits a `#warning`.

### Building and flashing

```bash
./scripts/compile-all.sh          # every project in the registry
./scripts/check-flag-matrix.sh    # every SUPPORTED flag combination — the real gate
./scripts/upload-project.sh projects/NN-name /dev/cu.usbmodemXXXX
```

Always flash with `upload-project.sh` rather than `arduino-cli upload`: it
compiles and flashes in one step, so the binary always matches the script's FQBN
and library set (`arduino-cli upload` alone has no `--libraries`).

Single project, compile only:

```bash
arduino-cli compile \
  --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries shared --build-path _arduino-build/NN-name projects/NN-name
```

Environment variables honoured by the scripts: `FQBN`, `BUILD_ROOT`,
`EXTRA_FLAGS`, `EXTRA_C_FLAGS`, `CTAGS_WORKAROUND`, `CORE_VERSION`.

### Host-side tests (no board required)

```bash
./scripts/test-cypher-keys.sh           # project 21 keyboard state machine + sound-pack parsing
./scripts/test-litego.sh --quick        # project 10 Go rules fixtures + AI hygiene
./scripts/test-cypher-tune.sh           # project 09 pitch table + backing-loop tempo lock
./scripts/test-cypher-desk.sh           # project 18 WAV/AVI/word-wrap parsers
./scripts/test-pokedex.sh               # project 15 CSV index paging + BMP sprite decode
./scripts/test-acid-glass.sh            # project 24 scene wrap, touch mapping, PCM validation
./scripts/test-inkwell.sh               # project 25 TXT/Markdown/EPUB parsers, paginator, gestures
./scripts/compile-flock-system.sh       # project 16 catalog + protocol suites, both companions
python3 scripts/test-flock-catalog.py
python3 scripts/test-flock-protocol.py
```

These compile the *real* project sources against a small Arduino shim, so they
test shipping code rather than a copy. They have earned their place: the project
21 harness caught a library that compiled green but never linked, and flagged a
key-row mapping change that would only ever have been audible.

### Companion firmware (not built by `compile-all`)

```bash
./scripts/build-espnow-companions.sh    # plain-ESP32 ESP-NOW bridge + sensor node
./scripts/build-flock-bridge.sh         # ESP32 BLE aggregator
./scripts/build-flock-bw16.sh           # BW16 dual-band Wi-Fi scanner
./scripts/build-builtin-kit.py          # regenerates project 09's synthesized kit
./scripts/convert-key-sounds.sh         # kbsim-style sound tree -> project 21's WAV layout
```

---

## Feature flags: the three-layer rule

The single most error-prone part of this repo. A flag must reach three different
compilation domains, and forgetting the third produces a green build that does
nothing.

1. **`shared/CrowPanelShared/AppConfig.h`** defines every flag to `0` under
   `#ifndef`. That is the default, and why mock mode is what you get for free.
2. **`projects/NN-*/config/ProjectConfig.h`** may override those defaults — but
   **only for translation units that include it**: the `.ino` and that project's
   `src/`. The shared library's `.cpp` files never see it.
3. **Flags gating shared code** (`USE_DISPLAY` in `DisplayBringup.cpp`,
   `USE_WIFI` in `CrowNetworkClient.cpp`) must therefore arrive as compiler
   defines:

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1" ./scripts/compile-all.sh
```

The scripts inject these through `--build-property compiler.cpp.extra_flags`,
never `build.extra_flags` — the esp32p4 platform owns `build.extra_flags.esp32p4`
for its USB defines, and overriding it clobbers them.

**`compiler.cpp.extra_flags` does not reach `.c` files.** Projects with
flag-gated C (project 22's vendored gwenesis core) also need
`compiler.c.extra_flags`; `upload-project.sh` and `check-flag-matrix.sh` set both,
and `EXTRA_C_FLAGS` appends C-only options. Without it the C core compiles to
nothing and the build is *still green*.

**Never wrap a feature-flagged library include in `__has_include`.** It silently
disables the feature and builds green anyway. Arduino CLI's dependency scan
preprocesses before the library's include path exists, so the guard is false at
exactly the wrong moment. Verify real linkage by looking for the library
directory under `<build-path>/libraries/` — not by a successful compile.

A new flag needs a default in `AppConfig.h` **and** a row in
`scripts/check-flag-matrix.sh`. A combination is only "supported" if it has a
green row there.

---

## Repository layout

```
projects/NN-name/
  NN-name.ino             sketch: globals, serial command registration, setup/loop
  config/ProjectConfig.h  flag + tuning overrides (included before AppConfig defaults)
  config/*.example.h      templates for gitignored per-machine files
  src/                    implementation classes
  README.md               what it is and how to drive it
  TECHNICAL.md            wiring, flags, proof state
in-progress/NN-name/      same layout; not yet featured, but built and flag-matrixed
shared/CrowPanelShared/   Arduino library passed to every build via --libraries shared
companions/, espnow/      helper-board firmware (not built by compile-all)
scripts/                  the entire build system
docs/                     proof matrix, risk register, bring-up checklist, notes
signatures/, mock-api/    detector catalog; optional local API for Wi-Fi projects
```

`scripts/project-registry.sh` is the canonical project list — a project not listed
there is skipped by every build script. It exposes both tiers separately
(`crowpanel_release_projects`, `crowpanel_inprogress_projects`) and together
(`crowpanel_projects`, what the build scripts iterate). The numbering gaps
(no 06, 12, 23) are intentional; those projects were retired. Numbering is shared
across tiers, so a project keeps its number when it graduates out of
`in-progress/`.

---

## Shared library architecture

`shared/CrowPanelShared/` is the spine. Include the umbrella header:

```cpp
#include <CrowPanelShared.h>
```

The pieces that matter most:

- **`HardwareProfile.{h,cpp}`** — every board-revision pin group and panel timing
  in one place: touch, wireless socket, audio, display, hosted-C6 SDIO, camera.
  Pin *polarity* lives here too. **Read pins from the profile; never hardcode a
  GPIO in a project.** One fix then covers every project.
- **`AppConfig.h`** — all feature-flag defaults, plus GT911 touch-mapping defaults.
- **`DisplayBringup`** (`namespace CrowDisplay`) — MIPI-DSI + GT911 bring-up,
  including the EK79007 vendor init sequence Arduino_GFX does not ship. Exposes
  the raw `Arduino_GFX` canvas, single- and multi-touch reads, backlight control,
  and an opt-in manual-flush mode (below).
- **`DashboardWidgets`** (`namespace Widgets`) — the drawing toolkit: panels,
  text, gauges, sparklines, pills. Touch-chrome names are `kChrome*`-prefixed
  because projects pull the namespace in wholesale.
- **`SerialCommandRouter` + `EventLog` + `StatusReport`** — the shared Serial UX.
  Every sketch answers `help`, `status`, `history` at **115200 baud, Newline**
  line endings. Lines over 95 characters are dropped.
- **`HostedWiFi`** — must run before any Wi-Fi path (below).
- **`CameraBringup` + `Sc2336Sensor`** — the MIPI-CSI path.

### One pipeline per project

Mock and real drivers feed the **same** function (`processPacket` / `processScan`
/ `processTap` / `onSensor`), so a Serial-injected event exercises exactly the
code a live packet would. This is what makes mock mode a real test rather than a
parallel implementation — preserve the property when adding a source.

### Manual flush (performance)

Arduino_GFX's DSI driver, with `auto_flush` on, calls `esp_cache_msync()` **per
pixel**. Custom-font glyphs draw pixel-by-pixel, so a text-heavy redraw pays tens
of thousands of cache syncs.

`CrowDisplay::begin(profile, title, /*manualFlush=*/true)` builds the panel with
auto-flush off; the app then draws a whole frame and calls `CrowDisplay::flush()`
(or the region overload) once. Default is `false`, so existing projects are
unaffected. Project 21 uses this and it is the difference between a sluggish and a
snappy touch UI.

---

## Hardware invariants

Verified on real hardware. Do not "fix" these.

- **Rendering is Arduino_GFX (Adafruit-GFX-style API). No LVGL, by design.** No
  `lv_conf.h`, no LVGL install. Gate display code on `USE_DISPLAY` *and*
  `CONFIG_IDF_TARGET_ESP32P4`.
- **The P4 has no radio.** All Wi-Fi/BLE goes through the onboard ESP32-C6 over
  SDIO (`esp_hosted`). The CrowPanel wires the SDIO data lines **reversed**
  versus Espressif's reference board, with C6 reset on IO32. `HostedWiFi` applies
  those pins before esp_hosted starts — skip it and init failure reboots the
  board. The same ordering applies to BLE: call the pin setup *before*
  `BLEDevice::init()`.
- **The P4 cannot be an ESP-NOW peer.** That transport reads UART frames from a
  plain-ESP32 bridge (`espnow/`).
- **Audio amp enable (IO30) is ACTIVE-LOW.** Encoded once as
  `AUDIO_OUT.controlActiveHigh = false`; always write
  `digitalWrite(profile.audio.control, controlActiveHigh ? HIGH : LOW)`. Driving
  it HIGH mutes the speaker while I2S keeps streaming — everything looks like it
  works and is silent. **Stream a block of silence before raising the enable** to
  avoid a turn-on pop.
- **Audio pins:** I2S LRCLK 21, BCLK 22, DOUT 23, amp enable 30; PDM mic CLK 24,
  DIN 26. The NS4168 needs no codec or I2C init — raw 16-bit stereo I2S is enough.
- **The camera needs no third-party library.** Core 3.3.8 already ships and links
  the ESP-IDF stack (`esp_driver_cam` / `isp` / `jpeg` / `ppa`) for the P4;
  `esp32-camera` does not exist for this target and is not needed. Only the SC2336
  register table is hand-written. The sensor's native 1024×600 matches the panel,
  so the viewfinder is a 1:1 PPA blit.
- **The DSI panel is single-framebuffer.** Animated surfaces need an
  internal-SRAM offscreen canvas, or they tear.
- **GT911 touch address is 0x5D or 0x14**, depending on INT level during reset.
  The driver probes both.
- **The GT911 point register is cleared on read.** Polling faster than the panel
  refreshes returns empty frames — which looks like a broken trackpad. Sample at a
  fixed cadence (~16 ms) independent of loop speed, and debounce releases so a
  dropped frame mid-press is not read as a lift.
- **Arduino `FS` prepends the mount point; C stdio does not.** Mixing the two path
  namespaces looks exactly like a dead SD card.
- **macOS writes AppleDouble `._*` files onto FAT32.** They appear as bogus
  entries to directory scans — copy with `ditto --norsrc --noextattr` or clean up
  afterwards.

---

## Proof vocabulary

Per-project states live in `docs/full-port-proof-matrix.md`:

| State | Means |
|---|---|
| `compile-ready` | The sketch and its flag combinations build for the ESP32-P4 target. |
| `uploaded` | The matching binary was flashed to a real CrowPanel and verified. |
| `field-proven` | The real peripheral or behaviour was observed on hardware. |

**Never upgrade a row past the evidence.** Never claim hardware support without
the exact FQBN, board revision, successful upload, and observed runtime
behaviour. When a stage goes green, update the proof matrix, the project README's
status section, and any `NOT HARDWARE-VERIFIED` source comments together.

`docs/hardware-bringup-checklist.md` is the staged sequence (Stage 0 toolchain
through Stage 7 camera). Flip **one flag at a time**, with
`docs/hardware-risk-register.md` open beside it.

---

## Adding a project

1. `projects/NN-name/` with the layout above; number it after the highest
   existing project.
2. Add the path to `scripts/project-registry.sh` — otherwise every script skips it.
3. Add flag defaults to `shared/CrowPanelShared/AppConfig.h` and a row per
   supported combination in `scripts/check-flag-matrix.sh`.
4. Read pins from `HardwareProfile`; put per-machine values in a
   `config/*.example.h` template and gitignore the real file.
5. Keep mock mode the default and route mock + real sources through one pipeline.
6. Write `README.md` (user-facing) and `TECHNICAL.md` (wiring, flags, proof
   state), and add a proof-matrix row starting at `compile-ready`.
7. Run `./scripts/check-flag-matrix.sh` and confirm your rows are green.

---

## Troubleshooting

**`expected constructor, destructor, or type conversion`, or `'cmdX' was not
declared in this scope`** on obviously valid sketch functions — a broken local
`ctags` is emitting mangled prototypes during Arduino's sketch preprocessing.
Retry with `CTAGS_WORKAROUND=1`, which points `tools.ctags.cmd.path` at
`/usr/bin/true` and skips prototype generation. Because of that, **every sketch
defines its functions before use**; preserve that ordering when editing `.ino`
files. (Whether you need this depends on your machine's ctags.)

**The board will not enter the bootloader** — hold **BOOT**, tap **RESET**,
release **BOOT**. Then check `arduino-cli board list`. As a fallback, try
`USBMode=default` in the FQBN.

**The serial port disappears once the sketch runs** — expected on some builds.
With `USBMode=hwcdc` the native port drops when the app starts; project 21's HID
build enumerates as a HID device instead. Add `CDCOnBoot=cdc` to the FQBN if you
need a console while the app runs. Note that **panic backtraces go to UART0**, not
USB — a crash dump needs a UART adapter on the UART0 TX pad.

**A feature does nothing despite a green build** — suspect the three-layer flag
rule, or an `__has_include` guard. Check `<build-path>/libraries/` for the library
you expect.

**Wi-Fi or BLE hangs at init** — the hosted-C6 SDIO pins were not applied before
`esp_hosted` started, or the C6 slave firmware is older than the core's client
(2.12.3 for core 3.3.8). See `docs/c6-wifi-handoff.md`.

More in [docs/troubleshooting.md](docs/troubleshooting.md).

---

## Code style

Two-space indent. `#ifndef` / `#define` / `#endif` guards. `PascalCase` types,
`camelCase` methods and variables.

Two storage rules, chosen deliberately for a tutorial repo on a 32 MB-PSRAM chip:

- **Transient formatting uses Arduino `String`.** Events happen at seconds
  cadence; `snprintf` boilerplate would cost readability for no measurable gain.
- **Long-lived storage uses fixed buffers.** `EventLog` is a fixed 16-entry ring
  of char arrays; the command router reads into a fixed line buffer. No heap
  growth on a panel that runs for days.

Comments should explain *why* — particularly where a value was hard-won. A
constant that took a hardware session to find deserves the sentence that saves
the next person that session.
