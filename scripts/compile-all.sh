#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FQBN="${FQBN:-esp32:esp32:esp32}"
BUILD_ROOT="${BUILD_ROOT:-$ROOT/_arduino-build}"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli is required. Run scripts/install-cores.sh first." >&2
  exit 1
fi

echo "Compiling CrowPanel tutorial sketches"
echo "FQBN: $FQBN"
echo "Shared libraries: $ROOT/shared"
echo
echo "Warning: replace the generic FQBN with the verified ESP32-P4/CrowPanel target before hardware work."
echo

BUILD_ARGS=()
if [[ "${CTAGS_WORKAROUND:-0}" == "1" ]]; then
  echo "Using local ctags workaround: tools.ctags.cmd.path=/usr/bin/true"
  BUILD_ARGS+=(--build-property "tools.ctags.cmd.path=/usr/bin/true")
fi

PROJECTS=(
  "$ROOT/projects/01-fieldops-control-center"
  "$ROOT/projects/02-vision-guard-inspection-kiosk"
  "$ROOT/projects/03-badgeops-nfc-rfid-system"
)

for PROJECT in "${PROJECTS[@]}"; do
  NAME="$(basename "$PROJECT")"
  echo "==> $NAME"
  mkdir -p "$BUILD_ROOT/$NAME"
  if [[ "${#BUILD_ARGS[@]}" -gt 0 ]]; then
    arduino-cli compile \
      --fqbn "$FQBN" \
      --libraries "$ROOT/shared" \
      --build-path "$BUILD_ROOT/$NAME" \
      "${BUILD_ARGS[@]}" \
      "$PROJECT"
  else
    arduino-cli compile \
      --fqbn "$FQBN" \
      --libraries "$ROOT/shared" \
      --build-path "$BUILD_ROOT/$NAME" \
      "$PROJECT"
  fi
done

echo
echo "All sketches compiled."
