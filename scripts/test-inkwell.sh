#!/usr/bin/env bash
set -euo pipefail

# Host-side tests for Inkwell (project 25): parsers, EPUB container walk and
# paginator — the code where a silent off-by-one reads as a corrupt book.
# All TUs under test are Arduino-free; the shipping files are the tested files.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/in-progress/25-inkwell"
OUT="${BUILD_ROOT:-$ROOT/_arduino-build}/inkwell-host"

CXX="${CXX:-g++}"
command -v "$CXX" >/dev/null 2>&1 || { echo "$CXX required" >&2; exit 1; }
mkdir -p "$OUT"

echo "Building Inkwell host tests with $CXX"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
  "$PROJECT/src/TxtParser.cpp" \
  "$PROJECT/test/host_main.cpp" \
  -o "$OUT/inkwell-tests"

"$OUT/inkwell-tests" "$@"
