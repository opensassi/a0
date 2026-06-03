#!/usr/bin/env bash
# E2E test for the local:github skill.
#
# Creates a real GitHub project for testing, runs a0 with the github skill
# loaded, exercises the full issue + project board workflow via the agent's
# forked tool-calling loop, then cleans up.
#
# Requires:
#   - gh CLI authenticated (gh auth status)
#   - a0 binary built (build/a0)
#
# Usage:
#   bash test/e2e/test_github_skill.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
A0="$PROJECT_DIR/build/a0"
MOCK_PORT=18083
MOCK_URL="http://127.0.0.1:${MOCK_PORT}/v1/chat/completions"
TEST_PID=$$
A0_DIR="/tmp/a0_gh_e2e_${TEST_PID}"
MOCK_LOG="/tmp/a0_gh_e2e_mock_${TEST_PID}.log"
OUT_FILE="/tmp/a0_gh_e2e_out_${TEST_PID}.txt"
TITLE_PREFIX="e2e-gh-skill-test-${TEST_PID}"

PASS_COUNT=0
FAIL_COUNT=0

pass() { PASS_COUNT=$((PASS_COUNT + 1)); echo "  PASS: $1"; }
fail() { FAIL_COUNT=$((FAIL_COUNT + 1)); echo "  FAIL: $1"; }

# Ensure a0 binary exists
if [ ! -f "$A0" ]; then
    echo "Building a0..."
    cmake -S "$PROJECT_DIR" -B "$PROJECT_DIR/build" 2>/dev/null
    cmake --build "$PROJECT_DIR/build" -j"$(nproc)" 2>/dev/null
fi

# -------------------------------------------------------------------
# Setup: gh auth check
# -------------------------------------------------------------------
echo "=== Setup ==="
if ! gh auth status 2>&1 > /dev/null; then
    echo "FAIL: gh is not authenticated. Run 'gh auth login' first."
    exit 1
fi
echo "  gh auth: OK"

# -------------------------------------------------------------------
# Setup: create test project
# -------------------------------------------------------------------
PROJECT_TITLE="a0-gh-skill-e2e-${TEST_PID}"
echo "  Creating test project: ${PROJECT_TITLE}"

PROJECT_JSON=$(gh project create --owner opensassi --title "$PROJECT_TITLE" --format json 2>/dev/null)
PROJECT_NUM=$(echo "$PROJECT_JSON" | jq -r '.number')
PROJECT_ID=$(echo "$PROJECT_JSON" | jq -r '.id')
echo "  Project #${PROJECT_NUM} ID: ${PROJECT_ID}"

# Create Phase field (required by the workflow)
PHASE_RESULT=$(gh api graphql -f query="mutation { createProjectV2Field(input: { projectId: \"${PROJECT_ID}\" dataType: SINGLE_SELECT name: \"Phase\" singleSelectOptions: [{name:\"Open Source\" color:BLUE description:\"\"}, {name:\"Cloud Beta\" color:GREEN description:\"\"}, {name:\"Enterprise\" color:YELLOW description:\"\"}] }) { projectV2Field { ... on ProjectV2SingleSelectField { id name options { id name } } } } }" --jq '.data.createProjectV2Field.projectV2Field' 2>/dev/null)
PHASE_FIELD_ID=$(echo "$PHASE_RESULT" | jq -r '.id')
PHASE_OS_ID=$(echo "$PHASE_RESULT" | jq -r '.options[] | select(.name=="Open Source") | .id')
PHASE_CB_ID=$(echo "$PHASE_RESULT" | jq -r '.options[] | select(.name=="Cloud Beta") | .id')
PHASE_ENT_ID=$(echo "$PHASE_RESULT" | jq -r '.options[] | select(.name=="Enterprise") | .id')

# Create Size field
SIZE_RESULT=$(gh api graphql -f query="mutation { createProjectV2Field(input: { projectId: \"${PROJECT_ID}\" dataType: SINGLE_SELECT name: \"Size\" singleSelectOptions: [{name:\"XS\" color:GRAY description:\"\"}, {name:\"S\" color:GRAY description:\"\"}, {name:\"M\" color:GRAY description:\"\"}, {name:\"L\" color:GRAY description:\"\"}, {name:\"XL\" color:GRAY description:\"\"}] }) { projectV2Field { ... on ProjectV2SingleSelectField { id name options { id name } } } } }" --jq '.data.createProjectV2Field.projectV2Field' 2>/dev/null)
SIZE_FIELD_ID=$(echo "$SIZE_RESULT" | jq -r '.id')
SIZE_S_ID=$(echo "$SIZE_RESULT" | jq -r '.options[] | select(.name=="S") | .id')

