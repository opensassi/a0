#!/usr/bin/env bash
set -euo pipefail
dir="${1:-.}"
total=0
echo "Padded audio segment durations:"
echo "---"
for f in "$dir"/audio-0*-padded.aac; do
  [ -f "$f" ] || continue
  dur=$(ffmpeg -i "$f" -f null - 2>&1 | grep -oP 'time=\K[\d:\.]+' | tail -1)
  IFS=: read -r h m s <<<"$dur"
  dur=$(echo "$h * 3600 + $m * 60 + $s" | bc)
  printf "  %-55s %6.2fs\n" "$(basename "$f")" "$dur"
  total=$(echo "$total + $dur" | bc)
done
echo "---"
printf "  %-55s %6.2fs\n" "TOTAL" "$total"
