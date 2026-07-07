#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/project-registry.sh"

# CrowPanel Advanced 7-inch ESP32-P4 (ESP32-P4NRW32: 16 MB flash, 32 MB PSRAM).
# app3M_fat9M_16MB is the 16 MB-native scheme with an app partition large
# enough for LVGL builds (no OTA slot - revisit if OTA is ever needed).
FQBN="${FQBN:-esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600}"
BUILD_ROOT="${BUILD_ROOT:-$ROOT/_arduino-build}"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli is required. Run scripts/install-cores.sh first." >&2
  exit 1
fi

echo "Compiling CrowPanel tutorial sketches"
echo "FQBN: $FQBN"
echo "Shared libraries: $ROOT/shared"
echo
echo "Note: a green build here means compile-verified only. Nothing is"
echo "hardware-verified until it runs on the real CrowPanel. See"
echo "docs/hardware-bringup-checklist.md for the staged bring-up sequence."
echo

BUILD_ARGS=()

# EXTRA_FLAGS injects -D defines that win over the #ifndef defaults in
# config/ProjectConfig.h AND reach the shared library translation units
# (which never see ProjectConfig.h). Example:
#   EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1" ./scripts/compile-all.sh
if [[ -n "${EXTRA_FLAGS:-}" ]]; then
  echo "Extra compiler flags: $EXTRA_FLAGS"
  BUILD_ARGS+=(--build-property "compiler.cpp.extra_flags=$EXTRA_FLAGS")
fi

if [[ "${CTAGS_WORKAROUND:-0}" == "1" ]]; then
  echo "Using local ctags workaround: tools.ctags.cmd.path=/usr/bin/true"
  BUILD_ARGS+=(--build-property "tools.ctags.cmd.path=/usr/bin/true")
fi

while IFS= read -r PROJECT_REL; do
  PROJECT="$ROOT/$PROJECT_REL"
  NAME="$(basename "$PROJECT")"
  echo "==> $NAME"
  mkdir -p "$BUILD_ROOT/$NAME"
  # ${BUILD_ARGS[@]+...} keeps `set -u` happy on macOS's bash 3.2 when the
  # array is empty.
  arduino-cli compile \
    --fqbn "$FQBN" \
    --libraries "$ROOT/shared" \
    --build-path "$BUILD_ROOT/$NAME" \
    ${BUILD_ARGS[@]+"${BUILD_ARGS[@]}"} \
    "$PROJECT"
done < <(crowpanel_projects)

echo
echo "All sketches compiled."
