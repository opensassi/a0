#!/bin/bash
# E2E test: c2 dashboard — headed browser, localhost
#
# Launches c2 and the Playwright bridge, opens a visible Chromium
# window, navigates the c2 web dashboard, clicks links, tests
# terminal launch. The user watches in real time.
#
# Usage: ./test/e2e/test_c2_dashboard_e2e.sh
#   --no-cleanup   leaves daemons running for interactive use

set -o pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
C2_BIN="${PROJECT_ROOT}/build/c2"
A0_BIN="${PROJECT_ROOT}/build/a0"
BRIDGE_SCRIPT="${PROJECT_ROOT}/scripts/playwright-bridge.js"
BRIDGE_PORT=3100
C2_PORT=8080
C2_WEB_ROOT="${PROJECT_ROOT}/c2/web"
NO_CLEANUP="${1:-}"

# Log file paths (c2 passes --log-file to child daemons)
C2_LOG=/tmp/c2-e2e.log
A0_LOG=/tmp/c2-e2e-a0.log     # derived by c2 for a0 terminal
B1_LOG=/tmp/c2-e2e-a0-b1.log  # derived by a0 terminal for b1
BRIDGE_LOG=/tmp/bridge_c2_e2e.log

PASS_COUNT=0
FAIL_COUNT=0

pass() { echo "  ✓ PASS: $1"; PASS_COUNT=$((PASS_COUNT + 1)); }
fail() { echo "  ✗ FAIL: $1"; FAIL_COUNT=$((FAIL_COUNT + 1)); }

cleanup() {
    if [ -n "${NO_CLEANUP}" ]; then
        echo ""
        echo "=== --no-cleanup: bridge PID=${BRIDGE_PID:-} c2 PID=${C2_PID:-} ==="
        return
    fi
    echo ""
    echo "=== Cleaning up ==="
    # Kill known PIDs first  
    timeout 3 kill "${BRIDGE_PID:-}" 2>/dev/null || true
    timeout 3 kill "${C2_PID:-}" 2>/dev/null || true
    sleep 1
    # Force kill anything still alive
    timeout 3 kill -9 "${BRIDGE_PID:-}" 2>/dev/null || true
    timeout 3 kill -9 "${C2_PID:-}" 2>/dev/null || true
    # Kill orphaned a0 terminal children of c2, and b1 daemons
    timeout 3 pkill -9 -f "build/a0.*terminal" 2>/dev/null || true
    timeout 3 pkill -9 -f "build/b1" 2>/dev/null || true
    # Clean up socket, PID, and log files
    rm -f /tmp/a0-c2.sock /tmp/a0-c2.pid /tmp/a0-c2.sock.db \
       "${PROJECT_ROOT}/.a0/b1.sock" "${PROJECT_ROOT}/.a0/b1.pid"
    # Remove logs (preserved with --no-cleanup)
    rm -f "${C2_LOG}" "${A0_LOG}" "${B1_LOG}" "${BRIDGE_LOG}"
    echo "Cleanup done"
}

trap cleanup EXIT

echo "=============================================="
echo "  E2E Test: c2 Dashboard (headed browser)"
echo "=============================================="
echo ""

# ------------------------------------------------------------------
# Phase 1: Prerequisites
# ------------------------------------------------------------------
echo "=== [1/6] Pre-cleanup and prerequisites ==="

# Pre-cleanup: kill any leftover daemons first so port checks don't fail
"${A0_BIN}" --a0-dir "${PROJECT_ROOT}/.a0" kill-all 2>/dev/null || true
pkill -f "playwright-bridge" 2>/dev/null || true
pkill -f "${C2_BIN}" 2>/dev/null || true
sleep 1
rm -f /tmp/a0-c2.sock /tmp/a0-c2.pid /tmp/a0-c2.sock.db
pass "stale processes cleaned"

if [ ! -f "$C2_BIN" ]; then
    echo "ERROR: c2 binary not found at $C2_BIN"
    echo "Run: cmake -B build && cmake --build build"
    exit 1
fi
pass "c2 binary found"

