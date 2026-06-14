# TuiSnapshot Spec

## §1. Overview

**Role:** Frame capture tool for the TUI. Launches `a0 tui --test-mode` with a real API connection, submits a goal ("list files in the current directory"), and captures screen frames at ~60fps for a configurable duration. Uses continuous polling with 5ms idle detection to detect frame boundaries. Outputs raw ANSI frames and plain-text extracts to a timestamped directory.

**Source file:** `scripts/tui-snapshot3.py`

**Dependencies:** `test/e2e/conftest.py` (TuiDriver), Python standard library (`os`, `sys`, `select`, `re`, `json`, `time`)

**Lifecycle:**
1. Allocate output directory: `/tmp/tui-snapshots/<run-id>/`
2. Start `TuiDriver` with no mock server (real API)
3. Wait for TUI to reach Idle state
4. Submit goal via `send_keys` + `send_enter`
5. Poll PTY master at 5ms intervals for output data
6. When output stalls for 5ms and ≥16ms since last save: write frame files
7. After `CAPTURE_SECONDS` (60s default): write manifest JSON
8. Stop TuiDriver via `driver.stop()`

## §2. Component Specifications

```python
# Configuration
CAPTURE_SECONDS = 60          # Total capture window
SNAPSHOT_INTERVAL = 0.016     # Minimum interval between frames (~60fps)
POLL_TIMEOUT = 0.005          # PTY select timeout (5ms idle detection)
# Output: /tmp/tui-snapshots/<run-id>/
#   frame_NNNNN.raw   — raw ANSI bytes
#   frame_NNNNN.txt   — plain text (ANSI stripped)
#   manifest.json     — frame index with timestamps and sizes
```

No classes defined — the file is a single script with helper functions:
- `strip_ansi(text)` — removes ANSI escape sequences via regex
- Main block: driver lifecycle, poll loop, frame detection, output

## §3. Architecture Diagram

```mermaid
graph TB
    subgraph "Host"
        SCRIPT[tui-snapshot3.py]
        TUI[TuiDriver: a0 tui --test-mode]
        OUT[Output: /tmp/tui-snapshots/]
    end

    subgraph "Frame Capture Loop"
        POLL[select.poll 5ms]
        READ[os.read PTY master]
        DETECT{5ms idle?}
        SAVE[save .raw + .txt]
    end

    subgraph "Output Artifacts"
        RAW[frame_N.raw]
        TXT[frame_N.txt]
        MAN[manifest.json]
    end

    SCRIPT --> TUI
    SCRIPT --> POLL
    POLL --> READ
    READ --> DETECT
    DETECT -->|yes| SAVE
    SAVE --> RAW
    SAVE --> TXT
    SCRIPT --> MAN
```

## §4. Data Flow

```mermaid
sequenceDiagram
    participant U as User
    participant S as tui-snapshot3.py
    participant D as TuiDriver
    participant A as a0 Process
    participant API as DeepSeek API

    U->>S: python3 tui-snapshot3.py [run-id]
    S->>D: TuiDriver(mock_server=None, test_mode=True)
    D->>D: fork + exec a0 tui --test-mode
    loop Wait for Idle (15s max)
        S->>D: select.poll(500ms)
        D-->>S: PTY data
        S->>S: check for b"Idle"
    end
    S->>D: send_keys("list files in the current directory")
    S->>D: send_enter()
    loop Capture frames (60s)
        S->>D: select.poll(5ms)
        D-->>S: PTY data
        alt data available
            S->>S: accumulate all_raw
        else 5ms idle
            S->>S: if 16ms since last save: write frame
        end
    end
    S->>S: write manifest.json
    S->>D: driver.stop()
```

## §5. Testing Requirements

| Test | Verification |
|------|-------------|
| Invocation without run-id | Generates timestamp-based run-id |
| Output directory creation | `/tmp/tui-snapshots/<run-id>/` created |
| Frame capture | At least one `.raw` + `.txt` pair written |
| Manifest format | JSON with `run_id`, `frames[]`, `total_frames` |
| ANSI stripping | `.txt` files contain no escape sequences |
| Graceful shutdown | `driver.stop()` called even on exception |

## §6. (skip)

## §7. CLI Entry Point

```
python3 scripts/tui-snapshot3.py [run-id]

Arguments:
  run-id    Optional identifier for the capture run (default: snap-<timestamp>)

Output:
  /tmp/tui-snapshots/<run-id>/
    frame_NNNNN.raw   Raw PTY output with ANSI escapes
    frame_NNNNN.txt   Plain text (ANSI stripped)
    manifest.json     Frame metadata index
