#!/usr/bin/env bash
# E2E test: System tool chaining (glob, bash, grep, read)
# Builds a standalone test binary and runs it against the real codebase.
# No LLM API key needed — tools execute in-process via expandPrompt().
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"

# Ensure build exists
if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/liba0_lib.a" ]; then
    echo "Building a0_lib..."
    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" 2>/dev/null
    cmake --build "$BUILD_DIR" -j"$(nproc)" --target a0_lib 2>/dev/null
fi

# Find nlohmann_json include path
JSON_INCLUDE=$(find "$BUILD_DIR/_deps" -path "*/nlohmann_json-src/include" 2>/dev/null | head -1)
if [ -z "$JSON_INCLUDE" ]; then
    # Try system path
    JSON_INCLUDE=""
fi

echo "=== Building system tools E2E test ==="
g++ -std=c++17 \
    -I"$PROJECT_DIR" \
    -I"$PROJECT_DIR/src" \
    ${JSON_INCLUDE:+-I"$JSON_INCLUDE"} \
    "$SCRIPT_DIR/test_system_tools_e2e.cpp" \
    "$BUILD_DIR/liba0_lib.a" \
    "$BUILD_DIR/src/persistence/libpersistence_lib.a" \
    "$BUILD_DIR/libcmd_runner_lib.a" \
    "$BUILD_DIR/libipc_lib.a" \
    -o /tmp/test_system_tools_e2e \
    -lcurl -lsqlite3 -ldl

echo ""
echo "=== Running system tools E2E test ==="
A0_ROOT="$PROJECT_DIR" /tmp/test_system_tools_e2e

RESULT=$?
rm -f /tmp/test_system_tools_e2e

if [ $RESULT -eq 0 ]; then
    echo ""
    echo "All system tools E2E tests PASSED"
else
    echo ""
    echo "Some system tools E2E tests FAILED"
fi
exit $RESULT