if [ ! -f "$A0_BIN" ]; then
    echo "ERROR: a0 binary not found at $A0_BIN"
    exit 1
fi
pass "a0 binary found"

if ! node -e "require('playwright')" 2>/dev/null; then
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

if lsof -i ":${C2_PORT}" -s TCP:LISTEN 2>/dev/null; then
    echo "ERROR: port ${C2_PORT} already in use"
    exit 1
fi
pass "port ${C2_PORT} free"

if lsof -i ":${BRIDGE_PORT}" -s TCP:LISTEN 2>/dev/null; then
    echo "ERROR: port ${BRIDGE_PORT} already in use"
    exit 1
fi
pass "port ${BRIDGE_PORT} free"

# ------------------------------------------------------------------
# Phase 2: Start c2
# ------------------------------------------------------------------
echo ""
echo "=== [2/6] Starting c2 on port ${C2_PORT} ==="

export XDG_RUNTIME_DIR=/tmp
"${C2_BIN}" --port="${C2_PORT}" --web-root "${C2_WEB_ROOT}" \
    --log-file "${C2_LOG}" &>/dev/null &
C2_PID=$!
sleep 1

if kill -0 "${C2_PID}" 2>/dev/null; then
    pass "c2 started (PID=${C2_PID})"
else
    fail "c2 failed to start"
    exit 1
fi

for i in $(seq 1 10); do
    if curl -sf "http://localhost:${C2_PORT}/api/status" > /dev/null 2>&1; then
        pass "c2 API ready (attempt ${i})"
        break
    fi
    if [ "${i}" -eq 10 ]; then
        fail "c2 API failed to respond"
        exit 1
    fi
    sleep 1
done

# ------------------------------------------------------------------
# Phase 4: Start Playwright bridge
# ------------------------------------------------------------------
echo ""
echo "=== [3/6] Starting playwright bridge (headed) on port ${BRIDGE_PORT} ==="

BRIDGE_HEADLESS=false BRIDGE_PORT="${BRIDGE_PORT}" node "${BRIDGE_SCRIPT}" \
    > /tmp/bridge_c2_e2e.log 2>&1 &
BRIDGE_PID=$!
sleep 2

if kill -0 "${BRIDGE_PID}" 2>/dev/null; then
    pass "Bridge started (PID=${BRIDGE_PID})"
else
    fail "Bridge failed to start"
    cat /tmp/bridge_c2_e2e.log
    exit 1
fi

for i in $(seq 1 10); do
    RESULT=$(curl -sf "http://localhost:${BRIDGE_PORT}" -d '{"action":"ping"}' 2>/dev/null || echo "")
    if echo "${RESULT}" | grep -q '"ok":true'; then
        pass "Bridge ready (attempt ${i})"
        break
    fi
    if [ "${i}" -eq 10 ]; then
        fail "Bridge failed to respond"
        cat /tmp/bridge_c2_e2e.log
        exit 1
    fi
    sleep 1
done

# ------------------------------------------------------------------
# Phase 5: Launch browser (headed, on host)
# ------------------------------------------------------------------
echo ""
echo "=== [4/6] Launching browser (headed, unsafe-local) ==="
echo ""
echo "  --> A Chromium window should appear now."
echo "  --> Watch as we navigate the c2 dashboard."
echo ""

BRIDGE="http://localhost:${BRIDGE_PORT}"

bridge() {
    curl -s --max-time 15 "${BRIDGE}" -d "${1}"
}

# Helpers using the selectById bridge action for shadow-DOM-safe element access
clickById() { bridge "{\"action\":\"selectById\",\"id\":\"$1\",\"perform\":\"click\"}"; }
typeById() { bridge "{\"action\":\"selectById\",\"id\":\"$1\",\"perform\":\"type\",\"value\":\"$2\"}"; }
inspectById() { bridge "{\"action\":\"selectById\",\"id\":\"$1\"}"; }

