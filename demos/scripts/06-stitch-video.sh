#!/usr/bin/env bash
set -euo pipefail

[ $# -lt 2 ] && echo "Usage: 06-stitch-video.sh <out.mp4> <file1.webm> [file2.webm ...]" && exit 1

out="$1"
shift
files=("$@")
n=${#files[@]}

echo "Stitching $n video clips..."

filter=""
for i in "${!files[@]}"; do
  filter+="[${i}:v]"
done
filter+="concat=n=${n}:v=1:a=0[out]"

cmd=(ffmpeg)
for f in "${files[@]}"; do
  cmd+=(-i "$f")
done
cmd+=(-filter_complex "$filter" -map "[out]" -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -y "$out")

"${cmd[@]}"

ls -lh "$out"
