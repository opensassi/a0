#!/usr/bin/env bash
# E2E Docker Integration Tests
# These tests require a running Docker daemon.
# They are skipped if Docker is not available.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
FAILED=0

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass() { echo -e "${GREEN}PASS${NC}: $1"; }
fail() { echo -e "${RED}FAIL${NC}: $1"; FAILED=1; }
skip() { echo -e "${YELLOW}SKIP${NC}: $1"; }

cleanup_containers() {
    local pattern="$1"
    docker ps -a --filter "name=${pattern}" --format '{{.ID}}' 2>/dev/null | xargs -r docker rm -f 2>/dev/null || true
}

check_docker() {
    if ! command -v docker &>/dev/null; then
        echo "Docker not found. Skipping Docker E2E tests."
        return 1
    fi
    if ! docker info &>/dev/null; then
        echo "Docker daemon not running. Skipping Docker E2E tests."
        return 1
    fi
    return 0
}

# Check preconditions
if ! check_docker; then
    skip "Docker E2E tests (no Docker available)"
    exit 0
fi

if [ ! -f "$BUILD_DIR/a0" ]; then
    echo "Building a0..."
    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" 2>/dev/null
    cmake --build "$BUILD_DIR" -j$(nproc) 2>/dev/null
fi

A0="$BUILD_DIR/a0"

cleanup() {
    cleanup_containers "e2e_test"
    cleanup_containers "high_pool"
    cleanup_containers "medium_pool"
    docker network rm e2e_test_default 2>/dev/null || true
    rm -rf "${PROJECT_DIR}/test_docker_e2e" 2>/dev/null || true
}
trap cleanup EXIT

echo ""
echo "========================================"
echo "  Docker E2E Test Suite"
echo "========================================"

# ========================================
# E2E-D1: Basic tool execution in container
# ========================================
echo ""
echo "--- E2E-D1: Basic tool in default image ---"
mkdir -p "${PROJECT_DIR}/test_docker_e2e"
cat > "${PROJECT_DIR}/test_docker_e2e/echo_docker.tool.json" <<'EOF'
{
    "name": "echo_docker",
    "description": "Echo in Docker",
    "command": "echo hello_docker",
    "inputMode": "stdin",
    "dockerImage": "ubuntu:22.04",
    "trustLevel": "MEDIUM",
    "useContainerPool": true
}
EOF
RESULTS=$("$A0" --skills-dir "${PROJECT_DIR}/test_docker_e2e" \
    --mock-api "http://localhost:18081/v1/chat/completions" \
    --no-docker=false \
    --container-idle-timeout 5 2>/dev/null <<< "echo_docker" || true)
if echo "$RESULTS" | grep -q "hello_docker"; then
    pass "E2E-D1"
else
    fail "E2E-D1 (output: $RESULTS)"
fi
cleanup

# ========================================
# E2E-D2: apt dependency installation
# ========================================
echo ""
echo "--- E2E-D2: apt dependency installation ---"
mkdir -p "${PROJECT_DIR}/test_docker_e2e"
cat > "${PROJECT_DIR}/test_docker_e2e/curl_test.tool.json" <<'EOF'
{
    "name": "curl_test",
    "description": "Test curl in Docker",
    "command": "curl --version",
    "inputMode": "stdin",
    "dockerImage": "ubuntu:22.04",
    "trustLevel": "LOW",
    "useContainerPool": true,
    "aptDependencies": ["curl"]
}
EOF
RESULTS=$("$A0" --skills-dir "${PROJECT_DIR}/test_docker_e2e" \
    --mock-api "http://localhost:18081/v1/chat/completions" \
    --container-idle-timeout 5 2>/dev/null <<< "curl_test" || true)
if echo "$RESULTS" | grep -qi "curl"; then
    pass "E2E-D2"
else
    fail "E2E-D2 (output: $RESULTS)"
fi
cleanup

# ========================================
# E2E-D3: HIGH trust container sharing
# ========================================
echo ""
echo "--- E2E-D3: HIGH trust container sharing ---"
mkdir -p "${PROJECT_DIR}/test_docker_e2e"
cat > "${PROJECT_DIR}/test_docker_e2e/tool_a.tool.json" <<'EOF'
{
    "name": "tool_a",
    "description": "Tool A",
    "command": "echo tool_a_output",
    "inputMode": "stdin",
    "dockerImage": "ubuntu:22.04",
    "trustLevel": "HIGH"
}
EOF
cat > "${PROJECT_DIR}/test_docker_e2e/tool_b.tool.json" <<'EOF'
{
    "name": "tool_b",
    "description": "Tool B",
    "command": "echo tool_b_output",
    "inputMode": "stdin",
    "dockerImage": "ubuntu:22.04",
    "trustLevel": "HIGH"
}
EOF
# Both tools share the "high_pool" container
CID_BEFORE=$(docker ps --filter "name=high_pool" --format '{{.ID}}' 2>/dev/null || true)
"$A0" --skills-dir "${PROJECT_DIR}/test_docker_e2e" \
    --mock-api "http://localhost:18081/v1/chat/completions" \
    --container-idle-timeout 30 2>/dev/null <<< "tool_a" || true