# Status field is auto-created with the project. Query its ID.
STATUS_RESULT=$(gh api graphql -f query="query { node(id: \"${PROJECT_ID}\") { ... on ProjectV2 { fields(first: 20) { nodes { ... on ProjectV2SingleSelectField { id name options { id name } } } } } } }" --jq '.data.node.fields.nodes[] | select(.name=="Status")' 2>/dev/null)
STATUS_FIELD_ID=$(echo "$STATUS_RESULT" | jq -r '.id')

echo "  Fields created: Phase=${PHASE_FIELD_ID} Status=${STATUS_FIELD_ID} Size=${SIZE_FIELD_ID}"

# Build mock config JSON
MOCK_CONFIG=$(cat <<EOF
{
    "title": "${TITLE_PREFIX}",
    "labels": "",
    "body": "E2E test issue for github skill",
    "milestone": "Open Source",
    "project_id": "${PROJECT_ID}",
    "phase_field_id": "${PHASE_FIELD_ID}",
    "phase_option_id": "${PHASE_OS_ID}",
    "status_field_id": "${STATUS_FIELD_ID}",
    "size_field_id": "${SIZE_FIELD_ID}",
    "size_option_id": "${SIZE_S_ID}"
}
EOF
)

# -------------------------------------------------------------------
# Setup: start mock server
# -------------------------------------------------------------------
echo "  Starting mock server on port ${MOCK_PORT}..."
python3 "$SCRIPT_DIR/mock_github_e2e.py" "$MOCK_PORT" "$MOCK_CONFIG" > "$MOCK_LOG" 2>&1 &
MOCK_PID=$!
sleep 1

if ! kill -0 "$MOCK_PID" 2>/dev/null; then
    echo "FAIL: Mock server failed to start"
    exit 1
fi
echo "  Mock PID: ${MOCK_PID}"

# -------------------------------------------------------------------
# Cleanup trap
# -------------------------------------------------------------------
cleanup() {
    echo ""
    echo "=== Cleanup ==="

    # Kill mock server
    [ -n "${MOCK_PID:-}" ] && kill "$MOCK_PID" 2>/dev/null || true

    # Find and tag test issues (project closing cleans up board items)
    for num in $(gh issue list --repo opensassi/a0 --state all --limit 30 --json number,title --jq ".[] | select(.title | startswith(\"${TITLE_PREFIX}\")) | .number" 2>/dev/null); do
        gh issue edit "$num" --repo opensassi/a0 --add-label "e2e-test" 2>/dev/null || true
        echo "  Tagged issue #${num} (closed by project cleanup)"
    done

    # Close test project
    if [ -n "${PROJECT_NUM:-}" ]; then
        gh project close "$PROJECT_NUM" --owner opensassi 2>/dev/null
        echo "  Closed project #${PROJECT_NUM}"
    fi

    # Clean up temp files
    rm -rf "$A0_DIR" 2>/dev/null || true
    rm -f "$MOCK_LOG" "$OUT_FILE" 2>/dev/null || true
}
trap cleanup EXIT

# -------------------------------------------------------------------
# Run a0
# -------------------------------------------------------------------
echo ""
echo "=== Running a0 with github skill ==="
mkdir -p "$A0_DIR"

GOAL="Create a new issue in opensassi/a0 called '${TITLE_PREFIX}'. Body: 'E2E test issue for github skill'. Set milestone to 'Open Source', phase to 'Open Source', size to 'S', and status to backlog."

echo "  Goal: Create issue '${TITLE_PREFIX}'..."
echo "  Starting a0 (timeout 120s)..."

# Run a0 with the goal via stdin REPL mode
echo "$GOAL" | timeout 120 "$A0" \
    --a0-dir "$A0_DIR" \
    --mock-api "$MOCK_URL" \
    --no-b1 \
    --skills-dir "$PROJECT_DIR/skills" \
    2>/dev/null > "$OUT_FILE" || true

A0_EXIT=$?
echo "  a0 exit code: ${A0_EXIT}"

# -------------------------------------------------------------------
# Verify results
# -------------------------------------------------------------------
echo ""
echo "=== Verification ==="

