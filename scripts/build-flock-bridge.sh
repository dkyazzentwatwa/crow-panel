#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FQBN="${FQBN:-esp32:esp32:esp32:PartitionScheme=huge_app}"
BUILD_ROOT="${BUILD_ROOT:-$ROOT/_arduino-build}/cypher-flock-bridge"
SKETCH="$ROOT/companions/cypher-flock-bridge"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli is required. Run scripts/install-cores.sh first." >&2
  exit 1
fi

ARGS=()
if [[ "${CTAGS_WORKAROUND:-0}" == "1" ]]; then
  ARGS+=(--build-property "tools.ctags.cmd.path=/usr/bin/true")
fi

mkdir -p "$BUILD_ROOT"
echo "Building Cypher Flock ESP32 BLE aggregator"
echo "FQBN: $FQBN"
arduino-cli compile \
  --fqbn "$FQBN" \
  --libraries "$ROOT/shared" \
  --build-path "$BUILD_ROOT" \
  ${ARGS[@]+"${ARGS[@]}"} \
  "$SKETCH"

echo
echo "BLE aggregator compile-verified. Upload only to a detected generic ESP32 port:"
echo "  arduino-cli upload --fqbn '$FQBN' -p <DETECTED_PORT> --input-dir '$BUILD_ROOT'"