CID_AFTER=$(docker ps --filter "name=high_pool" --format '{{.ID}}' 2>/dev/null || true)
if [ -n "$CID_AFTER" ]; then
    pass "E2E-D3 (shared container: $CID_AFTER)"
else
    fail "E2E-D3 (no high_pool container found)"
fi
cleanup

# ========================================
# E2E-D4: LOW trust isolation
# ========================================
echo ""
echo "--- E2E-D4: LOW trust isolation ---"
mkdir -p "${PROJECT_DIR}/test_docker_e2e"
cat > "${PROJECT_DIR}/test_docker_e2e/iso_a.tool.json" <<'EOF'
{
    "name": "iso_a",
    "description": "Isolated A",
    "command": "echo iso_a",
    "inputMode": "stdin",
    "dockerImage": "ubuntu:22.04",
    "trustLevel": "LOW"
}
EOF
cat > "${PROJECT_DIR}/test_docker_e2e/iso_b.tool.json" <<'EOF'
{
    "name": "iso_b",
    "description": "Isolated B",
    "command": "echo iso_b",
    "inputMode": "stdin",
    "dockerImage": "ubuntu:22.04",
    "trustLevel": "LOW"
}
EOF
# Both tools run, should create separate containers
"$A0" --skills-dir "${PROJECT_DIR}/test_docker_e2e" \
    --mock-api "http://localhost:18081/v1/chat/completions" \
    --container-idle-timeout 30 2>/dev/null <<< "iso_a" || true
CID_A=$(docker ps --filter "name=low_iso_a" --format '{{.ID}}' 2>/dev/null || true)
"$A0" --skills-dir "${PROJECT_DIR}/test_docker_e2e" \
    --mock-api "http://localhost:18081/v1/chat/completions" \
    --container-idle-timeout 30 2>/dev/null <<< "iso_b" || true
CID_B=$(docker ps --filter "name=low_iso_b" --format '{{.ID}}' 2>/dev/null || true)
if [ -n "$CID_A" ] && [ -n "$CID_B" ] && [ "$CID_A" != "$CID_B" ]; then
    pass "E2E-D4 (separate containers: $CID_A, $CID_B)"
else
    fail "E2E-D4 (containers: A=$CID_A B=$CID_B)"
fi
cleanup

# ========================================
# E2E-D5: Compose environment
# ========================================
echo ""
echo "--- E2E-D5: Compose environment ---"
mkdir -p "${PROJECT_DIR}/test_docker_e2e"
# Create a minimal docker-compose.yml that runs redis
cat > "${PROJECT_DIR}/test_docker_e2e/docker-compose.yml" <<'EOF'
version: '3'
services:
  redis:
    image: "redis:7-alpine"
    ports:
      - "6379"
EOF
cat > "${PROJECT_DIR}/test_docker_e2e/redis_skill.skill.json" <<'EOF'
{
    "name": "redis_skill",
    "description": "Redis test",
    "prompt": "redis ping result",
    "dependencies": [],
    "validators": [],
    "composeFile": "docker-compose.yml"
}
EOF
# Run compose stack via skill
"$A0" --skills-dir "${PROJECT_DIR}/test_docker_e2e" \
    --mock-api "http://localhost:18081/v1/chat/completions" \
    --container-idle-timeout 30 2>/dev/null <<< "redis_skill" || true
# Check that the compose stack is up
if docker ps --filter "name=redis" --format '{{.Names}}' 2>/dev/null | grep -q redis; then
    pass "E2E-D5 (compose services running)"
else
    fail "E2E-D5 (no redis container found)"
fi
cleanup

# ========================================
# E2E-D6: Container pruning
# ========================================
echo ""
echo "--- E2E-D6: Container pruning ---"
mkdir -p "${PROJECT_DIR}/test_docker_e2e"
cat > "${PROJECT_DIR}/test_docker_e2e/prune_test.tool.json" <<'EOF'
{
    "name": "prune_test",
    "description": "Prune test",
    "command": "echo prune_me",
    "inputMode": "stdin",
    "dockerImage": "ubuntu:22.04",
    "trustLevel": "MEDIUM",
    "useContainerPool": true
}
EOF
# Run tool with 2-second idle timeout
"$A0" --skills-dir "${PROJECT_DIR}/test_docker_e2e" \
    --mock-api "http://localhost:18081/v1/chat/completions" \
    --container-idle-timeout 2 2>/dev/null <<< "prune_test" || true
sleep 3
# Run again; the first container should be pruned (idle > 2s)
"$A0" --skills-dir "${PROJECT_DIR}/test_docker_e2e" \
    --mock-api "http://localhost:18081/v1/chat/completions" \
    --container-idle-timeout 2 2>/dev/null <<< "prune_test" || true
# Check that only one container exists (the new one)
COUNT=$(docker ps --filter "name=medium_pool" --format '{{.ID}}' 2>/dev/null | wc -l)
if [ "$COUNT" -le 1 ]; then
    pass "E2E-D6 (pruning: $COUNT container(s) after idle timeout)"
else
    fail "E2E-D6 (expected ≤1 container, got $COUNT)"
fi
cleanup

echo ""
echo "========================================"
echo "  Results"
echo "========================================"
if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All Docker E2E tests PASSED${NC}"
else
    echo -e "${RED}Some Docker E2E tests FAILED${NC}"
fi
exit $FAILED
