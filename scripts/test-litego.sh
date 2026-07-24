#!/usr/bin/env bash
set -euo pipefail

# Host-side tests for the LiteGo rules core and Monte-Carlo AI.
#
# The engine (projects/10-litego-touch-coach/src/Go*.{h,cpp}) is deliberately
# free of Arduino.h and String, so the exact translation units that ship in the
# firmware also build with a plain g++ here. That makes rules fixtures and AI
# tuning a one-second loop instead of a flash-and-squint cycle.
#
# Usage:
#   ./scripts/test-litego.sh            full run, includes the strength tournament
#   ./scripts/test-litego.sh --quick    fixtures + hygiene + bench only

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/projects/10-litego-touch-coach"
OUT="${BUILD_ROOT:-$ROOT/_arduino-build}/litego-host"

CXX="${CXX:-g++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
  echo "$CXX is required to run the LiteGo host tests." >&2
  exit 1
fi

mkdir -p "$OUT"

echo "Building LiteGo host tests with $CXX"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
  -DLITEGO_HOST_BUILD=1 \
  "$PROJECT/src/GoBoard.cpp" \
  "$PROJECT/src/GoAi.cpp" \
  "$PROJECT/src/GoFixtures.cpp" \
  "$PROJECT/test/host_main.cpp" \
  -o "$OUT/litego-tests"

echo
"$OUT/litego-tests" "$@"
