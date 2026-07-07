#!/usr/bin/env bash
set -euo pipefail

# Compiles the ESP-NOW companion sketches that run on PLAIN ESP32s (native
# radio) - the bridge and the sensor node. These are NOT built by
# compile-all.sh, which targets the CrowPanel's ESP32-P4. They share the
# vendored cypher-chat mesh code in espnow/shared-mesh (passed with --library).

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Plain ESP32 with the huge_app partition (mbedtls + WiFi + the mesh core need
# the room). Override for an S3/C3/C6 bridge, e.g. FQBN=esp32:esp32:esp32c3:...
FQBN="${FQBN:-esp32:esp32:esp32:PartitionScheme=huge_app}"
BUILD_ROOT="${BUILD_ROOT:-$ROOT/_arduino-build}/espnow"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli is required. Run scripts/install-cores.sh first." >&2
  exit 1
fi

BUILD_ARGS=()
if [[ "${CTAGS_WORKAROUND:-0}" == "1" ]]; then
  BUILD_ARGS+=(--build-property "tools.ctags.cmd.path=/usr/bin/true")
fi

echo "Building ESP-NOW companions"
echo "FQBN: $FQBN"
echo

for SKETCH in bridge sensor-node; do
  echo "==> espnow/$SKETCH"
  mkdir -p "$BUILD_ROOT/$SKETCH"
  arduino-cli compile \
    --fqbn "$FQBN" \
    --library "$ROOT/espnow/shared-mesh" \
    --build-path "$BUILD_ROOT/$SKETCH" \
    ${BUILD_ARGS[@]+"${BUILD_ARGS[@]}"} \
    "$ROOT/espnow/$SKETCH"
done

echo
echo "Companions compiled. Flash with:"
echo "  arduino-cli upload --fqbn \"$FQBN\" -p <PORT> --input-dir \"$BUILD_ROOT/<bridge|sensor-node>\""
echo "See espnow/README.md for wiring and passphrase/channel matching."
