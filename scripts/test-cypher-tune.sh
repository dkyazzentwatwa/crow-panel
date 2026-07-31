#!/usr/bin/env bash
set -euo pipefail

# Host-side tests for project 09 (Cypher Tune MPC). No board required.
#
# Covers the arithmetic that is silent when it goes wrong: the Q16 pitch table
# that replaced powf() on the render task, and the backing-loop tempo lock,
# where source frames and engine frames stopped being the same number when the
# engine moved to 32 kHz.
#
# These compile the shipping src/LoopLock.h, not a copy of it - that header is
# deliberately free of Arduino.h so this is possible.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/projects/09-cypher-tune-mpc"
OUT="${BUILD_ROOT:-$ROOT/_arduino-build}/cypher-tune-host"
CXX="${CXX:-g++}"

mkdir -p "$OUT"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
  "$PROJECT/test/host_main.cpp" \
  -o "$OUT/cypher-tune-tests"

"$OUT/cypher-tune-tests" "$@"
