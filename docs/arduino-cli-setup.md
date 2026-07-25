# Arduino CLI Setup

This repo is Arduino CLI only.

Do not add:

- PlatformIO
- `platformio.ini`
- ESP-IDF CMake project structure
- Hardcoded final board assumptions

## Install The ESP32 Core

```sh
./scripts/install-cores.sh
```

The script pins `esp32:esp32@3.3.8` — the version this repo is
compile-verified against (the ESP32-P4 target needs 3.3.x at minimum).
Override with `CORE_VERSION=x.y.z` and re-run `scripts/check-flag-matrix.sh`
after any core change. No third-party board package URL is needed:
`esp32p4` ships in the official Espressif index.

## Install Libraries

```sh
./scripts/install-libs.sh
```

Exact verified versions are in `libraries.txt`. Display rendering uses the
Adafruit-GFX-style API (Arduino_GFX) — no LVGL, no `lv_conf.h` needed.

## Compile All Projects

```sh
./scripts/compile-all.sh
```

Default FQBN (the real board):

```text
esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600
```

Option rationale: 16 MB flash and PSRAM match the P4NRW32; the
`app3M_fat9M_16MB` scheme is the 16 MB-native layout with an app partition
big enough for display builds (no OTA slot). `USBMode=hwcdc` and the default
`ChipVariant` are compile-time assumptions — Stage 0 of
`docs/hardware-bringup-checklist.md` covers the fallbacks.

Enable feature flags per build:

```sh
EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/compile-all.sh
```

Verify every supported flag combination:

```sh
./scripts/check-flag-matrix.sh
```

## Upload One Project

```sh
arduino-cli board list
./scripts/upload-project.sh in-progress/01-fieldops-control-center /dev/cu.usbmodem101
```

The script compiles and flashes in one step (`arduino-cli compile --upload`)
so the binary always matches the FQBN and shared library set. Use the
detected port. On macOS, ESP32 native USB boards often appear as
`/dev/cu.usbmodem*`; classic USB-UART bridges as `/dev/cu.usbserial*`.

## Local ctags Workaround

Some macOS Arduino CLI installs have a broken `ctags`, which surfaces as
mangled-prototype compile errors ("expected constructor, destructor, or
type conversion"). Retry any script with:

```sh
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
```

Details in `docs/troubleshooting.md`.