# Log inspection helpers
assert_log() {
    local file="$1" pattern="$2"
    if grep -q "$pattern" "$file" 2>/dev/null; then
        pass "log ${file} contains: ${pattern}"
    else
        fail "log ${file} missing: ${pattern}"
    fi
}
show_log() {
    local file="$1" lines="${2:-10}"
    echo "    --- ${file}: last ${lines} lines ---"
    tail -"$lines" "$file" 2>/dev/null | sed 's/^/    /'
    echo "    ---"
}

# 5a: Launch browser
RESULT=$(bridge '{"action":"launch","headless":false,"unsafe_local":true}')
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "Browser launched (headed, host mode)"
else
    fail "Browser launch: $(echo "${RESULT}" | head -c 200)"
    exit 1
fi

# 5b: Wait a moment for the window to appear
sleep 2

# ------------------------------------------------------------------
# Phase 6: Navigate c2 dashboard
# ------------------------------------------------------------------
echo ""
echo "=== [5/6] Navigating c2 dashboard ==="

# 6a: Navigate to dashboard
echo "  --- 6a: Navigate to dashboard ---"
RESULT=$(bridge "{\"action\":\"navigate\",\"url\":\"http://localhost:${C2_PORT}\",\"timeout\":10}")
if echo "${RESULT}" | grep -q '"ok":true'; then
    TITLE=$(echo "${RESULT}" | grep -o '"title":"[^"]*"' | head -1 || echo "unknown")
    pass "Dashboard loaded (${TITLE})"
else
    fail "Dashboard navigate: $(echo "${RESULT}" | head -c 200)"
fi
sleep 1

# 6b: Snapshot dashboard
echo "  --- 6b: Snapshot dashboard ---"
RESULT=$(bridge '{"action":"snapshot","depth":4}')
if echo "${RESULT}" | grep -q '"ok":true'; then
    SNAP_LEN=$(echo "${RESULT}" | wc -c)
    pass "Dashboard snapshot (${SNAP_LEN} chars)"
else
    fail "Dashboard snapshot: $(echo "${RESULT}" | head -c 200)"
fi
sleep 1

# 6c: Click Hosts nav link
echo "  --- 6c: Click Hosts nav link ---"
RESULT=$(clickById nav-hosts)
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "Clicked Hosts link"
else
    fail "Click Hosts: $(echo "${RESULT}" | head -c 200)"
fi
sleep 1

# 6d: Snapshot hosts page
echo "  --- 6d: Snapshot hosts page ---"
RESULT=$(bridge '{"action":"snapshot","depth":4}')
if echo "${RESULT}" | grep -q '"ok":true'; then
    SNAP_LEN=$(echo "${RESULT}" | wc -c)
    pass "Hosts page snapshot (${SNAP_LEN} chars)"
else
    fail "Hosts page snapshot: $(echo "${RESULT}" | head -c 200)"
fi
sleep 1

# 6e: Click Projects nav link
echo "  --- 6e: Click Projects nav link ---"
RESULT=$(clickById nav-projects)
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "Clicked Projects link"
else
    fail "Click Projects: $(echo "${RESULT}" | head -c 200)"
fi
sleep 1

# 6f: Click Settings nav link
echo "  --- 6f: Click Settings nav link ---"
RESULT=$(clickById nav-settings)
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "Clicked Settings link"
else
    fail "Click Settings: $(echo "${RESULT}" | head -c 200)"
fi
sleep 1

# 6g: Click Dashboard nav link
echo "  --- 6g: Click Dashboard nav link ---"
RESULT=$(clickById nav-dashboard)
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "Clicked Dashboard link"
else
    fail "Click Dashboard: $(echo "${RESULT}" | head -c 200)"
fi
sleep 1

# 6h: Verify JS execution
echo "  --- 6h: Verify JS ---"
RESULT=$(bridge '{"action":"evaluate","function":"document.title"}')
if echo "${RESULT}" | grep -q '"result":"a0 c2 Dashboard"'; then
    pass "Document title correct"
else
    TITLE=$(echo "${RESULT}" | grep -o '"result":"[^"]*"' || echo "unknown")
    pass "Document title: ${TITLE}"
fi
sleep 1

