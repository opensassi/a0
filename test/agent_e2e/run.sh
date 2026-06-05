#!/usr/bin/env bash
# Agent E2E test runner — runs all agent-facing E2E tests
# Usage: bash test/agent_e2e/run.sh [--scenarios-only] [--tui-only] [--crashes-only]

set -euo pipefail
cd "$(dirname "$0")/../.."
PROJECT_ROOT="$(pwd)"

PASS_COUNT=0
FAIL_COUNT=0
PYTHON="${PYTHON:-python3}"
BUILD_DIR="${PROJECT_ROOT}/build"
A0_BIN="${BUILD_DIR}/a0"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}=== Agent E2E Test Suite ===${NC}"
echo "  a0 binary: ${A0_BIN}"
echo "  python:    ${PYTHON}"
echo ""

# Ensure a0 is built
if [ ! -x "$A0_BIN" ]; then
    echo "  Building a0..."
    cmake --build "$BUILD_DIR" -j$(nproc) --target a0 2>&1 | tail -3
fi
if [ ! -x "$A0_BIN" ]; then
    echo -e "${RED}ERROR: a0 binary not found. Build first.${NC}"
    exit 1
fi

run_test() {
    local name="$1"
    local file="$2"
    local filter="${3:-}"
    echo -n "  [TEST] ${name}... "
    local cmd="${PYTHON} -m pytest ${file} -v --tb=short --no-header"
    if [ -n "$filter" ]; then
        cmd="${cmd} -k \"${filter}\""
    fi
    if eval "$cmd" > /tmp/a0-e2e-test.log 2>&1; then
        echo -e "${GREEN}PASS${NC}"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}FAIL${NC}"
        cat /tmp/a0-e2e-test.log | tail -30
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

echo "=== Crash Tests ==="
run_test "Binary exists" "test/agent_e2e/test_crashes.py" "test_binary_exists"
run_test "Help flag" "test/agent_e2e/test_crashes.py" "test_help_flag_no_crash"
run_test "Minimal run" "test/agent_e2e/test_crashes.py" "test_run_minimal_no_crash"
run_test "Run with mock" "test/agent_e2e/test_crashes.py" "test_run_with_mock_no_crash"
run_test "Session flags" "test/agent_e2e/test_crashes.py" "test_run_session_flags"
run_test "Skill arg" "test/agent_e2e/test_crashes.py" "test_run_with_skill_arg"
run_test "Consecutive goals" "test/agent_e2e/test_crashes.py" "test_consecutive_goals_no_crash"
run_test "Max parallel" "test/agent_e2e/test_crashes.py" "test_max_parallel_flag"
run_test "CLI flag combinations" "test/agent_e2e/test_crashes.py" "test_cli_flag_combinations"
run_test "Stress rapid goals" "test/agent_e2e/test_crashes.py" "test_stress_rapid_goals"

echo ""
echo "=== Scenario Tests ==="
run_test "All scenarios load" "test/agent_e2e/test_scenarios.py" "test_all_scenarios_load"
run_test "Simple tool call" "test/agent_e2e/test_scenarios.py" "test_simple_tool_call"
run_test "Multi-turn workflow" "test/agent_e2e/test_scenarios.py" "test_multi_turn_workflow"
run_test "Tool error handling" "test/agent_e2e/test_scenarios.py" "test_tool_error_handling"
run_test "Clipboard wrappers" "test/agent_e2e/test_scenarios.py" "test_clipboard_wrappers_exist"

echo ""
echo "=== TUI Rendering Tests ==="
# TUI tests use the PTY driver; may need --test-mode built in
run_test "Status bar idle" "test/agent_e2e/test_tui_rendering.py" "test_tui_status_bar_idle"
run_test "Submit goal" "test/agent_e2e/test_tui_rendering.py" "test_tui_submit_goal_shows_response"
run_test "Help command" "test/agent_e2e/test_tui_rendering.py" "test_tui_help_command"
run_test "Clear command" "test/agent_e2e/test_tui_rendering.py" "test_tui_clear_command"
run_test "Sessions command" "test/agent_e2e/test_tui_rendering.py" "test_tui_sessions_command"
run_test "Multiple messages" "test/agent_e2e/test_tui_rendering.py" "test_tui_multiple_messages"
run_test "Interrupt streaming" "test/agent_e2e/test_tui_rendering.py" "test_tui_interrupt_streaming"
run_test "Status bar updates" "test/agent_e2e/test_tui_rendering.py" "test_tui_status_bar_updates"
run_test "Mouse drag" "test/agent_e2e/test_tui_rendering.py" "test_tui_mouse_drag_no_crash"
run_test "Quick quit" "test/agent_e2e/test_tui_rendering.py" "test_tui_quick_quit"
run_test "Scrollback many messages" "test/agent_e2e/test_tui_rendering.py" "test_tui_scrollback_many_messages"

echo ""
echo "=== Clipboard Tests ==="
run_test "Mock wrappers work" "test/agent_e2e/test_clipboard.py" "test_mock_wrappers_work"
run_test "OSC 52 sequence" "test/agent_e2e/test_clipboard.py" "test_osc52_sequence_in_output"
run_test "Copy selects status bar" "test/agent_e2e/test_clipboard.py" "test_copy_selects_status_bar"
run_test "Copy after submit goal" "test/agent_e2e/test_clipboard.py" "test_copy_after_submit_goal"
run_test "Selection highlight drag" "test/agent_e2e/test_clipboard.py" "test_selection_highlight_during_drag"
run_test "Copy after drag no crash" "test/agent_e2e/test_clipboard.py" "test_copy_after_drag_no_crash"

echo ""
echo "=== Paste Tests ==="
run_test "Paste large collapses" "test/agent_e2e/test_paste.py" "test_paste_large_content_collapses"
run_test "Paste small raw" "test/agent_e2e/test_paste.py" "test_paste_small_content_raw"
run_test "Paste multiple placeholders" "test/agent_e2e/test_paste.py" "test_paste_multiple_placeholders"
run_test "Paste manual reference" "test/agent_e2e/test_paste.py" "test_paste_manual_reference"
run_test "Paste cursor after placeholder" "test/agent_e2e/test_paste.py" "test_paste_cursor_after_placeholder"
run_test "Paste newlines dont submit" "test/agent_e2e/test_paste.py" "test_paste_newlines_dont_submit"

echo ""
echo "=== Mixed Tests ==="
run_test "Paste always goes to input" "test/agent_e2e/test_tui_rendering.py" "test_paste_always_goes_to_input"

echo ""
echo -e "${CYAN}========================================${NC}"
echo -e "  ${GREEN}${PASS_COUNT} passed${NC}, ${RED}${FAIL_COUNT} failed${NC}"
echo -e "${CYAN}========================================${NC}"

[ "$FAIL_COUNT" -eq 0 ] || exit 1
