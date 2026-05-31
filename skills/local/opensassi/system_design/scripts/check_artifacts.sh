#!/usr/bin/env bash
# check_artifacts: Scan artifact metadata for MISSING/STALE review entries
set -euo pipefail

ARTIFACT_DIR=".artifacts"

if [ ! -d "$ARTIFACT_DIR" ]; then
  echo "No .artifacts/ directory found."
  exit 0
fi

STALE=0
MISSING=0
for SPEC in $(find . -name "*.spec.md" -not -path "./node_modules/*" -not -path "./.git/*" 2>/dev/null || true); do
  BASENAME=$(basename "$SPEC" .spec.md)
  ARTIFACT="${ARTIFACT_DIR}/${BASENAME}"
  if [ ! -d "$ARTIFACT" ]; then
    echo "MISSING: $BASENAME (no artifacts)"
    MISSING=$((MISSING+1))
  elif [ ! -f "${ARTIFACT}/diagram.png" ] && [ ! -f "${ARTIFACT}/d3-animation.html" ]; then
    echo "MISSING: $BASENAME (no rendered artifacts)"
    MISSING=$((MISSING+1))
  fi
done

if [ $MISSING -eq 0 ] && [ $STALE -eq 0 ]; then
  echo "All reviews current."
else
  echo "Found $MISSING missing, $STALE stale."
fi
