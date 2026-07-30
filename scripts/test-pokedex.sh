#!/usr/bin/env bash
set -euo pipefail

# Host-side tests for the Pokedex index and BMP decoder.
#
# PokedexIndex and PokedexBmp are deliberately free of Arduino.h, String, and
# SD_MMC, so the exact translation units that ship in the firmware also build
# with a plain g++ here. That makes CSV paging and sprite decode a one-second
# loop instead of a flash-and-squint cycle.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/projects/15-pokedex-panel"
OUT="${BUILD_ROOT:-$ROOT/_arduino-build}/pokedex-host"

CXX="${CXX:-g++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
  echo "$CXX is required to run the Pokedex host tests." >&2
  exit 1
fi

mkdir -p "$OUT"

SOURCES=("$PROJECT/src/PokedexIndex.cpp")
if [ -f "$PROJECT/src/PokedexBmp.cpp" ]; then
  SOURCES+=("$PROJECT/src/PokedexBmp.cpp")
fi

echo "Building Pokedex host tests with $CXX"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
  -DPOKEDEX_HOST_BUILD=1 \
  "${SOURCES[@]}" \
  "$PROJECT/test/host_main.cpp" \
  -o "$OUT/pokedex-tests"

echo
"$OUT/pokedex-tests" "$@"
