#!/usr/bin/env bash
# extract_artifacts: Parse .spec.md files, extract mermaid and D3 blocks to .artifacts/
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

BASENAME=$(basename "$FILE" .spec.md)
ARTIFACT_DIR=".artifacts/${BASENAME}"
mkdir -p "$ARTIFACT_DIR"

# Extract mermaid blocks
awk '/^```mermaid$/,/^```$/' "$FILE" | sed '/^```/d' > "${ARTIFACT_DIR}/diagram.mmd" 2>/dev/null || true

# Extract D3 HTML blocks
awk '/^```html$/,/^```$/' "$FILE" | sed '/^```/d' > "${ARTIFACT_DIR}/d3-animation.html" 2>/dev/null || true

echo "Extracted artifacts to ${ARTIFACT_DIR}/"
