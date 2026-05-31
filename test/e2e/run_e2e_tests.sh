#!/usr/bin/env bash
# E2E tests for a0 agent (mock DeepSeek API, no real credentials needed)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
MOCK_PORT=18081
MOCK_URL="http://127.0.0.1:${MOCK_PORT}/v1/chat/completions"
TEST_PID=$$

cleanup() {
    if [ -n "${MOCK_PID:-}" ]; then
        kill "$MOCK_PID" 2>/dev/null || true
        wait "$MOCK_PID" 2>/dev/null || true
    fi
    rm -rf /tmp/a0_e2e_${TEST_PID}_*
}
trap cleanup EXIT

# Start clean: remove any leftover test dirs
rm -rf /tmp/a0_e2e_${TEST_PID}_*

FAILED=0

# Helper: unique temp dir for a0 data (one per test to avoid SQLite collisions)
a0dir() {
    echo "/tmp/a0_e2e_${TEST_PID}_$1"
}

# Helper: create a skill component directory with a skill.json manifest
create_component() {
    local base_dir="$1"
    local ns="$2"
    local component="$3"
    local manifest="$4"
    local dir="${base_dir}/${ns}/${component}"
    mkdir -p "$dir"
    echo "$manifest" > "${dir}/skill.json"
}

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

# ======================================================================
echo ""
echo "=== E2E-01: Empty input ==="
A0_DIR=$(a0dir "e2e01")
output=$(echo "" | timeout 5 "$A0" --a0-dir "$A0_DIR" --no-b1 2>/dev/null || true)
rm -rf "$A0_DIR"
if echo "$output" | grep -q "no goal provided"; then
    echo "PASS: E2E-01"
else
    echo "FAIL: E2E-01 (got: $output)"
    FAILED=1
fi

# ======================================================================
echo ""
echo "=== E2E-02: Goal triggers tool inference ==="
N2_DIR="${PROJECT_DIR}/test_e2e_n2"
rm -rf "$N2_DIR"
create_component "$N2_DIR" "local" "tools" '{
    "name": "tools",
    "version": "1.0.0",
    "tools": [{
        "name": "bash",
        "description": "bash",
        "command": "bash",
        "inputMode": "stdin",
        "systemTool": true
    }],
    "prompts": []
}'
A0_DIR=$(a0dir "e2e02")
echo "count lines in file" | timeout 5 "$A0" \
    --skills-dir "$N2_DIR" \
    --mock-api "$MOCK_URL" \
    --no-b1 \
    --a0-dir "$A0_DIR" \
    2>/dev/null > /tmp/e2e_out.txt || true
rm -rf "$N2_DIR" "$A0_DIR"
output=$(cat /tmp/e2e_out.txt)
if [ -n "$output" ] && [ "$output" != "\"no goal provided\"" ]; then
    echo "PASS: E2E-02 (output: $output)"
else
    echo "FAIL: E2E-02 (output: $output)"
    FAILED=1
fi

# ======================================================================
echo ""
echo "=== E2E-03: N/A (flat file skills removed) ==="
    echo "SKIP: E2E-03 (skill inference no longer writes flat .skill.json files)"

# ======================================================================
echo ""
echo "=== E2E-04: Session persisted to SQLite ==="
N4_DIR="${PROJECT_DIR}/test_e2e_n4"
rm -rf "$N4_DIR"
A0_DIR=$(a0dir "e2e04")
echo "find files" | timeout 5 "$A0" \
    --skills-dir "$N4_DIR" \
    --mock-api "$MOCK_URL" \
    --no-b1 \
    --a0-dir "$A0_DIR" \
    2>/dev/null > /dev/null || true
rm -rf "$N4_DIR"
# Verify session data was written to SQLite
row_count=$(sqlite3 "$A0_DIR/db/sessions.db" \
    "SELECT COUNT(*) FROM message WHERE session_id = (
        SELECT id FROM session ORDER BY id DESC LIMIT 1
    )" 2>/dev/null) || row_count=0
rm -rf "$A0_DIR"
if [ "$row_count" -ge 1 ]; then
    echo "PASS: E2E-04 (${row_count} messages in SQLite)"
else
    echo "FAIL: E2E-04 (no messages in SQLite)"
    FAILED=1
fi

# ======================================================================
# NEGATIVE E2E TESTS
# ======================================================================

echo ""
echo "=== E2E-N1: Skill with missing dependency ==="
N1_DIR="${PROJECT_DIR}/test_e2e_n1"
rm -rf "$N1_DIR"
create_component "$N1_DIR" "local" "n1" '{
    "name": "n1",
    "version": "1.0.0",
    "tools": [],
    "prompts": [{
        "name": "bad_skill",
        "description": "skill with missing dep",
        "prompt": "do something",
        "dependencies": ["nonexistent_tool"],
        "validators": []
    }]
}'
A0_DIR=$(a0dir "e2en1")
echo "bad_skill" | timeout 5 "$A0" \
    --skills-dir "$N1_DIR" \
    --mock-api "$MOCK_URL" \
    --no-b1 \
    --a0-dir "$A0_DIR" \
    2>/dev/null > /tmp/e2e_n1.out || true
