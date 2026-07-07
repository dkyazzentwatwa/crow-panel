# Repository Guidelines

## Project Structure & Module Organization

This is an Arduino CLI-only monorepo for CrowPanel tutorial projects.

- `projects/01-fieldops-control-center/`: mock LoRa field sensor dashboard.
- `projects/02-vision-guard-inspection-kiosk/`: mock camera, QR, and inspection kiosk.
- `projects/03-badgeops-nfc-rfid-system/`: mock NFC/RFID badge terminal.
- `shared/CrowPanelShared/`: Arduino library shared by all sketches.
- `scripts/`: Arduino CLI install, compile, and upload helpers.
- `docs/`: hardware notes, setup, revision profiles, security notes, and troubleshooting.
- `mock-api/`: optional Express API for later Wi-Fi/API demos.

Each project keeps its `.ino` file at the project root, project flags in `config/ProjectConfig.h`, and implementation classes in `src/`.

## Build, Test, and Development Commands

- `./scripts/install-cores.sh`: installs the ESP32 Arduino core.
- `./scripts/install-libs.sh`: installs safe/common optional libraries only.
- `./scripts/compile-all.sh`: compiles all three sketches with `FQBN="${FQBN:-esp32:esp32:esp32}"`.
- `CTAGS_WORKAROUND=1 ./scripts/compile-all.sh`: retries with the local macOS ctags workaround.
- `./scripts/upload-project.sh projects/03-badgeops-nfc-rfid-system /dev/cu.usbserial-0001`: uploads one sketch to a detected serial port.
- `cd mock-api && npm install && npm start`: runs the optional local API on port `8787`.

## Coding Style & Naming Conventions

Use 2-space indentation for Arduino/C++ and JavaScript. Keep headers guarded with `#ifndef ... #define ... #endif`. Class names use `PascalCase`; methods and variables use `camelCase`. Keep hardware-specific imports behind feature flags such as `USE_LORA_DRIVER`, `USE_CAMERA_DRIVER`, `USE_PN532_DRIVER`, and `USE_MFRC522_DRIVER`.

## Testing Guidelines

There is no formal test framework yet. Treat `./scripts/compile-all.sh` as the required regression check. For API changes, smoke-test `GET /health`, `POST /events`, `GET /events`, `POST /summary`, `GET /badges`, `POST /badges`, and `POST /inspection`.

## Commit & Pull Request Guidelines

Git history currently has only `Initial commit`, so use short imperative commit messages, for example `Add FieldOps demo docs`. PRs should include: summary, commands run, proof state (`compile-ready`, `uploaded`, or `field-proven`), affected project folder, and screenshots or Serial logs for UI/demo changes.

## Security & Configuration Tips

Do not commit secrets, Wi-Fi credentials, or local `arduino-cli.yaml`. UID-only RFID/NFC is demo-grade only; preserve the warning in `docs/security-notes.md` and BadgeOps docs. Do not claim CrowPanel hardware support until the exact FQBN, board revision, upload, and runtime behavior are verified.
