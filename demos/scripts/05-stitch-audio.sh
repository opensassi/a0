#!/usr/bin/env bash
set -euo pipefail

[ $# -lt 2 ] && echo "Usage: 05-stitch-audio.sh <out.m4a> <file1.aac> [file2.aac ...]" && exit 1

out="$1"
shift
files=("$@")
n=${#files[@]}

echo "Stitching $n audio segments..."

filter=""
for i in "${!files[@]}"; do
  filter+="[${i}:a]"
done
filter+="concat=n=${n}:v=0:a=1[a]"

cmd=(ffmpeg)
for f in "${files[@]}"; do
  cmd+=(-i "$f")
done
cmd+=(-filter_complex "$filter" -map "[a]" -c:a aac -b:a 48k -y "$out")

"${cmd[@]}"

dur=$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$out")
printf "Done: %s (%.1fs)\n" "$out" "$dur"
