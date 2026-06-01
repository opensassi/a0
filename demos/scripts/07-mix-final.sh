#!/usr/bin/env bash
set -euo pipefail

[ $# -ne 3 ] && echo "Usage: 07-mix-final.sh <video.mp4> <audio.m4a> <out.mp4>" && exit 1

video="$1"
audio="$2"
out="$3"

[ ! -f "$video" ] && echo "Missing: $video" && exit 1
[ ! -f "$audio" ] && echo "Missing: $audio" && exit 1

echo "Mixing video + audio..."
ffmpeg -i "$video" -i "$audio" \
  -c:v copy -c:a aac -b:a 192k -shortest -y "$out"

ls -lh "$out"
dur=$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$out")
printf "Duration: %.1fs\n" "$dur"
