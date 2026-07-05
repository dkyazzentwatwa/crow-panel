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

The script installs `esp32:esp32`, then reminds you to verify the real CrowPanel ESP32-P4 FQBN.

## Compile All Projects

```sh
./scripts/compile-all.sh
```

Default:

```sh
FQBN="${FQBN:-esp32:esp32:esp32}"
```

That generic FQBN is a scaffold default, not a final CrowPanel hardware claim.

## Upload One Project

```sh
arduino-cli board list
./scripts/upload-project.sh projects/01-fieldops-control-center /dev/cu.usbserial-0001
```

Use the detected port. On macOS, ESP32 native USB boards often appear as `/dev/cu.usbmodem*`; classic USB-UART bridges often appear as `/dev/cu.usbserial*`.

## Local ctags Workaround

Some macOS Arduino CLI installs fail before real compilation because the bundled `ctags` binary is the wrong architecture. If you see that symptom, retry:

```sh
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
```
