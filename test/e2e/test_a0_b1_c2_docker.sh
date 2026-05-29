#!/bin/bash
# E2E test: a0 + b1 + c2 with Docker tool execution
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"

A0="${BUILD_DIR}/a0"
B1="${BUILD_DIR}/b1"
C2="${BUILD_DIR}/c2"
SKILLS="${PROJECT_DIR}/test/skills"

PASS=0
FAIL=0

cleanup() {
    echo "=== cleanup ==="
    "$A0" --kill-all 2>/dev/null || true
    pkill -x b1 2>/dev/null || true
    pkill -x c2 2>/dev/null || true
    rm -f /tmp/a0-c2.pid /tmp/a0-c2.sock
    rm -f "$PROJECT_DIR/.a0/b1.pid" "$PROJECT_DIR/.a0/b1.sock"
}

assert_eq() {
    local expected="$1"
    local actual="$2"
    local label="$3"
    if [ "$expected" = "$actual" ]; then
        echo "  PASS: $label"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $label (expected: $expected, got: $actual)"
        FAIL=$((FAIL + 1))
    fi
}

assert_contains() {
    local needle="$1"
    local haystack="$2"
    local label="$3"
    if echo "$haystack" | grep -Fq "$needle"; then
        echo "  PASS: $label"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $label (expected to contain: $needle)"
        FAIL=$((FAIL + 1))
    fi
}

assert_running() {
    local name="$1"
    local label="$2"
    if pgrep -x "$name" >/dev/null 2>&1; then
        echo "  PASS: $label"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $label ($name not running)"
        FAIL=$((FAIL + 1))
    fi
}

assert_not_running() {
    local name="$1"
    local label="$2"
    if pgrep -x "$name" >/dev/null 2>&1; then
        echo "  FAIL: $label ($name still running)"
        FAIL=$((FAIL + 1))
    else
        echo "  PASS: $label"
        PASS=$((PASS + 1))
    fi
}

echo "========================================="
echo "a0 + b1 + c2 E2E Test Suite"
echo "========================================="
date

# Ensure build exists
if [ ! -f "$A0" ] || [ ! -f "$B1" ] || [ ! -f "$C2" ]; then
    echo "Building all targets..."
    cd "$BUILD_DIR"
    cmake "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Release
    make -j"$(nproc)" a0 b1 c2
fi

# Pre-flight: check Docker
if ! docker info >/dev/null 2>&1; then
    echo "SKIPPED: Docker daemon not available"
    exit 0
fi

# Check DeepSeek API key
if [ -z "${DEEPSEEK_API_KEY:-}" ]; then
    echo "SKIPPED: DEEPSEEK_API_KEY not set"
    exit 0
fi

cleanup
trap cleanup EXIT INT TERM

# ======================================================================
echo ""
echo "--- Test 1: a0 runs a skill via --run (inference, no --skill) ---"
# This uses the --run free-form mode which triggers LLM inference
# Since we have components loaded, it will need to infer a skill
OUTPUT=$("$A0" --skills-dir "$SKILLS" --run docker_greeting 2>/dev/null || true)
assert_contains "Hello from Docker" "$OUTPUT" "a0 --run docker_greeting via inference"

# ======================================================================
echo ""
echo "--- Test 2: a0 --skill + --prompt (exact match, skip inference) ---"
OUTPUT=$("$A0" --skills-dir "$SKILLS" --skill docker_greeting --prompt 'E2E' 2>/dev/null || true)
assert_contains "Hello from Docker" "$OUTPUT" "skill docker_greeting executes via --skill"
assert_contains "E2E" "$OUTPUT" "prompt 'E2E' substituted into tool call"

# ======================================================================
echo ""
echo "--- Test 3: a0 auto-launches b1 ---"
# Run a quick skill that triggers b1 launch
"$A0" --skills-dir "$SKILLS" --skill docker_greeting --prompt 'test' >/dev/null 2>&1 &
A0_PID=$!
sleep 2
assert_running "b1" "b1 auto-launched by a0"
wait "$A0_PID" 2>/dev/null || true

# ======================================================================
echo ""
echo "--- Test 4: b1 auto-launches c2 ---"
assert_running "c2" "c2 auto-launched by b1"

# ======================================================================
echo ""
echo "--- Test 5: c2 REST API returns agent stats ---"
sleep 2
STATS=$(curl -sf http://localhost:8080/api/stats 2>/dev/null || echo '{"totalB1s":0}')
assert_contains '"totalB1s":1' "$STATS" "c2 /api/stats shows 1 supervisor"
assert_contains '"totalAgents":1' "$STATS" "c2 /api/stats shows 1 agent"

# ======================================================================
echo ""
echo "--- Test 6: c2 REST API returns status with agent details ---"
STATUS=$(curl -sf http://localhost:8080/api/status 2>/dev/null || echo '[]')
assert_contains '"pid"' "$STATUS" "c2 /api/status contains pid fields"

# ======================================================================
echo ""
echo "--- Test 7: c2 dashboard serves HTML ---"
HTML=$(curl -sf http://localhost:8080/ 2>/dev/null || echo '')
assert_contains "a0 Agent Dashboard" "$HTML" "c2 dashboard HTML served"

# ======================================================================
echo ""
echo "--- Test 8: a0 --no-b1 skips b1 launch ---"
cleanup
OUTPUT=$("$A0" --skills-dir "$SKILLS" --skill docker_greeting --no-b1 --prompt 'no-b1' 2>/dev/null || true)
assert_contains "Hello from Docker" "$OUTPUT" "a0 --no-b1 executes skill correctly"
assert_not_running "b1" "b1 not started with --no-b1"

# ======================================================================
echo ""
echo "--- Test 9: Second a0 reuses same b1 instance ---"
cleanup
"$A0" --skills-dir "$SKILLS" --skill docker_greeting --prompt 'first' >/dev/null 2>&1 &
A0_PID1=$!
sleep 2
assert_running "b1" "first a0 launched b1"

"$A0" --skills-dir "$SKILLS" --skill docker_greeting --prompt 'second' >/dev/null 2>&1 &
A0_PID2=$!
sleep 2

# c2 should show 2 agents
STATS=$(curl -sf http://localhost:8080/api/stats 2>/dev/null || echo '{"totalAgents":0}')
assert_contains '"totalAgents":2' "$STATS" "c2 shows 2 agents from 2 a0 instances"
wait "$A0_PID1" "$A0_PID2" 2>/dev/null || true

# ======================================================================
echo ""
echo "--- Test 10: a0 --kill-all cleans up b1 and c2 ---"
"$A0" --kill-all 2>/dev/null || true
sleep 1
assert_not_running "b1" "b1 killed by --kill-all"
assert_not_running "c2" "c2 killed by --kill-all"

# ======================================================================
echo ""
echo "========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "========================================="

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
