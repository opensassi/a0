#!/usr/bin/env bash
# export_session.sh — Export a session via a0 CLI as .jsonl.gz + .sha256 in sessions/
set -euo pipefail

TITLE_SLUG=""
SESSION_ID=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --slug=*) TITLE_SLUG="${1#*=}"; shift ;;
    --session=*) SESSION_ID="${1#*=}"; shift ;;
    *) echo "Unknown argument: $1"; exit 1 ;;
  esac
done

if [ -z "$TITLE_SLUG" ] || [ -z "$SESSION_ID" ]; then
  echo "Usage: export_session.sh --slug=<title-slug> --session=<session-id>"
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SKILL_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_DIR="$(cd "$SKILL_DIR/../../../.." && pwd)"
SESSIONS_DIR="$PROJECT_DIR/sessions"
BASE_FILE="$SESSIONS_DIR/${TITLE_SLUG}-${SESSION_ID}"
A0_BIN="${PROJECT_DIR}/build/a0"

mkdir -p "$SESSIONS_DIR"

JSONL_FILE="${BASE_FILE}.jsonl"
"$A0_BIN" --a0-dir "$PROJECT_DIR/.a0" session export \
    --session-id "$SESSION_ID" --output "$JSONL_FILE"

RAW_SIZE=$(wc -c < "$JSONL_FILE")

echo "=> Computing content hash..."
sha256sum "$JSONL_FILE" | cut -d' ' -f1 > "${BASE_FILE}.sha256"
echo "   Hash: $(cat "${BASE_FILE}.sha256")"

echo "=> Compressing with gzip (max)..."
gzip -9 "$JSONL_FILE"
COMP_SIZE=$(wc -c < "${BASE_FILE}.jsonl.gz")
PCT=$(( (RAW_SIZE - COMP_SIZE) * 100 / RAW_SIZE ))
echo "   ${RAW_SIZE} -> ${COMP_SIZE} bytes (${PCT}% saved)"

echo "=> Done: ${BASE_FILE}.jsonl.gz"
