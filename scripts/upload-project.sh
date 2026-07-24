#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/project-registry.sh"

# Same default FQBN as compile-all.sh; see the comment there.
FQBN="${FQBN:-esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600}"
BUILD_ROOT="${BUILD_ROOT:-$ROOT/_arduino-build}"

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <project-folder> <serial-port>" >&2
  echo "Example: $0 projects/03-badgeops-nfc-rfid-system /dev/cu.usbmodem101" >&2
  exit 1
fi

PROJECT_DIR="$1"
PORT="$2"

if [[ "$PROJECT_DIR" != /* ]]; then
  PROJECT_DIR="$ROOT/$PROJECT_DIR"
fi

if [[ ! -d "$PROJECT_DIR" ]]; then
  echo "Project folder not found: $PROJECT_DIR" >&2
  exit 1
fi

PROJECT_REL="${PROJECT_DIR#$ROOT/}"
if ! crowpanel_project_exists "$PROJECT_REL"; then
  echo "Warning: $PROJECT_REL is not listed in scripts/project-registry.sh" >&2
fi

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli is required. Run scripts/install-cores.sh first." >&2
  exit 1
fi

NAME="$(basename "$PROJECT_DIR")"
mkdir -p "$BUILD_ROOT/$NAME"

BUILD_ARGS=()
if [[ -n "${EXTRA_FLAGS:-}" ]]; then
  echo "Extra compiler flags: $EXTRA_FLAGS"
  BUILD_ARGS+=(--build-property "compiler.cpp.extra_flags=$EXTRA_FLAGS")
  # The same defines must also reach .c files: compiler.cpp.extra_flags does
  # NOT, so a project with a flag-gated C core (project 22's vendored gwenesis)
  # would flash a binary where that core compiled to nothing, with no error.
  # EXTRA_C_FLAGS appends C-only options such as warning suppressions.
  BUILD_ARGS+=(--build-property "compiler.c.extra_flags=$EXTRA_FLAGS ${EXTRA_C_FLAGS:-}")
fi
if [[ "${CTAGS_WORKAROUND:-0}" == "1" ]]; then
  BUILD_ARGS+=(--build-property "tools.ctags.cmd.path=/usr/bin/true")
fi

echo "Compile + upload $PROJECT_DIR"
echo "Port: $PORT"
echo "FQBN: $FQBN"
echo
echo "If the board does not enter the bootloader: hold BOOT while pressing"
echo "RESET, or retry with USBMode=default in the FQBN (USB-OTG instead of"
echo "HW CDC). See docs/hardware-bringup-checklist.md, Stage 0."
echo

# compile --upload keeps the flashed binary consistent with this script's
# FQBN and library set (arduino-cli upload alone has no --libraries flag).
arduino-cli compile \
  --fqbn "$FQBN" \
  --libraries "$ROOT/shared" \
  --build-path "$BUILD_ROOT/$NAME" \
  ${BUILD_ARGS[@]+"${BUILD_ARGS[@]}"} \
  --upload \
  --port "$PORT" \
  "$PROJECT_DIR"
