#!/usr/bin/env bash
set -euo pipefail

# esp32 core 3.3.x is the minimum for the ESP32-P4 target; 3.3.8 is the
# version this repo is compile-verified against. Override if needed:
#   CORE_VERSION=3.3.10 ./scripts/install-cores.sh
CORE_VERSION="${CORE_VERSION:-3.3.8}"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli is required. Install it before running this script." >&2
  exit 1
fi

echo "Updating Arduino core index..."
arduino-cli core update-index

echo "Installing ESP32 Arduino core ${CORE_VERSION}..."
arduino-cli core install "esp32:esp32@${CORE_VERSION}"

cat <<'NOTE'

Core install complete.

Important:
- The ESP32-P4 target (esp32:esp32:esp32p4) ships in the official Espressif
  core index - no third-party board package URL is required.
- The repo default FQBN targets the CrowPanel Advanced 7-inch ESP32-P4:
  esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB
- Every flag combination in scripts/check-flag-matrix.sh is compile-verified
  against core 3.3.8. Newer 3.3.x releases are expected to work; re-run the
  flag matrix after changing core versions.
NOTE
