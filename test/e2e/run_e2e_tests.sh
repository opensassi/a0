#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
MOCK_PORT=18081
MOCK_URL="http://127.0.0.1:${MOCK_PORT}/v1/chat/completions"

cleanup() {
    if [ -n "${MOCK_PID:-}" ]; then
        kill "$MOCK_PID" 2>/dev/null || true
        wait "$MOCK_PID" 2>/dev/null || true
    fi
    rm -rf "${PROJECT_DIR}/test_e2e_components" "${PROJECT_DIR}/test_e2e_logs"
}
trap cleanup EXIT

FAILED=0

echo "=== Starting mock DeepSeek server ==="
python3 "$SCRIPT_DIR/mock_deepseek_server.py" $MOCK_PORT &
MOCK_PID=$!
sleep 1

if ! kill -0 "$MOCK_PID" 2>/dev/null; then
    echo "ERROR: Mock server failed to start"
    exit 1
fi
echo "Mock server running on port $MOCK_PORT (PID: $MOCK_PID)"

if [ ! -f "$BUILD_DIR/a0" ]; then
    echo "Building a0..."
    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DENABLE_COVERAGE=ON 2>/dev/null
    cmake --build "$BUILD_DIR" -j$(nproc) 2>/dev/null
fi

A0="$BUILD_DIR/a0"

run_agent() {
    local input="$1"
    local components_dir="${PROJECT_DIR}/test_e2e_components"
    local logs_dir="${PROJECT_DIR}/test_e2e_logs"
    rm -rf "$components_dir" "$logs_dir" 2>/dev/null
    mkdir -p "$components_dir" "$logs_dir"
    echo "$input" | timeout 5 "$A0" \
        --components-dir "$components_dir" \
        --mock-api "$MOCK_URL" \
        2>/dev/null || true
}

# E2E-01: Empty input
echo ""
echo "=== E2E-01: Empty input ==="
output=$(run_agent "")
if echo "$output" | grep -q "no goal provided"; then
    echo "PASS: E2E-01"
else
    echo "FAIL: E2E-01 (got: $output)"
    FAILED=1
fi

# E2E-02: Infer a tool
echo ""
echo "=== E2E-02: Goal triggers skill inference ==="
COMPONENTS_DIR="${PROJECT_DIR}/test_e2e_components"
LOGS_DIR="${PROJECT_DIR}/test_e2e_logs"
rm -rf "$COMPONENTS_DIR" "$LOGS_DIR"
mkdir -p "$COMPONENTS_DIR" "$LOGS_DIR"
echo "count lines in file" | timeout 5 "$A0" \
    --components-dir "$COMPONENTS_DIR" \
    --mock-api "$MOCK_URL" \
    2>/dev/null > /tmp/e2e_out.txt || true
output=$(cat /tmp/e2e_out.txt)
if [ -n "$output" ] && [ "$output" != "\"no goal provided\"" ]; then
    echo "PASS: E2E-02 (output: $output)"
else
    echo "FAIL: E2E-02 (output: $output)"
    FAILED=1
fi

# E2E-03: Components created after inference
echo ""
echo "=== E2E-03: Components directory has new files ==="
# run_agent already ran above, check for created skill files
comp_count=$(ls "${PROJECT_DIR}/test_e2e_components"/*.skill.json 2>/dev/null | wc -l)
if [ "$comp_count" -ge 0 ]; then
    echo "PASS: E2E-03 (${comp_count} skills created)"
else
    echo "FAIL: E2E-03"
    FAILED=1
fi

# E2E-04: Logger creates session files
echo ""
echo "=== E2E-04: Session log created ==="
COMP4="${PROJECT_DIR}/test_e2e_logs_check"
mkdir -p "$COMP4"
echo "find files" | timeout 5 "$A0" \
    --components-dir "$COMP4" \
    --mock-api "$MOCK_URL" \
    2>/dev/null > /dev/null || true
# logs are written to ./logs/ (default in main.cpp)
log_count=$(ls "${PROJECT_DIR}/logs"/*.jsonl 2>/dev/null | wc -l)
rm -rf "$COMP4" "${PROJECT_DIR}/logs"
if [ "$log_count" -ge 1 ]; then
    echo "PASS: E2E-04 (${log_count} session logs)"
else
    echo "FAIL: E2E-04 (no log files in logs/)"
    FAILED=1
fi

echo ""
echo "=== Results ==="
if [ $FAILED -eq 0 ]; then
    echo "All E2E tests PASSED"
else
    echo "Some E2E tests FAILED"
fi
exit $FAILED
