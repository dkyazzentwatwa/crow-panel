#!/usr/bin/env bash
set -euo pipefail

# Convert video for the Cypher Desk (project 18) video player.
#
# Target: MJPEG video plus interleaved 16-bit PCM audio in an AVI container.
# That combination is chosen for the hardware, not for tidiness:
#
#   - MJPEG frames go straight to the ESP32-P4's hardware JPEG decoder, which
#     core 3.3.8 already links. Nothing else the panel can decode needs no
#     third-party dependency.
#   - Every frame is a keyframe, so a dropped frame costs one frame rather
#     than corrupting everything until the next I-frame.
#   - AVI interleaves audio next to the video it belongs with, so playback is
#     one sequential read off the card instead of two competing seeks.
#   - PCM audio needs no decoder at all; the mixer resamples it.
#
# Defaults are sized for the panel: 512 wide at 15 fps is roughly 300 KB/s,
# comfortable on SD_MMC in 1-bit mode, and the player scales it up to the
# window with the PPA. The decoder is built for CYPHER_DESK_VIDEO_MAX_W x
# CYPHER_DESK_VIDEO_MAX_H (640x480 by default) and refuses anything larger
# with its real dimensions in the message.
#
# Usage:
#   ./scripts/convert-crowpanel-video.sh input.mp4 [output.avi]
#   WIDTH=640 FPS=20 QUALITY=4 ./scripts/convert-crowpanel-video.sh clip.mov
#
# Copy the result to /cypher-puter/desk/video/ on a FAT32 card.
# Never commit converted media; it is not yours to redistribute.

WIDTH="${WIDTH:-512}"
FPS="${FPS:-15}"
# ffmpeg's MJPEG quality scale: 2 is best, 31 is worst. 6 is a good balance
# between file size and the SD read budget above.
QUALITY="${QUALITY:-6}"
AUDIO_RATE="${AUDIO_RATE:-44100}"
AUDIO_CHANNELS="${AUDIO_CHANNELS:-2}"

if [ $# -lt 1 ]; then
  sed -n '3,31p' "$0" | sed 's/^# \{0,1\}//'
  exit 1
fi

INPUT="$1"
OUTPUT="${2:-${INPUT%.*}.avi}"

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "ffmpeg is required. brew install ffmpeg" >&2
  exit 1
fi
if [ ! -f "$INPUT" ]; then
  echo "No such file: $INPUT" >&2
  exit 1
fi

echo "Converting $INPUT"
echo "  video  MJPEG ${WIDTH}px wide, ${FPS} fps, q${QUALITY}"
echo "  audio  PCM s16le ${AUDIO_RATE} Hz, ${AUDIO_CHANNELS} channel(s)"

# scale=W:-2 keeps the aspect ratio and forces an even height, which the JPEG
# encoder requires. The player letterboxes rather than stretching, so the
# source aspect is preserved on the panel.
ffmpeg -v error -y -i "$INPUT" \
  -vf "scale=${WIDTH}:-2,fps=${FPS}" \
  -c:v mjpeg -q:v "$QUALITY" -pix_fmt yuvj420p \
  -c:a pcm_s16le -ar "$AUDIO_RATE" -ac "$AUDIO_CHANNELS" \
  "$OUTPUT"

SIZE_BYTES=$(wc -c < "$OUTPUT" | tr -d ' ')
DURATION=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$OUTPUT" 2>/dev/null || echo 0)
echo "Wrote $OUTPUT ($((SIZE_BYTES / 1024)) KB)"
if [ "${DURATION%.*}" -gt 0 ] 2>/dev/null; then
  echo "  average $((SIZE_BYTES / ${DURATION%.*} / 1024)) KB/s off the card"
fi
echo
echo "Copy to /cypher-puter/desk/video/ on a FAT32 card (not exFAT)."
echo "The panel reports dropped frames in the player; if that number climbs,"
echo "lower WIDTH or FPS, or raise QUALITY toward 31."
