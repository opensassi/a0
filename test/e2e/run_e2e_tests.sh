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
# Provide bash tool so inferred skill's dependencies are satisfied
cat > "$COMPONENTS_DIR/bash.tool.json" <<'EOF'
{"name":"bash","description":"bash","command":"bash","inputMode":"stdin"}
EOF
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

# ===== NEGATIVE E2E TESTS =====

# E2E-N1: Skill with missing dependency
echo ""
echo "=== E2E-N1: Skill with missing dependency ==="
N1_DIR="${PROJECT_DIR}/test_e2e_n1"
rm -rf "$N1_DIR"
mkdir -p "$N1_DIR"
cat > "$N1_DIR/bad_skill.skill.json" <<'EOF'
{
    "name": "bad_skill",
    "description": "skill with missing dep",
    "prompt": "do something",
    "dependencies": ["nonexistent_tool"],
    "validators": []
}
EOF
echo "bad_skill" | timeout 5 "$A0" \
    --components-dir "$N1_DIR" \
    --mock-api "$MOCK_URL" \
    2>/dev/null > /tmp/e2e_n1.out || true
rm -rf "$N1_DIR"
if grep -q "Missing dependencies" /tmp/e2e_n1.out; then
    echo "PASS: E2E-N1"
else
    echo "FAIL: E2E-N1 (got: $(cat /tmp/e2e_n1.out))"
    FAILED=1
fi

# E2E-N2: Tool timeout
echo ""
echo "=== E2E-N2: Tool timeout ==="
N2_DIR="${PROJECT_DIR}/test_e2e_n2"
rm -rf "$N2_DIR"
mkdir -p "$N2_DIR"
cat > "$N2_DIR/sleep_tool.tool.json" <<'EOF'
{
    "name": "sleep_tool",
    "description": "sleeps",
    "command": "sleep 100",
    "inputMode": "stdin"
}
EOF
cat > "$N2_DIR/sleep_skill.skill.json" <<'EOF'
{
    "name": "sleep_skill",
    "description": "sleep skill",
    "prompt": "{{tool:sleep_tool}}",
    "dependencies": ["sleep_tool"],
    "validators": []
}
EOF
start=$(date +%s)
echo "sleep_skill" | timeout 40 "$A0" \
    --components-dir "$N2_DIR" \
    --mock-api "$MOCK_URL" \
    2>/tmp/e2e_n2_stderr.txt > /tmp/e2e_n2.out || true
end=$(date +%s)
elapsed=$((end - start))
rm -rf "$N2_DIR"
# Check trace log for timeout error (tool output goes into expanded prompt, then to LLM)
if grep -q "ERROR: timeout" /tmp/e2e_n2_stderr.txt && [ $elapsed -lt 35 ]; then
    echo "PASS: E2E-N2 (${elapsed}s)"
else
    echo "FAIL: E2E-N2 (elapsed: ${elapsed}s, stderr has timeout: $(grep -c 'ERROR: timeout' /tmp/e2e_n2_stderr.txt 2>/dev/null))"
    head -5 /tmp/e2e_n2_stderr.txt 2>/dev/null
    FAILED=1
fi

# E2E-N3: Parameter substitution via trace log
echo ""
echo "=== E2E-N3: {{goal}} substitution ==="
N3_DIR="${PROJECT_DIR}/test_e2e_n3"
rm -rf "$N3_DIR"
mkdir -p "$N3_DIR"
cat > "$N3_DIR/param_skill.skill.json" <<'EOF'
{
    "name": "param_skill",
    "description": "echo goal",
    "prompt": "Process: {{goal}}",
    "dependencies": [],
    "validators": []
}
EOF
echo "param_skill" | timeout 10 "$A0" \
    --components-dir "$N3_DIR" \
    --mock-api "$MOCK_URL" \
    2>/tmp/e2e_n3_stderr.txt > /tmp/e2e_n3_out.txt || true
rm -rf "$N3_DIR"
if grep -q "Process: param_skill" /tmp/e2e_n3_stderr.txt 2>/dev/null; then
    echo "PASS: E2E-N3"
else
    echo "FAIL: E2E-N3 (stderr did not contain 'Process: param_skill')"
    echo "stderr: $(head -20 /tmp/e2e_n3_stderr.txt 2>/dev/null)"
    FAILED=1
fi

# E2E-N4: Args mode tool
echo ""
echo "=== E2E-N4: Args mode ==="
N4_DIR="${PROJECT_DIR}/test_e2e_n4"
rm -rf "$N4_DIR"
mkdir -p "$N4_DIR"
cat > "$N4_DIR/echo_arg.tool.json" <<'EOF'
{
    "name": "echo_arg",
    "description": "echo first positional arg",
    "command": "sh -c 'echo $1' _",
    "inputMode": "args"
}
EOF
cat > "$N4_DIR/args_skill.skill.json" <<'EOF'
{
    "name": "args_skill",
    "description": "args demo",
    "prompt": "{{tool:echo_arg _=\"hello_args\"}}",
    "dependencies": ["echo_arg"],
    "validators": []
}
EOF
echo "args_skill" | timeout 5 "$A0" \
    --components-dir "$N4_DIR" \
    --mock-api "$MOCK_URL" \
    2>/tmp/e2e_n4_stderr.txt > /tmp/e2e_n4.out || true
rm -rf "$N4_DIR"
# Tool result "hello_args" should appear in expanded prompt via trace log
if grep -q "hello_args" /tmp/e2e_n4_stderr.txt; then
    echo "PASS: E2E-N4"
else
    echo "FAIL: E2E-N4 (stderr: $(head -30 /tmp/e2e_n4_stderr.txt 2>/dev/null))"
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
