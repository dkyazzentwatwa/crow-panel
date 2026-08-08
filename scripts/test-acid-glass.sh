#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/in-progress/24-acid-glass-visualizer"
BUILD_DIR="${BUILD_ROOT:-$ROOT/_host-build}/acid-glass"

mkdir -p "$BUILD_DIR"
c++ -std=c++17 -Wall -Wextra -Werror \
  "$PROJECT/test/host_main.cpp" -o "$BUILD_DIR/acid-glass-host-tests"
"$BUILD_DIR/acid-glass-host-tests"
