#!/usr/bin/env bash
set -euo pipefail

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli is required. Install it before running this script." >&2
  exit 1
fi

echo "Installing safe/common optional library..."
arduino-cli lib install ArduinoJson

cat <<'NOTE'

Hardware-specific libraries are intentionally not installed by default.

Enable and install them only after verifying the real board revision, wiring,
and Elecrow Arduino example for your module:

  # arduino-cli lib install "lvgl"
  # arduino-cli lib install "MFRC522"
  # arduino-cli lib install "Adafruit PN532"
  # TODO: choose a LoRa/SX1262 library after confirming the module example.

NOTE
