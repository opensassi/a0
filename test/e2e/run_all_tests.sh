#!/bin/bash
# run_all_tests.sh — Full test suite for the a0 project.
#
# Runs all four test layers in order:
#   1. Unit tests (ctest / Google Test, C++)
#   2. Agent E2E tests (mock DeepSeek API, no browser)
#   3. c2 dashboard E2E tests (headed browser, Playwright)
#   4. TUI E2E tests (mock DeepSeek + --test-mode)
#
# Exits with 0 if all pass, 1 if any fail.

set -o pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

PASS_COUNT=0
FAIL_COUNT=0
FAILED_SUITES=""

pass_suite() {
    echo "  ✓ ALL PASSED"
    PASS_COUNT=$((PASS_COUNT + 1))
}
fail_suite() {
    echo "  ✗ FAILED"
    FAIL_COUNT=$((FAIL_COUNT + 1))
    FAILED_SUITES="${FAILED_SUITES}  - $1\n"
}

echo "=============================================="
echo "  a0 Full Test Suite"
echo "=============================================="
echo ""

# ------------------------------------------------------------------
# Phase 1: Unit tests
# ------------------------------------------------------------------
echo "=== [1/4] Unit tests (ctest) ==="
echo ""

if [ ! -d "${BUILD_DIR}" ]; then
    echo "  Build directory not found at ${BUILD_DIR}"
    echo "  Run: cmake -B build && cmake --build build"
    fail_suite "unit"
else
    if ctest --test-dir "${BUILD_DIR}" --output-on-failure 2>&1; then
        pass_suite
    else
        fail_suite "unit"
    fi
fi

echo ""

# ------------------------------------------------------------------
# Phase 2: Agent E2E tests
# ------------------------------------------------------------------
echo "=== [2/4] Agent E2E tests (mock DeepSeek) ==="
echo ""

if bash "${PROJECT_ROOT}/test/e2e/run_e2e_tests.sh" 2>&1; then
    pass_suite
else
    fail_suite "agent-e2e"
fi

echo ""

# ------------------------------------------------------------------
# Phase 3: c2 dashboard E2E tests (headed browser)
# ------------------------------------------------------------------
echo "=== [3/4] c2 dashboard E2E tests (headed browser) ==="
echo ""

echo "  --- c2 dashboard (29 assertions) ---"
if bash "${PROJECT_ROOT}/test/e2e/test_c2_dashboard_e2e.sh" 2>&1; then
    echo ""
    echo "  --- c2 agent interact (25 assertions) ---"
    if bash "${PROJECT_ROOT}/test/e2e/test_c2_agent_interact.sh" 2>&1; then
        pass_suite
    else
        fail_suite "c2-agent-interact-e2e"
    fi
else
    fail_suite "c2-dashboard-e2e"
fi

echo ""

# ------------------------------------------------------------------
# Phase 4: TUI E2E tests (mock DeepSeek + --test-mode)
# ------------------------------------------------------------------
echo "=== [4/4] TUI E2E tests (mock DeepSeek + --test-mode) ==="
echo ""

cd "${PROJECT_ROOT}"
if python3 -m pytest test/e2e/test_tui_e2e.py -v 2>&1; then
    pass_suite
else
    fail_suite "tui-e2e"
fi

echo ""

# ------------------------------------------------------------------
# Results
# ------------------------------------------------------------------
echo "=============================================="
echo "  Results"
echo "=============================================="
echo ""
echo "  Suites passed: ${PASS_COUNT}"
echo "  Suites failed: ${FAIL_COUNT}"
echo ""

if [ -n "${FAILED_SUITES}" ]; then
    echo "  Failed suites:"
    printf "${FAILED_SUITES}"
    echo ""
    echo "  SOME TESTS FAILED"
    exit 1
else
    echo "  ALL TESTS PASSED"
    exit 0
fi