rm -rf "$N1_DIR" "$A0_DIR"
if grep -q "Missing dependencies" /tmp/e2e_n1.out; then
    echo "PASS: E2E-N1"
else
    echo "FAIL: E2E-N1 (got: $(cat /tmp/e2e_n1.out))"
    FAILED=1
fi

# ======================================================================
echo ""
echo "=== E2E-N2: Tool timeout ==="
N2_DIR="${PROJECT_DIR}/test_e2e_n2"
rm -rf "$N2_DIR"
create_component "$N2_DIR" "local" "n2" '{
    "name": "n2",
    "version": "1.0.0",
    "tools": [{
        "name": "sleep_tool",
        "description": "sleeps",
        "command": "sleep 100",
        "inputMode": "stdin",
        "timeoutSecs": 3
    }],
    "prompts": [{
        "name": "sleep_skill",
        "description": "sleep skill",
        "prompt": "{{tool:sleep_tool _=\"\"}}",
        "dependencies": ["local:n2:sleep_tool"],
        "validators": []
    }]
}'
A0_DIR=$(a0dir "e2en2")
start=$(date +%s)
echo "sleep_skill" | timeout 40 "$A0" \
    --skills-dir "$N2_DIR" \
    --mock-api "$MOCK_URL" \
    --no-b1 \
    --a0-dir "$A0_DIR" \
    2>/tmp/e2e_n2_stderr.txt > /tmp/e2e_n2.out || true
end=$(date +%s)
elapsed=$((end - start))
rm -rf "$N2_DIR" "$A0_DIR"
if grep -q "ERROR: timeout" /tmp/e2e_n2_stderr.txt && [ $elapsed -lt 10 ]; then
    echo "PASS: E2E-N2 (${elapsed}s)"
else
    echo "FAIL: E2E-N2 (elapsed: ${elapsed}s, stderr has timeout: $(grep -c 'ERROR: timeout' /tmp/e2e_n2_stderr.txt 2>/dev/null))"
    head -5 /tmp/e2e_n2_stderr.txt 2>/dev/null
    FAILED=1
fi

# ======================================================================
echo ""
echo "=== E2E-N3: Parameter substitution ==="
N3_DIR="${PROJECT_DIR}/test_e2e_n3"
rm -rf "$N3_DIR"
create_component "$N3_DIR" "local" "n3" '{
    "name": "n3",
    "version": "1.0.0",
    "tools": [],
    "prompts": [{
        "name": "param_skill",
        "description": "echo goal",
        "prompt": "Process: {{goal}}",
        "dependencies": [],
        "validators": []
    }]
}'
A0_DIR=$(a0dir "e2en3")
echo "param_skill" | timeout 10 "$A0" \
    --skills-dir "$N3_DIR" \
    --mock-api "$MOCK_URL" \
    --no-b1 \
    --a0-dir "$A0_DIR" \
    2>/tmp/e2e_n3_stderr.txt > /tmp/e2e_n3_out.txt || true
rm -rf "$N3_DIR" "$A0_DIR"
if grep -q "Process: param_skill" /tmp/e2e_n3_stderr.txt 2>/dev/null; then
    echo "PASS: E2E-N3"
else
    echo "FAIL: E2E-N3 (stderr did not contain 'Process: param_skill')"
    echo "stderr: $(head -20 /tmp/e2e_n3_stderr.txt 2>/dev/null)"
    FAILED=1
fi

# ======================================================================
echo ""
echo "=== E2E-N4: Args mode ==="
N4_DIR="${PROJECT_DIR}/test_e2e_n4"
rm -rf "$N4_DIR"
create_component "$N4_DIR" "local" "n4" '{
    "name": "n4",
    "version": "1.0.0",
    "tools": [{
        "name": "echo_arg",
        "description": "echo first positional arg",
        "command": "sh -c '\''echo $1'\'' _",
        "inputMode": "args"
    }],
    "prompts": [{
        "name": "args_skill",
        "description": "args demo",
        "prompt": "{{tool:echo_arg _=\"hello_args\"}}",
        "dependencies": ["local:n4:echo_arg"],
        "validators": []
    }]
}'
A0_DIR=$(a0dir "e2en4")
echo "args_skill" | timeout 5 "$A0" \
    --skills-dir "$N4_DIR" \
    --mock-api "$MOCK_URL" \
    --no-b1 \
    --a0-dir "$A0_DIR" \
    2>/tmp/e2e_n4_stderr.txt > /tmp/e2e_n4.out || true
rm -rf "$N4_DIR" "$A0_DIR"
if grep -q "hello_args" /tmp/e2e_n4_stderr.txt; then
    echo "PASS: E2E-N4"
else
    echo "FAIL: E2E-N4 (stderr: $(head -30 /tmp/e2e_n4_stderr.txt 2>/dev/null))"
    FAILED=1
fi

# ======================================================================
echo ""
echo "=== Results ==="
if [ $FAILED -eq 0 ]; then
    echo "All E2E tests PASSED"
else
    echo "Some E2E tests FAILED"
fi
exit $FAILED
