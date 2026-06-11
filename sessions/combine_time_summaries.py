#!/usr/bin/env python3
"""Combine time estimate and actual stats summaries into a single file.

Reads time-estimates-summary.json and actual-times-summary.json,
extracts their .summary blocks, and writes time-summary-combined.json.
"""
import os, sys, json
from datetime import datetime, timezone

SESSIONS_DIR = os.path.dirname(os.path.abspath(__file__))

ESTIMATES_PATH = os.path.join(SESSIONS_DIR, "time-estimates-summary.json")
ACTUAL_PATH = os.path.join(SESSIONS_DIR, "actual-times-summary.json")
OUTPUT_PATH = os.path.join(SESSIONS_DIR, "time-summary-combined.json")

for path, label in [(ESTIMATES_PATH, "Estimates"), (ACTUAL_PATH, "Actual")]:
    if not os.path.exists(path):
        print(f"Missing: {path}", file=sys.stderr)
        sys.exit(1)

with open(ESTIMATES_PATH) as f:
    estimates = json.load(f)
with open(ACTUAL_PATH) as f:
    actual = json.load(f)

combined = {
    "generated_at": datetime.now(timezone.utc).isoformat(),
    "session_count": estimates["summary"]["session_count"],
    "calendar_span_hours": actual.get("calendar_span_hours"),
    "estimates": estimates["summary"],
    "actual": actual["summary"],
}

with open(OUTPUT_PATH, "w") as f:
    json.dump(combined, f, indent=2)

print(f"Wrote {OUTPUT_PATH}")
print(f"  Session count: {combined['session_count']}")
print(f"  Estimates keys: {len(combined['estimates'])}")
print(f"  Actual keys:    {len(combined['actual'])}")
