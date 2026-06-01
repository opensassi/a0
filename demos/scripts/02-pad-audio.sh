#!/usr/bin/env bash
set -euo pipefail
INPUT="$1"
DIR="$(dirname "$INPUT")"
BASE="$(basename "$INPUT" .aac)"
OUTPUT="${DIR}/${BASE}-padded.aac"

ffmpeg -i "$INPUT" \
  -f lavfi -t 0.5 -i anullsrc=r=24000:cl=mono \
  -f lavfi -t 0.5 -i anullsrc=r=24000:cl=mono \
  -filter_complex "[1:a][0:a][2:a]concat=n=3:v=0:a=1[a]" \
  -map "[a]" -c:a aac -b:a 48k -ar 24000 -ac 1 -y "$OUTPUT"
