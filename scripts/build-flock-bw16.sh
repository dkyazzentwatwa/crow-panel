#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FQBN="${FQBN:-realtek:AmebaD:Ai-Thinker_BW16}"
BUILD_ROOT="${BUILD_ROOT:-$ROOT/_arduino-build}/cypher-flock-bw16"
SKETCH="$ROOT/companions/cypher-flock-bw16"
EXTRA_FLAGS="${EXTRA_FLAGS:-}"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli is required." >&2
  exit 1
fi

mkdir -p "$BUILD_ROOT"
echo "Building Cypher Flock BW16 Wi-Fi node"
echo "FQBN: $FQBN"
ARGS=()
if [[ -n "$EXTRA_FLAGS" ]]; then
  echo "Extra compiler flags: $EXTRA_FLAGS"
  BASE_EXTRA_FLAGS="$(arduino-cli compile --fqbn "$FQBN" --show-properties "$SKETCH" | sed -n 's/^build.extra_flags=//p')"
  ARGS+=(--build-property "build.extra_flags=$BASE_EXTRA_FLAGS $EXTRA_FLAGS")
fi
arduino-cli compile \
  --fqbn "$FQBN" \
  --libraries "$ROOT/shared" \
  --build-path "$BUILD_ROOT" \
  ${ARGS[@]+"${ARGS[@]}"} \
  "$SKETCH"

echo
echo "BW16 compile-verified only. Upload/runtime and raw 5 GHz capture remain unproven."
