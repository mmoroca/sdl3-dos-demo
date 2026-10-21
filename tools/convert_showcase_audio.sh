#!/bin/sh
# Convert the four CC0 OGG source tracks to DOS-friendly U8 mono WAV assets.
# Usage: sh tools/convert_showcase_audio.sh [OGG_SOURCE_DIR] [WAV_OUTPUT_DIR]
set -eu

SOURCE_DIR=${1:-docs}
OUTPUT_DIR=${2:-SHOWCASE}
FFMPEG=${FFMPEG:-ffmpeg}

if ! command -v "$FFMPEG" >/dev/null 2>&1; then
    echo "Error: FFmpeg was not found. Install it or set FFMPEG=/path/to/ffmpeg." >&2
    exit 1
fi
if [ ! -d "$SOURCE_DIR" ]; then
    echo "Error: OGG source directory not found: $SOURCE_DIR" >&2
    exit 1
fi
mkdir -p "$OUTPUT_DIR"

# SDL3 DOS playback uses one unsigned 8-bit mono stream at 11025 Hz. OGG is
# converted off-line so the executable does not need a Vorbis decoder.
"$FFMPEG" -nostdin -y -i "$SOURCE_DIR/Space Cadet.ogg" -vn -ac 1 -ar 11025 -c:a pcm_u8 "$OUTPUT_DIR/MUSIC.WAV"
"$FFMPEG" -nostdin -y -i "$SOURCE_DIR/laser2.ogg"       -vn -ac 1 -ar 11025 -c:a pcm_u8 "$OUTPUT_DIR/LASER.WAV"
"$FFMPEG" -nostdin -y -i "$SOURCE_DIR/explosion4.ogg"   -vn -ac 1 -ar 11025 -c:a pcm_u8 "$OUTPUT_DIR/HIT.WAV"
"$FFMPEG" -nostdin -y -i "$SOURCE_DIR/engine5.ogg"      -vn -ac 1 -ar 11025 -c:a pcm_u8 "$OUTPUT_DIR/ENGINE.WAV"

echo "Created U8 mono, 11025 Hz WAV files in: $OUTPUT_DIR"
