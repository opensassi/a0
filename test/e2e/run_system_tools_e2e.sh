#!/usr/bin/env bash
# E2E test: System tool chaining (glob, bash, grep, read)
# Now a proper CMake/GTest test — run via ctest or directly:
#   ctest --test-dir build -R test_system_tools_e2e
#   build/test_system_tools_e2e
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"

echo "=== Building & running system tools E2E test ==="
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DENABLE_COVERAGE=ON 2>/dev/null
cmake --build "$BUILD_DIR" -j"$(nproc)" --target test_system_tools_e2e 2>/dev/null

cd "$PROJECT_DIR"
"$BUILD_DIR/test_system_tools_e2e"
RESULT=$?

if [ $RESULT -eq 0 ]; then
    echo "All system tools E2E tests PASSED"
else
    echo "Some system tools E2E tests FAILED"
fi
exit $RESULT
