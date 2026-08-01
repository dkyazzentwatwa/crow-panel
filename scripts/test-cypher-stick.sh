#!/usr/bin/env bash
set -euo pipefail

# Host-side tests for project 23 (Cypher Stick). No board required.
#
# Covers the logic that is silent when it goes wrong: SOCD resolution (a wrong
# hat value is a wrong input, not a crash), key hit-testing, and profile
# serialisation. These compile the shipping src/ headers, which are deliberately
# free of Arduino.h so this is possible.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/in-progress/23-cypher-stick"
OUT="${BUILD_ROOT:-$ROOT/_arduino-build}/cypher-stick-host"
CXX="${CXX:-g++}"

mkdir -p "$OUT"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
  "$PROJECT/test/host_main.cpp" \
  "$PROJECT/src/SocdCleaner.cpp" \
  "$PROJECT/src/StickLayout.cpp" \
  -o "$OUT/cypher-stick-tests"

"$OUT/cypher-stick-tests" "$@"
