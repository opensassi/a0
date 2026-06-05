#!/usr/bin/env bash
# test_tui_e2e.sh — Full TUI E2E test with mock DeepSeek API
#
# Uses Python PTY-based test driver (no expect needed).
# Replaced the old bash+expect approach.
#
# Usage: bash test/e2e/test_tui_e2e.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
A0_BIN="${PROJECT_DIR}/build/a0"

echo "=== TUI E2E tests (Python PTY driver) ==="
echo ""

if [ ! -x "$A0_BIN" ]; then
    echo "  ERROR: a0 binary not found at ${A0_BIN}"
    exit 1
fi

cd "$PROJECT_DIR"

if python3 -m pytest test/e2e/test_tui_e2e.py -v; then
    echo ""
    echo "All TUI E2E tests PASSED"
    exit 0
else
    echo ""
    echo "Some TUI E2E tests FAILED"
    exit 1
fi
