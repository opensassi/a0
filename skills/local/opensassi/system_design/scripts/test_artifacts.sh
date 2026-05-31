#!/usr/bin/env bash
# test_artifacts: Render mermaid to PNG, capture D3 filmstrip
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

BASENAME=$(basename "$FILE" .spec.md)
ARTIFACT_DIR=".artifacts/${BASENAME}"

if [ ! -d "$ARTIFACT_DIR" ]; then
  echo "ERROR: no artifacts directory, run extract_artifacts first"
  exit 1
fi

# Render mermaid if present
if [ -f "${ARTIFACT_DIR}/diagram.mmd" ] && command -v mmdc &>/dev/null; then
  mmdc -i "${ARTIFACT_DIR}/diagram.mmd" -o "${ARTIFACT_DIR}/diagram.png" 2>/dev/null
  echo "Rendered mermaid diagram to ${ARTIFACT_DIR}/diagram.png"
else
  echo "No mermaid diagram found or mmdc not installed"
fi

# Check D3 animation
if [ -f "${ARTIFACT_DIR}/d3-animation.html" ] && command -v npx &>/dev/null; then
  echo "D3 animation present: ${ARTIFACT_DIR}/d3-animation.html"
  echo "(filmstrip capture requires Playwright — skipped in test mode)"
else
  echo "No D3 animation found"
fi

echo "Test complete."
