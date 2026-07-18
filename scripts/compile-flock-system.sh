#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

python3 scripts/generate-flock-catalog.py --check
python3 scripts/test-flock-catalog.py
python3 scripts/test-flock-protocol.py
CTAGS_WORKAROUND=1 scripts/build-flock-bridge.sh
scripts/build-flock-bw16.sh
BUILD_ROOT="$ROOT/_arduino-build/cypher-flock-bw16-fallback" \
  EXTRA_FLAGS="-DUSE_BW16_PROMISCUOUS=0" scripts/build-flock-bw16.sh

echo
echo "Cypher Flock companions and catalog are compile-ready."
echo "Run the Project 16 flag-matrix rows separately for the CrowPanel P4 target."