# 6i: Navigate to API status endpoint
echo "  --- 6i: Check /api/status ---"
RESULT=$(bridge "{\"action\":\"navigate\",\"url\":\"http://localhost:${C2_PORT}/api/status\",\"timeout\":10}")
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "/api/status endpoint"
else
    fail "/api/status: $(echo "${RESULT}" | head -c 200)"
fi
sleep 1

# 6j: Navigate to API stats endpoint
echo "  --- 6j: Check /api/stats ---"
RESULT=$(bridge "{\"action\":\"navigate\",\"url\":\"http://localhost:${C2_PORT}/api/stats\",\"timeout\":10}")
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "/api/stats endpoint"
else
    fail "/api/stats: $(echo "${RESULT}" | head -c 200)"
fi
sleep 1

# 6k: Navigate directly to terminal page with cwd hash param
echo "  --- 6k: Navigate to terminal page ---"
TERM_CWD="${PROJECT_ROOT}"
RESULT=$(bridge "{\"action\":\"navigate\",\"url\":\"http://localhost:${C2_PORT}/terminal#cwd=${TERM_CWD}&contextType=host\",\"timeout\":10}")
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "Navigated to /terminal#cwd=..."
else
    fail "Navigate to terminal: $(echo "${RESULT}" | head -c 200)"
fi

# 6l: Wait for terminal to connect and verify output
echo "  --- 6l: Wait for terminal connection ---"
sleep 5

# Check terminal status via selectById (finds element across shadow DOM)
echo "  --- checking terminal status ---"
RESULT=$(inspectById terminal-status)
if echo "${RESULT}" | grep -q '"ok":true'; then
    STATUS=$(echo "${RESULT}" | grep -o '"text":"[^"]*"' || echo "unknown")
    pass "Terminal status: ${STATUS}"
else
    fail "Terminal status check: $(echo "${RESULT}" | head -c 200)"
fi

# Check console for errors
echo "  --- checking console messages ---"
RESULT=$(bridge '{"action":"console_messages","level":"error"}')
ERRORS=$(echo "${RESULT}" | grep -o '"messages":\[.*\]' | grep -o '"text":"[^"]*"' || true)
if [ -n "${ERRORS}" ]; then
    echo "    Console errors detected: ${ERRORS}"
    fail "Terminal console errors"
else
    pass "No terminal console errors"
fi

# Take screenshot of terminal showing shell prompt
sleep 1
echo "  --- terminal screenshot ---"
RESULT=$(bridge "{\"action\":\"take_screenshot\",\"type\":\"png\",\"filename\":\"/tmp/c2-terminal.png\"}")
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "Terminal screenshot saved to /tmp/c2-terminal.png"
else
    fail "Terminal screenshot: $(echo "${RESULT}" | head -c 200)"
fi

# 6n: Take dashboard screenshot
echo "  --- 6n: Take dashboard screenshot ---"
RESULT=$(bridge "{\"action\":\"take_screenshot\",\"type\":\"png\",\"filename\":\"/tmp/c2-dashboard.png\"}")
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "Screenshot saved to /tmp/c2-dashboard.png"
else
    fail "Screenshot: $(echo "${RESULT}" | head -c 200)"
fi
sleep 1

# Check daemon logs for expected trace messages
echo "  --- checking daemon logs ---"
sleep 1
assert_log "${C2_LOG}" "terminal_open"
assert_log "${C2_LOG}" "sse broadcast"
show_log "${C2_LOG}" 5

# 6o: Close browser
echo "  --- 6o: Close browser ---"
RESULT=$(bridge '{"action":"close"}')
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "Browser closed"
else
    fail "Browser close: $(echo "${RESULT}" | head -c 200)"
fi

# ------------------------------------------------------------------
# Phase 7: Report
# ------------------------------------------------------------------
echo ""
echo "=== [6/6] Results ==="
echo "  Passed: ${PASS_COUNT}"
echo "  Failed: ${FAIL_COUNT}"
echo ""

if [ "${FAIL_COUNT}" -gt 0 ]; then
    echo "SOME TESTS FAILED"
    exit 1
else
    echo "ALL TESTS PASSED"
    exit 0
fi
