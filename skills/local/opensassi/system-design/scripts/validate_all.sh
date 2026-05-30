#!/usr/bin/env bash
# validate_all: Run extract + test on all .spec.md files
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXTRACT="$SCRIPT_DIR/extract_artifacts.sh"
TEST="$SCRIPT_DIR/test_artifacts.sh"

SPEC_FILES=$(find . -name "*.spec.md" -not -path "./node_modules/*" -not -path "./.git/*" 2>/dev/null || true)

if [ -z "$SPEC_FILES" ]; then
  echo "No .spec.md files found."
  exit 0
fi

FAILED=0
for FILE in $SPEC_FILES; do
  echo "=== $FILE ==="
  bash "$EXTRACT" --file "$FILE" || { echo "EXTRACT FAILED"; FAILED=$((FAILED+1)); continue; }
  bash "$TEST" --file "$FILE" || { echo "TEST FAILED"; FAILED=$((FAILED+1)); }
done

if [ $FAILED -eq 0 ]; then
  echo "All artifacts validated successfully."
else
  echo "$FAILED spec file(s) had errors."
  exit 1
fi
