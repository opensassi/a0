#!/usr/bin/env bash
# E2E tests for a0 agent (mock DeepSeek API, no real credentials needed)
#
# Replaced the old piped-stdin tests with Python pytest-based tests
# that use the AgentSubprocess helper (a0 run <goal>) correctly.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "=== Agent Headless E2E tests ==="
echo ""

cd "$PROJECT_DIR"

if python3 -m pytest test/e2e/test_agent_e2e.py -v; then
    echo ""
    echo "All agent E2E tests PASSED"
    exit 0
else
    echo ""
    echo "Some agent E2E tests FAILED"
    exit 1
fi
