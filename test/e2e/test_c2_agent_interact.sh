#!/bin/bash
# E2E test: c2 agent interaction page — read-only message viewer
#
# Seeds a test SQLite DB, registers a fake b1 with c2 via IPC,
# starts the headed browser, navigates to /agent/:uuid/interact,
# and verifies messages load and display correctly.
#
# Usage: ./test/e2e/test_c2_agent_interact.sh
#   --no-cleanup   leaves daemons running for interactive inspection

set -o pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
C2_BIN="${PROJECT_ROOT}/build/c2"
BRIDGE_SCRIPT="${PROJECT_ROOT}/scripts/playwright-bridge.js"
BRIDGE_PORT=3101
C2_PORT=8081
C2_WEB_ROOT="${PROJECT_ROOT}/c2/web"
NO_CLEANUP="${1:-}"

TEST_DIR="/tmp/test-interact"
TEST_DB="${TEST_DIR}/.a0/db/sessions.db"
TEST_SESSION="test-session-000001"
TEST_B1_PID=9999

C2_LOG=/tmp/c2-agent-interact.log
BRIDGE_LOG=/tmp/bridge_agent_interact.log

PASS_COUNT=0
FAIL_COUNT=0

pass() { echo "  ✓ PASS: $1"; PASS_COUNT=$((PASS_COUNT + 1)); }
fail() { echo "  ✗ FAIL: $1"; FAIL_COUNT=$((FAIL_COUNT + 1)); }

cleanup() {
    if [ -n "${NO_CLEANUP}" ]; then
        echo ""
        echo "=== --no-cleanup: bridge PID=${BRIDGE_PID:-} c2 PID=${C2_PID:-} reg_helper PID=${REG_HELPER_PID:-} ==="
        return
    fi
    echo ""
    echo "=== Cleaning up ==="
    # Kill known PIDs (no pkill — it hangs on zombie entries)
    for pid in "${REG_HELPER_PID:-}" "${BRIDGE_PID:-}" "${C2_PID:-}"; do
        [ -n "$pid" ] && kill -TERM "$pid" 2>/dev/null || true
    done
    sleep 1
    for pid in "${REG_HELPER_PID:-}" "${BRIDGE_PID:-}" "${C2_PID:-}"; do
        [ -n "$pid" ] && kill -KILL "$pid" 2>/dev/null || true
    done
    rm -f /tmp/a0-c2.sock /tmp/a0-c2.pid /tmp/a0-c2.sock.db \
       "${C2_LOG}" "${BRIDGE_LOG}"
    rm -rf "${TEST_DIR}"
    echo "Cleanup done"
}

trap cleanup EXIT

echo "=============================================="
echo "  E2E Test: c2 Agent Interaction Page"
echo "=============================================="
echo ""

# ------------------------------------------------------------------
# Phase 1: Prerequisites
# ------------------------------------------------------------------
echo "=== [1/6] Pre-cleanup and prerequisites ==="

# Pre-clean
pkill -f "playwright-bridge.*3101" 2>/dev/null || true
pkill -f "${C2_BIN}.*8081" 2>/dev/null || true
sleep 1
rm -f /tmp/a0-c2.sock /tmp/a0-c2.pid /tmp/a0-c2.sock.db
rm -rf "${TEST_DIR}"
pass "stale processes cleaned"

if [ ! -f "$C2_BIN" ]; then
    echo "ERROR: c2 binary not found at $C2_BIN"
    exit 1
fi
pass "c2 binary found"

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
# Phase 2: Seed test database
# ------------------------------------------------------------------
echo ""
echo "=== [2/6] Seeding test SQLite database ==="

mkdir -p "${TEST_DIR}/.a0/db"

python3 -c "
import sqlite3, os

db_path = '${TEST_DB}'
os.makedirs(os.path.dirname(db_path), exist_ok=True)
conn = sqlite3.connect(db_path)
c = conn.cursor()

c.executescript('''
CREATE TABLE IF NOT EXISTS agent (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    binary_sha1 TEXT NOT NULL UNIQUE,
    built_at INTEGER
);
INSERT OR IGNORE INTO agent (id, binary_sha1, built_at) VALUES (1, 'test-sha1', 1717000000);

CREATE TABLE IF NOT EXISTS session (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    uuid TEXT NOT NULL UNIQUE,
    agent_id INTEGER NOT NULL REFERENCES agent(id),
    started_at INTEGER NOT NULL
);
INSERT OR IGNORE INTO session (id, uuid, agent_id, started_at) VALUES (1, '${TEST_SESSION}', 1, 1717000000);

CREATE TABLE IF NOT EXISTS message (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL REFERENCES session(id),
    sub_session_id INTEGER,
    seq INTEGER NOT NULL DEFAULT 0,
    role TEXT NOT NULL,
    content TEXT NOT NULL DEFAULT '',
    tool_calls_json TEXT,
    tool_call_id TEXT,
    name TEXT,
    result_json TEXT,
    created_at INTEGER NOT NULL
);

INSERT INTO message (session_id, seq, role, content, created_at) VALUES
(1, 0, 'user',  'What files are in my home directory?', 1716999880),
(1, 1, 'assistant', 'I will check your home directory contents using the list_files tool.', 1716999881),
(1, 2, 'tool', '[{\"path\":\"/home/user\",\"files\":[\"doc1.txt\",\"doc2.txt\",\"projects\",\".bashrc\"]}]', 1716999882),
(1, 3, 'assistant', 'Your home directory contains:\n- doc1.txt\n- doc2.txt\n- projects/\n- .bashrc\n\nWould you like me to examine any of these?', 1716999883),
(1, 4, 'user',  'Show me the contents of doc1.txt', 1716999940);

CREATE INDEX IF NOT EXISTS idx_message_session ON message(session_id, id);
''')

