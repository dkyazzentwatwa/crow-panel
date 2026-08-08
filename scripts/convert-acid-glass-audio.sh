#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 INPUT_AUDIO OUTPUT.WAV" >&2
  exit 2
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "ffmpeg is required (brew install ffmpeg)" >&2
  exit 1
fi

ffmpeg -hide_banner -loglevel error -y -i "$1" -vn -acodec pcm_s16le -ar 44100 -ac 2 "$2"
echo "Created PCM16 44.1 kHz stereo WAV: $2"
