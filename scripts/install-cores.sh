#!/usr/bin/env bash
set -euo pipefail

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli is required. Install it before running this script." >&2
  exit 1
fi

echo "Updating Arduino core index..."
arduino-cli core update-index

echo "Installing ESP32 Arduino core..."
arduino-cli core install esp32:esp32

cat <<'NOTE'

Core install complete.

Important:
- The scaffold defaults to FQBN=esp32:esp32:esp32 only so mock sketches can compile early.
- For the real CrowPanel Advanced 7-inch ESP32-P4 target, verify the correct ESP32-P4/CrowPanel FQBN after installing the supported ESP32 Arduino core and any Elecrow-supported board package.
- If Elecrow publishes a required board package URL, add it to arduino-cli.yaml.example or your global Arduino CLI config before compiling for hardware.
NOTE
