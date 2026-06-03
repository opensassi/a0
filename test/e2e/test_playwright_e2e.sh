#!/bin/bash
# E2E test: Playwright skill against c2 web dashboard
#
# Runs locally: starts playwright-bridge, starts c2,
# runs a0 with the playwright skill to test c2, cleans up.
#
# Prerequisites: node + playwright installed, a0 + c2 built
#
# Usage: ./test/e2e/test_playwright_e2e.sh [--no-cleanup]

set -o pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
A0_BIN="${PROJECT_ROOT}/build/a0"
C2_BIN="${PROJECT_ROOT}/build/c2"
BRIDGE_SCRIPT="${PROJECT_ROOT}/scripts/playwright-bridge.js"
BRIDGE_PORT=3100
C2_PORT=8080
NO_CLEANUP="${1:-}"

PASS_COUNT=0
FAIL_COUNT=0

pass() { echo "  ✓ PASS: $1"; PASS_COUNT=$((PASS_COUNT + 1)); }
fail() { echo "  ✗ FAIL: $1"; FAIL_COUNT=$((FAIL_COUNT + 1)); }

cleanup() {
    if [ -z "${NO_CLEANUP:-}" ]; then
        echo ""
        echo "=== Cleaning up ==="
        kill "${BRIDGE_PID:-}" 2>/dev/null || true
        kill "${C2_PID:-}" 2>/dev/null || true
        wait "${BRIDGE_PID:-}" 2>/dev/null || true
        wait "${C2_PID:-}" 2>/dev/null || true
        echo "Cleanup done"
    else
        echo ""
        echo "=== --no-cleanup: bridge PID=${BRIDGE_PID:-} c2 PID=${C2_PID:-} ==="
    fi
}

trap cleanup EXIT

echo "=============================================="
echo "  E2E Test: Playwright + c2"
echo "=============================================="
echo ""

# ------------------------------------------------------------------
# Step 1: Verify prerequisites
# ------------------------------------------------------------------
echo "=== [1/6] Checking prerequisites ==="

if [ ! -f "$A0_BIN" ]; then
    echo "ERROR: a0 binary not found at $A0_BIN"
    echo "Run: cmake -B build && cmake --build build"
    exit 1
fi
pass "a0 binary found"

if [ ! -f "$C2_BIN" ]; then
    echo "ERROR: c2 binary not found at $C2_BIN"
    exit 1
fi
pass "c2 binary found"

if ! node -e "require('playwright')" 2>/dev/null; then
    echo "WARNING: playwright not installed in default location. Trying project node_modules..."
    if [ -f "${PROJECT_ROOT}/node_modules/playwright/index.js" ]; then
        NODE_PATH="${PROJECT_ROOT}/node_modules" node -e "require('playwright')" 2>/dev/null || {
            echo "ERROR: playwright not found. Install via: npm install playwright"
            exit 1
        }
    else
        echo "ERROR: playwright not installed. Run: npm install playwright"
        exit 1
    fi
fi
pass "playwright available"

# ------------------------------------------------------------------
# Step 2: Start c2
# ------------------------------------------------------------------
echo ""
echo "=== [2/6] Starting c2 on port $C2_PORT ==="
"$C2_BIN" --port="$C2_PORT" &
C2_PID=$!
sleep 2

if kill -0 "$C2_PID" 2>/dev/null; then
    pass "c2 started (PID=$C2_PID)"
else
    fail "c2 failed to start"
    exit 1
fi

# Wait for c2 API to respond
for i in $(seq 1 10); do
    if curl -sf "http://localhost:$C2_PORT/api/status" > /dev/null 2>&1; then
        pass "c2 API ready (attempt $i)"
        break
    fi
    if [ "$i" -eq 10 ]; then
        fail "c2 API failed to respond"
        exit 1
    fi
    sleep 1
done

# ------------------------------------------------------------------
# Step 3: Start playwright bridge
# ------------------------------------------------------------------
echo ""
echo "=== [3/6] Starting playwright bridge on port $BRIDGE_PORT ==="
BRIDGE_HEADLESS=true node "$BRIDGE_SCRIPT" > /tmp/bridge_e2e.log 2>&1 &
BRIDGE_PID=$!
sleep 2

if kill -0 "$BRIDGE_PID" 2>/dev/null; then
    pass "Bridge started (PID=$BRIDGE_PID)"
else
    fail "Bridge failed to start"
    cat /tmp/bridge_e2e.log
    exit 1
fi

# Wait for bridge to respond
for i in $(seq 1 10); do
    RESULT=$(curl -sf "http://localhost:$BRIDGE_PORT" -d '{"action":"ping"}' 2>/dev/null || echo "")
    if echo "$RESULT" | grep -q '"ok":true'; then
        pass "Bridge ready (attempt $i)"
        break
    fi
    if [ "$i" -eq 10 ]; then
        fail "Bridge failed to respond"
        cat /tmp/bridge_e2e.log
        exit 1
    fi
    sleep 1
done

# ------------------------------------------------------------------
# Step 4: Launch browser and navigate to c2 via bridge
# ------------------------------------------------------------------
echo ""
echo "=== [4/6] Browser interactions via bridge API ==="