conn.commit()
conn.close()
print('seeded')
"

if [ -f "${TEST_DB}" ]; then
    MSG_COUNT=$(python3 -c "import sqlite3; print(sqlite3.connect('${TEST_DB}').execute('SELECT COUNT(*) FROM message WHERE session_id = 1').fetchone()[0])")
    pass "Test DB seeded at ${TEST_DB} (${MSG_COUNT} messages)"
else
    fail "Failed to create test DB"
    exit 1
fi

# ------------------------------------------------------------------
# Phase 3: Start c2
# ------------------------------------------------------------------
echo ""
echo "=== [3/6] Starting c2 on port ${C2_PORT} ==="

export XDG_RUNTIME_DIR=/tmp
"${C2_BIN}" --port ${C2_PORT} --web-root "${C2_WEB_ROOT}" \
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
# Phase 4: Register fake b1 with c2 via IPC
# ------------------------------------------------------------------
echo ""
echo "=== [4/6] Registering mock b1 with agent ==="

# Use Python to connect to c2 Unix socket, register, update agents, keep alive
python3 -c "
import socket, json, time, sys

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(5)
sock.connect('/tmp/a0-c2.sock')

# Register as b1
reg = json.dumps({'type':'register','pid':${TEST_B1_PID},'wd':'${TEST_DIR}','hostname':'test'})
sock.sendall((reg + '\n').encode())

# Brief pause to let c2 process
time.sleep(0.2)

# Update with agent targeting our test session
agents = json.dumps({
    'type':'update',
    'pid':${TEST_B1_PID},
    'agents':[{'pid':12345,'session':'${TEST_SESSION}','state':'running'}]
})
sock.sendall((agents + '\n').encode())

# Keep connection alive until killed
try:
    while True:
        time.sleep(1)
except:
    sock.close()
" &
REG_HELPER_PID=$!
sleep 1

if kill -0 "${REG_HELPER_PID}" 2>/dev/null; then
    pass "Mock b1 registered with c2"
else
    fail "Mock b1 registration failed"
    exit 1
fi

# Verify c2 sees the agent
AGENT_CHECK=$(curl -sf "http://localhost:${C2_PORT}/api/agent/${TEST_SESSION}" 2>/dev/null)
if echo "${AGENT_CHECK}" | grep -q '"session"'; then
    pass "c2 reports agent registered"
else
    fail "c2 does not see agent — response: ${AGENT_CHECK}"
    exit 1
fi

# ------------------------------------------------------------------
# Phase 5: Start Playwright bridge
# ------------------------------------------------------------------
echo ""
echo "=== [5/6] Starting playwright bridge (headed) ==="

BRIDGE_HEADLESS=false BRIDGE_PORT="${BRIDGE_PORT}" node "${BRIDGE_SCRIPT}" \
    > "${BRIDGE_LOG}" 2>&1 &
BRIDGE_PID=$!
sleep 2

if kill -0 "${BRIDGE_PID}" 2>/dev/null; then
    pass "Bridge started (PID=${BRIDGE_PID})"
else
    fail "Bridge failed to start"
    cat "${BRIDGE_LOG}"
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
        cat "${BRIDGE_LOG}"
        exit 1
    fi
    sleep 1
done

# ------------------------------------------------------------------
# Phase 6: Browser navigation and assertions
# ------------------------------------------------------------------
echo ""
echo "=== [6/6] Opening agent interaction page ==="

BRIDGE="http://localhost:${BRIDGE_PORT}"

bridge() {
    curl -s --max-time 15 "${BRIDGE}" -d "${1}"
}

# Helpers
clickById() { bridge "{\"action\":\"selectById\",\"id\":\"$1\",\"perform\":\"click\"}"; }
typeById() { bridge "{\"action\":\"selectById\",\"id\":\"$1\",\"perform\":\"type\",\"value\":\"$2\"}"; }
inspectById() { bridge "{\"action\":\"selectById\",\"id\":\"$1\"}"; }

# Launch browser
echo "  --- Launching browser ---"
RESULT=$(bridge '{"action":"launch","headless":false,"unsafe_local":true}')
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "Browser launched (headed, host mode)"
else
    fail "Browser launch: $(echo "${RESULT}" | head -c 200)"
    exit 1
