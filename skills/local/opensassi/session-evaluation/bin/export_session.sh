#!/usr/bin/env bash
# export_session.sh — Bundle a session log as .json.bz2 + .sha256 in sessions/
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
LOGS_DIR="$PROJECT_DIR/logs"
SESSIONS_DIR="$PROJECT_DIR/sessions"
LOG_FILE="$LOGS_DIR/$SESSION_ID.jsonl"
BASE_FILE="$SESSIONS_DIR/${TITLE_SLUG}-${SESSION_ID}"

if [ ! -f "$LOG_FILE" ]; then
  echo "ERROR: session log not found: $LOG_FILE"
  exit 1
fi

mkdir -p "$SESSIONS_DIR"

JSON_FILE="${BASE_FILE}.json"
echo '[' > "$JSON_FILE"
FIRST=true
while IFS= read -r LINE; do
  if [ "$FIRST" = true ]; then FIRST=false; else echo ',' >> "$JSON_FILE"; fi
  echo "$LINE" >> "$JSON_FILE"
done < "$LOG_FILE"
echo ']' >> "$JSON_FILE"

RAW_SIZE=$(wc -c < "$JSON_FILE")

echo "=> Computing content hash..."
sha256sum "$JSON_FILE" | cut -d' ' -f1 > "${BASE_FILE}.sha256"
echo "   Hash: $(cat "${BASE_FILE}.sha256")"

echo "=> Compressing with bzip2 (max)..."
bzip2 -9 "$JSON_FILE"
COMP_SIZE=$(wc -c < "${BASE_FILE}.json.bz2")
PCT=$(( (RAW_SIZE - COMP_SIZE) * 100 / RAW_SIZE ))
echo "   ${RAW_SIZE} -> ${COMP_SIZE} bytes (${PCT}% saved)"

echo "=> Done: ${BASE_FILE}.json.bz2"
