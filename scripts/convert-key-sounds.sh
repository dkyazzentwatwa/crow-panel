#!/usr/bin/env bash
set -euo pipefail

# Converts a kbsim-style keyboard sound tree into the 16-bit PCM WAV layout
# Project 21 (Cypher Keys) loads from SD.
#
#   ./scripts/convert-key-sounds.sh <src-audio-dir> <out-dir> [rate]
#
# Source layout (kbsim, https://github.com/tplai/kbsim — mp3, any rate):
#   <src>/<pack>/press/{GENERIC_R0..R4,BACKSPACE,ENTER,SPACE}.mp3
#   <src>/<pack>/release/{GENERIC,BACKSPACE,ENTER,SPACE}.mp3
#
# Output layout (copy <out-dir>/* to /cypher-keys/sounds/ on a FAT32 card):
#   <out>/<pack>/press/<NAME>.wav      16-bit PCM, mono, `rate` Hz
#   <out>/<pack>/release/<NAME>.wav
#
# Names are preserved so the firmware can pick a row-specific press sound
# (GENERIC_R<row>) and dedicated BACKSPACE/ENTER/SPACE clips, falling back to
# GENERIC_R0 / GENERIC when a pack omits one (e.g. kbsim's mxblue).
#
# The sample rate must match CYPHER_KEYS_AUDIO_SAMPLE_RATE so the click engine
# never has to resample on the keypress path.
#
# NOTE ON ASSETS: sound packs are NOT vendored into this repo. kbsim's code is
# MIT but it documents no provenance or licence for the recordings themselves,
# so convert them onto your own card for personal use and keep them out of Git.
# The built-in synthesized profiles (Blue/Brown/Red) remain the shipped default.

if [[ $# -lt 2 ]]; then
  sed -n '3,28p' "$0" >&2
  exit 1
fi

SRC="$1"
OUT="$2"
RATE="${3:-22050}"

if [[ ! -d "$SRC" ]]; then
  echo "source dir not found: $SRC" >&2
  exit 1
fi
if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "ffmpeg is required (brew install ffmpeg)" >&2
  exit 1
fi

mkdir -p "$OUT"
PACKS=0
CLIPS=0

for packdir in "$SRC"/*/; do
  [[ -d "$packdir" ]] || continue
  pack="$(basename "$packdir")"
  found=0
  for phase in press release; do
    [[ -d "$packdir/$phase" ]] || continue
    mkdir -p "$OUT/$pack/$phase"
    for f in "$packdir/$phase"/*; do
      [[ -f "$f" ]] || continue
      base="$(basename "${f%.*}")"
      # -ac 1 mono, -ar RATE, pcm_s16le: exactly what the engine plays.
      if ffmpeg -v error -y -i "$f" -ac 1 -ar "$RATE" -c:a pcm_s16le \
          "$OUT/$pack/$phase/$base.wav" 2>/dev/null; then
        CLIPS=$((CLIPS + 1))
        found=1
      else
        echo "  ! failed: $pack/$phase/$(basename "$f")" >&2
      fi
    done
  done
  if [[ $found -eq 1 ]]; then
    PACKS=$((PACKS + 1))
    printf '  %-12s press=%s release=%s\n' "$pack" \
      "$(ls "$OUT/$pack/press" 2>/dev/null | wc -l | tr -d ' ')" \
      "$(ls "$OUT/$pack/release" 2>/dev/null | wc -l | tr -d ' ')"
  fi
done

echo
echo "converted $CLIPS clips across $PACKS packs at ${RATE} Hz -> $OUT"
echo "total size: $(du -sh "$OUT" | cut -f1)"
echo
echo "Next: copy the pack folders to /cypher-keys/sounds/ on a FAT32 SD card:"
echo "  cp -R \"$OUT\"/* /Volumes/<CARD>/cypher-keys/sounds/"
