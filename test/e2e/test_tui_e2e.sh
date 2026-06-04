#!/usr/bin/env bash
# test_tui_e2e.sh — Full TUI E2E test with mock DeepSeek API
#
# Starts the Python mock DeepSeek server, then drives a0 tui
# via --test-mode (FixedSize screen) with scripted input.
#
# Usage: bash test/e2e/test_tui_e2e.sh [--no-cleanup]

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
A0_BIN="${BUILD_DIR}/a0"
MOCK_PORT=18080
A0_DIR="/tmp/a0-tui-e2e-$$"
PASS_COUNT=0
FAIL_COUNT=0

cleanup() {
    if [ -z "${NO_CLEANUP:-}" ]; then
        kill $MOCK_PID 2>/dev/null || true
        wait $MOCK_PID 2>/dev/null || true
        rm -rf "$A0_DIR" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

# ---------------------------------------------------------------------------
# Phase 1: Start mock DeepSeek server
# ---------------------------------------------------------------------------
echo "=== TUI E2E: Starting mock DeepSeek server on port ${MOCK_PORT} ==="
python3 "${PROJECT_ROOT}/test/e2e/mock_deepseek_server.py" --port ${MOCK_PORT} &
MOCK_PID=$!

# Wait for mock server to be ready
for i in {1..10}; do
    if curl -s "http://localhost:${MOCK_PORT}/health" >/dev/null 2>&1; then
        echo "  Mock server ready"
        break
    fi
    if [ $i -eq 10 ]; then
        echo "  ERROR: Mock server failed to start"
        exit 1
    fi
    sleep 0.5
done

# ---------------------------------------------------------------------------
# Phase 2: Verify a0 binary exists
# ---------------------------------------------------------------------------
if [ ! -x "$A0_BIN" ]; then
    echo "  ERROR: a0 binary not found at ${A0_BIN}"
    exit 1
fi

echo "=== TUI E2E: Running TUI in test mode ==="

# ---------------------------------------------------------------------------
# Phase 3: Run TUI with scripted input via expect
# ---------------------------------------------------------------------------
# We use `expect` to script interactive FTXUI input since the TUI
# reads from the terminal, not stdin.
if command -v expect &>/dev/null; then
    expect <<EXPECT_SCRIPT
set timeout 10
set a0_bin [exec which ${A0_BIN}]
set a0_dir "${A0_DIR}"

spawn \$a0_bin tui --test-mode --mock-api http://localhost:${MOCK_PORT} --a0-dir \$a0_dir --no-docker --no-permissions

# Wait for TUI to start (status bar should show)
sleep 2

# Test 1: Type a goal and submit
send "hello\r"
sleep 3

# Test 2: Verify response appears
expect {
    "Processing" { set test1_pass 1 }
    timeout { set test1_pass 0 }
}

# Test 3: Type /help command
send "/help\r"
sleep 1

# Test 4: Verify help dialog appears
expect {
    "Keybindings" { set test2_pass 1 }
    timeout { set test2_pass 0 }
}

# Test 5: Submit another goal
send "/clear\r"
sleep 1

# Clean exit
send ":q\r"
sleep 1

# Report results
if { \$test1_pass && \$test2_pass } {
    puts "\nALL TUI E2E TESTS PASSED"
} else {
    if { !\$test1_pass } { puts "FAIL: Submit goal did not produce response" }
    if { !\$test2_pass } { puts "FAIL: /help did not show keybindings" }
    exit 1
}
EXPECT_SCRIPT
else
    echo "  SKIP: 'expect' not installed — skipping interactive TUI E2E tests"
    echo "  Install with: apt install expect"
fi

echo ""
echo "=== TUI E2E: Done ==="