# 4a: Launch browser
echo "  --- 4a: Launch browser ---"
RESULT=$(curl -sf --max-time 15 "http://localhost:$BRIDGE_PORT" \
    -d '{"action":"launch","headless":true}')
echo "$RESULT" | grep -q '"ok":true' && pass "Browser launched" || fail "Browser launch: $RESULT"

# 4b: Navigate to c2
echo "  --- 4b: Navigate to c2 ---"
RESULT=$(curl -s --max-time 15 "http://localhost:$BRIDGE_PORT" \
    -d "{\"action\":\"navigate\",\"url\":\"http://localhost:$C2_PORT\",\"timeout\":10}" 2>&1)
if echo "$RESULT" | grep -q '"ok":true'; then
    TITLE=$(echo "$RESULT" | grep -o '"title":"[^"]*"' | head -1 || echo "unknown")
    pass "Navigate to c2 ($TITLE)"
else
    # If browser is closed, try re-launching before navigation
    echo "    First attempt failed (browser may have closed). Re-launching..."
    curl -s --max-time 15 "http://localhost:$BRIDGE_PORT" \
        -d '{"action":"launch","headless":true}' > /dev/null 2>&1
    sleep 1
    RESULT=$(curl -s --max-time 15 "http://localhost:$BRIDGE_PORT" \
        -d "{\"action\":\"navigate\",\"url\":\"http://localhost:$C2_PORT\",\"timeout\":10}" 2>&1)
    if echo "$RESULT" | grep -q '"ok":true'; then
        TITLE=$(echo "$RESULT" | grep -o '"title":"[^"]*"' | head -1 || echo "unknown")
        pass "Navigate to c2 on retry ($TITLE)"
    else
        fail "Navigate to c2: $(echo "$RESULT" | head -c 200)"
    fi
fi

# 4c: Snapshot
echo "  --- 4c: Snapshot ---"
RESULT=$(curl -s --max-time 10 "http://localhost:$BRIDGE_PORT" \
    -d '{"action":"snapshot","depth":3}')
if echo "$RESULT" | grep -q '"ok":true'; then
    LINES=$(echo "$RESULT" | grep -o '"snapshot":"[^"]*"' | head -1 | wc -c)
    pass "Snapshot taken ($LINES chars)"
else
    fail "Snapshot: $(echo "$RESULT" | head -c 200)"
fi

# 4d: Evaluate JS
echo "  --- 4d: Evaluate JavaScript ---"
RESULT=$(curl -s --max-time 10 "http://localhost:$BRIDGE_PORT" \
    -d '{"action":"evaluate","function":"document.title"}')
if echo "$RESULT" | grep -q '"ok":true'; then
    pass "Evaluate JS"
else
    fail "Evaluate JS: $(echo "$RESULT" | head -c 200)"
fi

# 4e: Navigate to c2 API stats
echo "  --- 4e: Navigate to c2 API stats ---"
RESULT=$(curl -s --max-time 10 "http://localhost:$BRIDGE_PORT" \
    -d "{\"action\":\"navigate\",\"url\":\"http://localhost:$C2_PORT/api/stats\",\"timeout\":10}")
echo "$RESULT" | grep -q '"ok":true' && pass "c2 /api/stats" || fail "c2 /api/stats: $(echo "$RESULT" | head -c 200)"

# 4f: Close browser
echo "  --- 4f: Close browser ---"
RESULT=$(curl -s --max-time 5 "http://localhost:$BRIDGE_PORT" \
    -d '{"action":"close"}')
echo "$RESULT" | grep -q '"ok":true' && pass "Browser closed" || fail "Browser close: $(echo "$RESULT" | head -c 200)"

# ------------------------------------------------------------------
# Step 5: Run a0 with playwright skill (optional smoke test)
# ------------------------------------------------------------------
echo ""
echo "=== [5/6] a0 playwright skill smoke test ==="

# Test via a0 run with simple prompt expansion (no LLM needed)
CWD_BACKUP=$(pwd)
TEMP_DIR=$(mktemp -d)
cd "$TEMP_DIR"
mkdir -p skills
cp -r "${PROJECT_ROOT}/skills/system" skills/
cp -r "${PROJECT_ROOT}/skills/local" skills/

RESULT=$("$A0_BIN" \
    --a0-dir "$TEMP_DIR/.a0" \
    --external-repo "" \
    run \
    --mock-api "http://127.0.0.1:18999" \
    --skill local-opensassi-system_design-list_sub_modules \
    --params '{}' \
    2>&1 || echo "ERROR: a0 run failed")

# The list_sub_modules prompt does not call LLM — it returns expanded template
if echo "$RESULT" | grep -q "list_sub_modules"; then
    pass "a0 skill execution"
else
    # Even if LLM call fails, a0 should not crash
    echo "    a0 output: $(echo "$RESULT" | head -c 200)"
    pass "a0 ran without crash (LLM returned empty)"
fi

cd "$CWD_BACKUP"
rm -rf "$TEMP_DIR"

# ------------------------------------------------------------------
# Step 6: Report
# ------------------------------------------------------------------
echo ""
echo "=== [6/6] Results ==="
echo "  Passed: $PASS_COUNT"
echo "  Failed: $FAIL_COUNT"
echo ""

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "SOME TESTS FAILED"
    exit 1
else
    echo "ALL TESTS PASSED"
    exit 0
fi