# T1: a0 produced some output
if [ -s "$OUT_FILE" ]; then
    pass "a0 produced output (${A0_EXIT})"
else
    fail "a0 produced no output"
fi

# T2: Find created issue
ISSUE_NUM=$(gh issue list --repo opensassi/a0 --state all --limit 30 --json number,title --jq ".[] | select(.title == \"${TITLE_PREFIX}\") | .number" 2>/dev/null | head -1)
if [ -n "$ISSUE_NUM" ]; then
    pass "Issue #${ISSUE_NUM} was created with title '${TITLE_PREFIX}'"
else
    fail "No issue found with title '${TITLE_PREFIX}'"
    # Still test the mock / project cleanup but skip remaining issue-specific checks
fi

if [ -n "${ISSUE_NUM:-}" ]; then
    # T3: Milestone is set
    MILESTONE=$(gh issue view "$ISSUE_NUM" --repo opensassi/a0 --json milestone --jq '.milestone.title' 2>/dev/null || echo "")
    if [ "$MILESTONE" = "Open Source" ]; then
        pass "Milestone is 'Open Source'"
    else
        fail "Milestone is '${MILESTONE}' (expected 'Open Source')"
    fi

    # T4: Issue is on the project board with correct fields
    # Get the issue's node ID, then find it on the project board
    ISSUE_NODE=$(gh api graphql -f query="query { repository(owner:\"opensassi\", name:\"a0\") { issue(number:${ISSUE_NUM}) { id } } }" --jq '.data.repository.issue.id' 2>/dev/null || echo "")

    if [ -n "$ISSUE_NODE" ]; then
        # Find the project item for this issue
        BOARD_QUERY="query { node(id: \"${PROJECT_ID}\") { ... on ProjectV2 { items(first: 20) { nodes { id content { ... on Issue { id } } fieldValues(first: 10) { nodes { ... on ProjectV2ItemFieldSingleSelectValue { name field { ... on ProjectV2SingleSelectField { name } } } } } } } } } }"
        BOARD_DATA=$(gh api graphql -f query="$BOARD_QUERY" --jq '.data.node.items.nodes[] | select(.content.id == "'"${ISSUE_NODE}"'")' 2>/dev/null || echo "")

        if [ -n "$BOARD_DATA" ]; then
            pass "Issue is present on the project board"

            # Check Phase field
            PHASE_VAL=$(echo "$BOARD_DATA" | jq -r '.fieldValues.nodes[] | select(.field.name=="Phase") | .name' 2>/dev/null || echo "")
            if [ "$PHASE_VAL" = "Open Source" ]; then
                pass "Phase field = 'Open Source'"
            else
                fail "Phase field = '${PHASE_VAL}' (expected 'Open Source')"
            fi

            # Check Status field
            STATUS_VAL=$(echo "$BOARD_DATA" | jq -r '.fieldValues.nodes[] | select(.field.name=="Status") | .name' 2>/dev/null || echo "")
            if [ "$STATUS_VAL" = "Todo" ]; then
                pass "Status field = 'Todo' (Backlog)"
            else
                fail "Status field = '${STATUS_VAL}' (expected 'Todo')"
            fi

            # Check Size field
            SIZE_VAL=$(echo "$BOARD_DATA" | jq -r '.fieldValues.nodes[] | select(.field.name=="Size") | .name' 2>/dev/null || echo "")
            if [ "$SIZE_VAL" = "S" ]; then
                pass "Size field = 'S'"
            else
                fail "Size field = '${SIZE_VAL}' (expected 'S')"
            fi
        else
            fail "Issue not found on the project board"
        fi
    else
        fail "Could not resolve issue node ID"
    fi
fi

# T6: Mock server processed all 7 turns
MOCK_TURNS=$(grep -c "turn=" "$MOCK_LOG" 2>/dev/null || echo "0")
if [ "$MOCK_TURNS" -ge 3 ]; then
    pass "Mock server processed ${MOCK_TURNS} turns (>=3)"
else
    fail "Mock server only processed ${MOCK_TURNS} turns (expected >=3)"
fi

# -------------------------------------------------------------------
# Results
# -------------------------------------------------------------------
echo ""
echo "Results: ${PASS_COUNT} passed, ${FAIL_COUNT} failed"
if [ "$FAIL_COUNT" -gt 0 ]; then
    exit 1
fi
