#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FQBN="${FQBN:-esp32:esp32:esp32}"

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <project-folder> <serial-port>" >&2
  echo "Example: $0 projects/03-badgeops-nfc-rfid-system /dev/cu.usbserial-0001" >&2
  exit 1
fi

PROJECT_DIR="$1"
PORT="$2"

if [[ "$PROJECT_DIR" != /* ]]; then
  PROJECT_DIR="$ROOT/$PROJECT_DIR"
fi

if [[ ! -d "$PROJECT_DIR" ]]; then
  echo "Project folder not found: $PROJECT_DIR" >&2
  exit 1
fi

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli is required. Run scripts/install-cores.sh first." >&2
  exit 1
fi

echo "Uploading $PROJECT_DIR"
echo "Port: $PORT"
echo "FQBN: $FQBN"
echo "Warning: verify this FQBN against the real CrowPanel ESP32-P4 board package before uploading."

arduino-cli upload \
  --fqbn "$FQBN" \
  --libraries "$ROOT/shared" \
  -p "$PORT" \
  "$PROJECT_DIR"
