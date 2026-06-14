#!/usr/bin/env python3
"""Snapshot TUI frames v3 - continuous read, frame detection."""

import os, sys, time, json, select, re

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'test', 'e2e'))
from conftest import TuiDriver, A0_BIN, SKILLS_DIR

RUN_ID = sys.argv[1] if len(sys.argv) > 1 else f"snap-{int(time.time())}"
OUT = f"/tmp/tui-snapshots/{RUN_ID}"
os.makedirs(OUT, exist_ok=True)

def strip_ansi(t):
    return re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', t)

# Use a trivial fixture so the mock server produces multi-tool responses quickly
# Otherwise the real API costs $$ and takes 20+ seconds per turn
import subprocess, tempfile, json as j

# We'll use a MockServer with a scenario that produces tool calls and a response
# BUT inject a real-ish delay pattern to simulate uneven SSE arrival.
# Actually... the user said don't use mock server. Use real API.
# But real API costs money and takes very long.
# 
# Let me just run the real API one more time but with better capture.
# We'll use the first approach but fix the capture cadence.

CAPTURE_SECONDS = 60

driver = TuiDriver(
    mock_server=None,
    a0_dir=f"/tmp/a0-snap-{os.getpid()}",
    extra_args=["--no-b1"],
    test_mode=True
)

frames = []
all_raw = b""

try:
    driver.start()
    print("TUI started. Waiting for Idle...", file=sys.stderr)

    deadline = time.monotonic() + 15
    ready = False
    while time.monotonic() < deadline:
        r, _, _ = select.select([driver.master_fd], [], [], 0.5)
        if r:
            try:
                data = os.read(driver.master_fd, 65536)
                if data:
                    all_raw += data
                    if b"Idle" in all_raw:
                        ready = True
                        break
            except OSError:
                break

    if not ready:
        print("TUI didn't become ready", file=sys.stderr)
        driver.stop()
        sys.exit(1)

    print(f"TUI ready at t={time.monotonic():.3f}. Submitting goal...", file=sys.stderr)
    all_raw = b""
    driver.send_keys("list files in the current directory")
    driver.send_enter()

    start_ts = time.monotonic()
    frame_count = 0
    last_save = 0
    deadline = start_ts + CAPTURE_SECONDS

    # Accumulate all output, snapshot every time output size stabilizes for 5ms
    while time.monotonic() < deadline:
        r, _, _ = select.select([driver.master_fd], [], [], 0.005)
        if r:
            try:
                data = os.read(driver.master_fd, 65536)
                if data:
                    all_raw += data
            except OSError:
                break
        else:
            # No data for 5ms - save a snapshot
            now = time.monotonic()
            if now - last_save >= 0.016 and len(all_raw) > 0:
                fname = f"frame_{frame_count:05d}.raw"
                with open(os.path.join(OUT, fname), 'wb') as f:
                    f.write(all_raw)
                plain = strip_ansi(all_raw.decode("utf-8", errors="replace"))
                with open(os.path.join(OUT, f"frame_{frame_count:05d}.txt"), 'w') as f:
                    f.write(plain)
                ts = now - start_ts
                frames.append({
                    "frame": frame_count, "file": fname,
                    "ts": round(ts, 3), "size": len(all_raw)
                })
                frame_count += 1
                last_save = now

    with open(os.path.join(OUT, "manifest.json"), 'w') as f:
        j.dump({"run_id": RUN_ID, "frames": frames, "total_frames": frame_count}, f, indent=2)

    print(f"Done. {frame_count} frames to {OUT}", file=sys.stderr)
    print(OUT)

finally:
    driver.stop()
