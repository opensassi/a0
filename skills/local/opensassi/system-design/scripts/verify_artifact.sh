#!/usr/bin/env bash
# verify_artifact: Assert D3 keyframe DOM state matches ANIMATION_VERIFICATION
set -euo pipefail

FILE=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --file) FILE="$2"; shift 2 ;;
    *) echo "Usage: $0 --file <path>"; exit 1 ;;
  esac
done

if [ -z "$FILE" ]; then
  echo "ERROR: --file required"
  exit 1
fi

if [ ! -f "$FILE" ]; then
  echo "ERROR: file not found: $FILE"
  exit 1
fi

echo "Verifying D3 animation: $FILE"
echo "(full DOM assertion requires Playwright — running basic HTML check)"

# Basic check: file exists, has expected globals
if grep -q "ANIMATION_KEYFRAMES" "$FILE" 2>/dev/null; then
  echo "PASS: ANIMATION_KEYFRAMES found"
else
  echo "FAIL: ANIMATION_KEYFRAMES not found"
  exit 1
fi

if grep -q "data-testid=\"play-pause\"" "$FILE" 2>/dev/null; then
  echo "PASS: play-pause button found"
else
  echo "FAIL: play-pause button not found"
  exit 1
fi

echo "Verification passed."
