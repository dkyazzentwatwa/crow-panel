#!/usr/bin/env bash
set -euo pipefail

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli is required. Install it before running this script." >&2
  exit 1
fi

echo "Updating library index..."
arduino-cli lib update-index

# Everything below is compile-verified against esp32:esp32@3.3.8 on the
# esp32p4 target. See libraries.txt for the exact verified versions and
# which feature flag each library serves. `lib install` is idempotent:
# already-installed libraries are skipped or upgraded.
LIBRARIES=(
  "ArduinoJson"          # packet/JSON parsing (USE_LORA_DRIVER payloads, later Wi-Fi work)
  "RadioLib"             # SX1262 LoRa driver - Elecrow's own choice in their Lesson13 example
  "GFX Library for Arduino"  # Arduino_GFX: Adafruit-GFX-style API + MIPI-DSI classes for USE_DISPLAY
  "U8g2"                 # Project 18 compact fonts through Arduino_GFX's native U8g2 support
  "SensorLib"            # GT911 capacitive touch driver
  "Adafruit PN532"       # USE_PN532_DRIVER (pulls in Adafruit BusIO)
  "MFRC522"              # USE_MFRC522_DRIVER
  "RF24"                 # optional nRF24-style module in the wireless socket
  "NimBLE-Arduino"       # generic ESP32 Cypher Flock BLE detector companion
  "TinyGPSPlus"          # USE_GPS_DRIVER - NMEA parsing for project 13 SurveyOps
  # Pinned deliberately: 2.5.7 is the compile-verified version, but the registry
  # now offers 3.0.2 - a major bump that nothing here has built against. An
  # unpinned install would hand a fresh clone untested code while the flag matrix
  # still reported green. Re-verify project 19's [radios] and [full] rows before
  # moving this pin.
  "SmartRC-CC1101-Driver-Lib@2.5.7"  # USE_STARBEAM_RADIOS - CC1101 sub-GHz on project 19
)

# Collect failures rather than dying on the first one: `set -e` would abort
# mid-list and leave the remaining libraries uninstalled with no summary, and
# the flag matrix SKIPs (not FAILs) on a missing library, so a silent partial
# install is exactly how rows go quietly unbuilt.
FAILED_LIBS=()
for LIB in "${LIBRARIES[@]}"; do
  echo "==> $LIB"
  if ! arduino-cli lib install "$LIB"; then
    FAILED_LIBS+=("$LIB")
  fi
done

if (( ${#FAILED_LIBS[@]} > 0 )); then
  echo >&2
  echo "FAILED to install ${#FAILED_LIBS[@]} librar(y/ies):" >&2
  for LIB in "${FAILED_LIBS[@]}"; do
    echo "  - $LIB" >&2
  done
  cat >&2 <<'FAILNOTE'

These rows will SKIP (not FAIL) in scripts/check-flag-matrix.sh, so the matrix
can still exit 0 while leaving them unbuilt. Resolve before trusting a green run.

A "please manually remove all duplicates" error means two directories under
~/Documents/Arduino/libraries/ declare the same name= in library.properties.
arduino-cli refuses to touch the library until one is removed.
FAILNOTE
  exit 1
fi

cat <<'NOTE'

Library install complete.

Rendering note: this repo deliberately uses the Adafruit-GFX-style API
(via Arduino_GFX's DSI display class) instead of LVGL. No lv_conf.h or
LVGL install is needed.

All libraries here are compile-verified only. Cross-check each driver
against the official Elecrow examples before first power-on:
https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen
NOTE
