#!/usr/bin/env bash
set -euo pipefail

# Host-side tests for Cypher Desk OS (project 18).
#
# Covers the three parsers the project gained, which are simultaneously the
# highest-risk code in it and the hardest to watch fail on glass: a mis-parsed
# chunk header looks exactly like a corrupt file, and an off-by-one in the wrap
# table silently puts the caret in the wrong place.
#
#   DeskWavReader   RIFF/WAVE: rates, channel counts, bit depths, ffmpeg's
#                   LIST/INFO chunk, RIFF's even-length chunk padding
#   DeskAviReader   MJPEG-in-AVI: header walk, stream typing, chunk padding,
#                   LIST 'rec ' groups, and video-only clips (project 02's)
#   DeskTextWrap    editor word wrap and the index <-> line/column round trip
#                   that tap-to-place-cursor depends on
#
# All three are deliberately free of SD_MMC and display headers - the byte
# source abstractions exist so the EXACT translation units that ship in the
# firmware are the ones under test here, not a copy.
#
# Usage:
#   ./scripts/test-cypher-desk.sh

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/projects/18-cypher-desk-panel"
OUT="${BUILD_ROOT:-$ROOT/_arduino-build}/cypher-desk-host"

CXX="${CXX:-g++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
  echo "$CXX is required to run the Cypher Desk host tests." >&2
  exit 1
fi

mkdir -p "$OUT"

echo "Building Cypher Desk host tests with $CXX"
# USE_CYPHER_DESK_SD=0 keeps the FS.h-backed source adapters out; the tests
# drive the readers from an in-memory source instead.
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
  -DUSE_CYPHER_DESK_SD=0 \
  -DUSE_DISPLAY=0 \
  -I"$PROJECT/test/shim" \
  -I"$ROOT/shared/CrowPanelShared" \
  "$PROJECT/src/DeskWavReader.cpp" \
  "$PROJECT/src/DeskAviReader.cpp" \
  "$PROJECT/src/DeskTextWrap.cpp" \
  "$PROJECT/test/host_main.cpp" \
  -o "$OUT/cypher-desk-tests"

echo
"$OUT/cypher-desk-tests" "$@"