fi
sleep 3

# Navigate to agent interaction page
echo "  --- Navigating to /agent/${TEST_SESSION}/interact ---"
RESULT=$(bridge "{\"action\":\"navigate\",\"url\":\"http://localhost:${C2_PORT}/agent/${TEST_SESSION}/interact\",\"timeout\":10}")
if echo "${RESULT}" | grep -q '"ok":true'; then
    TITLE=$(echo "${RESULT}" | grep -o '"title":"[^"]*"' | head -1 || echo "unknown")
    pass "Interact page loaded (${TITLE})"
else
    fail "Navigate: $(echo "${RESULT}" | head -c 200)"
fi

# Wait for async message load to complete
echo "  --- Wait for conversation messages to load ---"
for i in $(seq 1 10); do
    RESULT=$(inspectById conv-status)
    STATUS_TEXT=$(echo "${RESULT}" | grep -o '"text":"[^"]*"' || echo "")
    if echo "${STATUS_TEXT}" | grep -q "messages"; then
        pass "Conversation loaded: ${STATUS_TEXT}"
        break
    fi
    if [ "${i}" -eq 10 ]; then
        fail "Conversation messages never loaded — status: ${STATUS_TEXT}"
    fi
    sleep 1
done

# Snapshot the page
echo "  --- Snapshot page ---"
RESULT=$(bridge '{"action":"snapshot","depth":6}')
if echo "${RESULT}" | grep -q '"ok":true'; then
    SNAP_LEN=$(echo "${RESULT}" | wc -c)
    pass "Page snapshot (${SNAP_LEN} chars)"
else
    fail "Snapshot: $(echo "${RESULT}" | head -c 200)"
fi
sleep 1

# Check that agent title is displayed
echo "  --- Check agent title ---"
RESULT=$(inspectById agent-title)
if echo "${RESULT}" | grep -q '"ok":true'; then
    TEXT=$(echo "${RESULT}" | grep -o '"text":"[^"]*"' || echo "unknown")
    pass "Agent title found: ${TEXT}"
else
    fail "Agent title not found: $(echo "${RESULT}" | head -c 200)"
fi

# Verify conversation shows expected number of messages
echo "  --- Verify message count ---"
RESULT=$(inspectById conv-status)
STATUS_TEXT=$(echo "${RESULT}" | grep -o '"text":"[^"]*"' || echo "0 messages")
MSG_COUNT=$(echo "${STATUS_TEXT}" | grep -o '[0-9]*' || echo "0")
if [ "${MSG_COUNT}" -ge 5 ]; then
    pass "All messages loaded (${MSG_COUNT})"
else
    fail "Expected ≥5 messages, got ${MSG_COUNT}"
fi

# Check message input area exists
echo "  --- Check message input area ---"
RESULT=$(inspectById msg-input)
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "Message input area found"
else
    fail "Message input area not found: $(echo "${RESULT}" | head -c 200)"
fi

# Check send buttons exist
echo "  --- Check send buttons ---"
RESULT=$(inspectById btn-send-queue)
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "Send (next turn) button found"
else
    fail "Send (next turn) button not found: $(echo "${RESULT}" | head -c 200)"
fi

RESULT=$(inspectById btn-send-abort)
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "Send & Interrupt button found"
else
    fail "Send & Interrupt button not found: $(echo "${RESULT}" | head -c 200)"
fi

# Check agent detail fields
echo "  --- Check agent details ---"
RESULT=$(inspectById agent-details)
TEXT=$(echo "${RESULT}" | grep -o '"text":"[^"]*"' || echo "empty")
if echo "${TEXT}" | grep -q "test-session"; then
    pass "Agent details show session info: ${TEXT}"
else
    fail "Agent details missing session — got: ${TEXT}"
fi

# Verify agent info fields via API
echo "  --- Verify agent API fields ---"
RESULT=$(bridge "{\"action\":\"evaluate\",\"function\":\"fetch('/api/agent/${TEST_SESSION}').then(r=>r.json()).then(d=>d.pid+'|'+d.state+'|'+d.hostname+'|'+d.b1Workdir).catch(e=>'err:'+e)\"}")
echo "${RESULT}" | grep -q '"result"' && pass "Agent API returns pid|state|hostname|project" || fail "Agent API error: $(echo "${RESULT}" | head -c 200)"

# Take screenshot
echo "  --- Take screenshot ---"
RESULT=$(bridge "{\"action\":\"take_screenshot\",\"type\":\"png\",\"filename\":\"/tmp/c2-agent-interact.png\"}")
if echo "${RESULT}" | grep -q '"ok":true'; then
    pass "Screenshot saved to /tmp/c2-agent-interact.png"
else
    fail "Screenshot: $(echo "${RESULT}" | head -c 200)"
fi

# Close browser
echo "  --- Close browser ---"
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
echo "=== Results ==="
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
