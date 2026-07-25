#!/usr/bin/env bash
set -euo pipefail

# Host-side tests for the Cypher Keys touch model and sound packs (project 21).
#
# src/HidKeyboard.cpp, src/KeysTouch.cpp and src/KeySoundPacks.cpp are the exact
# translation units that ship in the firmware; they are compiled here against
# test/shim/Arduino.h (a test-driven millis() and a tiny String) and
# test/shim/CrowPanelShared.h (a scripted GT911 sample), which shadows the real
# shared header on the include path. That makes the chording / sticky /
# hold-repeat rules, the release debounce, and the WAV parsing plus per-key clip
# fallbacks a one-second loop instead of a flash-and-squint cycle.
#
# The display code in HidKeyboard.cpp is gated on CONFIG_IDF_TARGET_ESP32P4,
# which is never defined here, so no Arduino_GFX is needed. USE_DISPLAY=1 is set
# so KeysTouch compiles its real GT911 path rather than the headless stub.
# USE_CYPHER_KEYS_SD stays 0: the SD_MMC half of KeySoundPacks.cpp is stubbed,
# while its pure half (RIFF header parsing, the filename map, the resolution
# order) is the same code the firmware runs.
#
# The sound-pack tests will additionally parse REAL converted WAVs when a tree
# produced by scripts/convert-key-sounds.sh is present. Point
# CYPHER_KEYS_SOUND_DIR at it (default ~/Downloads/cypher-keys-sounds); those
# checks SKIP rather than fail when it is missing, and no audio is ever copied
# into the repo.
#
# Usage: ./scripts/test-cypher-keys.sh

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/projects/21-cypher-keys-hid-deck"
OUT="${BUILD_ROOT:-$ROOT/_arduino-build}/cypher-keys-host"
SOUND_DIR="${CYPHER_KEYS_SOUND_DIR:-$HOME/Downloads/cypher-keys-sounds}"

CXX="${CXX:-clang++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
  echo "$CXX is required to run the Cypher Keys host tests." >&2
  exit 1
fi

mkdir -p "$OUT"

echo "Building Cypher Keys host tests with $CXX"
"$CXX" -std=c++17 -O1 -Wall -Wextra -Werror \
  -DUSE_DISPLAY=1 \
  -DCYPHER_KEYS_TEST_SOUND_DIR="\"$SOUND_DIR\"" \
  -I "$PROJECT/test/shim" \
  -I "$ROOT/shared/CrowPanelShared" \
  "$PROJECT/src/HidKeyboard.cpp" \
  "$PROJECT/src/KeysTouch.cpp" \
  "$PROJECT/src/KeySoundPacks.cpp" \
  "$PROJECT/test/host_main.cpp" \
  -o "$OUT/cypher-keys-tests"

echo
"$OUT/cypher-keys-tests"
