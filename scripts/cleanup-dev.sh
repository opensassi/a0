#!/usr/bin/env bash
# scripts/cleanup-dev.sh — Kill orphaned dev processes (no a0 dependency)
set -euo pipefail

SCRIPT_PID=$$
MY_PPID=$(ps -o ppid= -p $$ 2>/dev/null | tr -d ' ')

# 1. Kill by exact binary path (won't match harness/node processes)
pkill -f "^.*/build/a0[ \t]" 2>/dev/null || true
pkill -f "^.*/build/b1[ \t]" 2>/dev/null || true
pkill -f "^.*/build/c2[ \t]" 2>/dev/null || true
pkill -f "mock_deepseek"       2>/dev/null || true

# 2. Read PID files from .a0/ (handles daemons not spawned by a0)
for f in .a0/b1.pid .a0/a0-c2.pid; do
  [ -f "$f" ] && { kill "$(cat "$f")" 2>/dev/null; rm -f "$f"; } || true
done

# 3. Unlink stale sockets and temp e2e dirs
rm -f .a0/b1.sock .a0/b1.sock.lock
find /tmp -maxdepth 2 -name "a0-e2e-*" -type d -exec rm -rf {} + 2>/dev/null || true

# 4. Hard-kill survivors
sleep 0.3
for sig in TERM KILL; do
  pkill "-$sig" -f "^.*/build/a0[ \t]" 2>/dev/null || true
  pkill "-$sig" -f "^.*/build/b1[ \t]" 2>/dev/null || true
  pkill "-$sig" -f "^.*/build/c2[ \t]" 2>/dev/null || true
  pkill "-$sig" -f "mock_deepseek"     2>/dev/null || true
done

echo "cleanup-dev: done"
