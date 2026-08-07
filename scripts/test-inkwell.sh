#!/usr/bin/env bash
set -euo pipefail

# Host-side tests for Inkwell (project 25): parsers, EPUB container walk and
# paginator — the code where a silent off-by-one reads as a corrupt book.
# All TUs under test are Arduino-free; the shipping files are the tested files.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/in-progress/25-inkwell"
OUT="${BUILD_ROOT:-$ROOT/_arduino-build}/inkwell-host"

CC="${CC:-gcc}"
CXX="${CXX:-g++}"
command -v "$CXX" >/dev/null 2>&1 || { echo "$CXX required" >&2; exit 1; }
command -v "$CC" >/dev/null 2>&1 || { echo "$CC required" >&2; exit 1; }
mkdir -p "$OUT"

# miniz is vendored, third-party C -- compiled separately, without our
# -Wall -Wextra -Werror (it isn't our code to keep warning-clean).
echo "Compiling vendored miniz.c with $CC"
"$CC" -O2 -c "$PROJECT/src/miniz.c" -o "$OUT/miniz.o"

echo "Building Inkwell host tests with $CXX"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
  "$PROJECT/src/TxtParser.cpp" \
  "$PROJECT/src/MarkdownParser.cpp" \
  "$PROJECT/src/XhtmlParser.cpp" \
  "$PROJECT/src/EpubBook.cpp" \
  "$PROJECT/test/host_main.cpp" \
  "$OUT/miniz.o" \
  -o "$OUT/inkwell-tests"

"$OUT/inkwell-tests" "$@"
