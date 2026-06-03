#!/bin/bash
# browser.sh — CLI shim for Playwright Bridge command tools.
# Invoked by the tool runner with inputMode: "stdin".
# First argument is the Playwright action name (e.g. "navigate", "click").
# Reads JSON from stdin, adds the action, POSTs to the bridge.
#
# Usage: browser.sh navigate < /tmp/params.json
#   where /tmp/params.json = {"url":"http://..."}

set -euo pipefail

ACTION="${1:?Usage: browser.sh <action>}"
BRIDGE_PORT="${BRIDGE_PORT:-3100}"
BRIDGE_URL="http://localhost:${BRIDGE_PORT}"

# Read stdin JSON and merge with action
BODY=$(jq -c --arg action "$ACTION" '{action: $action, args: .}' /dev/stdin 2>/dev/null || echo '{"action":"'"$ACTION"'","args":{}}')

# POST to bridge
RESPONSE=$(curl -s -X POST "$BRIDGE_URL" -d "$BODY" --max-time 120 2>/dev/null)

# Check for curl failure
if [ $? -ne 0 ]; then
  echo "ERROR: failed to connect to Playwright bridge at $BRIDGE_URL"
  exit 1
fi

# Check for error in response
ERROR=$(echo "$RESPONSE" | jq -r '.error // empty' 2>/dev/null)
if [ -n "$ERROR" ]; then
  echo "ERROR: $ERROR"
  exit 1
fi

# Print the full response JSON
echo "$RESPONSE" | jq -c '.' 2>/dev/null || echo "$RESPONSE"
