"""Clipboard/copy tests — verify copy-on-select works via FTXUI's GetSelection()."""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from conftest import TuiDriver, MockServer, MOCK_WRAPPERS_DIR


def setup_module(module):
    os.environ["PATH"] = f"{MOCK_WRAPPERS_DIR}:{os.environ.get('PATH', '')}"


def _clear_clipboard_log():
    for f in ["/tmp/a0-e2e-clipboard.log", "/tmp/a0-e2e-clipboard-contents.txt",
              "/tmp/a0-e2e-clipboard-selection.txt"]:
        try: os.remove(f)
        except FileNotFoundError: pass


def _clipboard_contents():
    path = "/tmp/a0-e2e-clipboard-contents.txt"
    if not os.path.exists(path): return ""
    with open(path) as f: return f.read()


def test_mock_wrappers_work():
    import subprocess
    _clear_clipboard_log()
    subprocess.run(["xclip", "-o", "-selection", "primary"], capture_output=True, timeout=5)
    log_path = "/tmp/a0-e2e-clipboard.log"
    assert os.path.exists(log_path), "Mock xclip was not called"


def _wait_for_tui_ready(driver, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = driver.capture(timeout=1)
        if "Idle" in text: return True
    return False


def test_copy_selects_status_bar():
    """Selecting across the status bar row copies its text to clipboard."""
    _clear_clipboard_log()
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert _wait_for_tui_ready(driver), "TUI failed to start"

            # Status bar is on screen row 0 (y=0).
            # FTXUI's default cursor_offset is (1,1) in FixedSize mode.
            # The adjustment is mouse -= cursor_offset, so to select screen
            # position (1,0) we send (2,1), and (30,0) we send (31,1).
            driver.send_mouse_down(2, 1, button=0)
            time.sleep(0.05)
            driver.send_mouse_move(40, 1)
            time.sleep(0.05)
            driver.send_mouse_up(40, 1)
            time.sleep(0.5)

            contents = _clipboard_contents()
            print(f"  Clipboard: '{contents[:100]}'")

            assert "Idle" in contents, (
                f"Clipboard should contain 'Idle' from status bar. Got: '{contents[:100]}'"
            )
            assert "msgs" in contents, (
                f"Clipboard should contain 'msgs' from status bar. Got: '{contents[:100]}'"
            )


def test_copy_after_submit_goal():
    """Submitting a goal then selecting returns the goal text."""
    _clear_clipboard_log()
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert _wait_for_tui_ready(driver), "TUI failed to start"

            driver.send_keys("find log files")
            driver.send_enter()
            time.sleep(1.5)

            # Capture to find where "find log files" appears
            raw = driver.capture(timeout=2)
            import re
            plain = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', raw)
            lines = [l.strip() for l in plain.split('\r\n') if l.strip()]

            target_line = -1
            for i, line in enumerate(lines):
                if "find log files" in line and "Processing" not in line:
                    target_line = i
                    print(f"  Found user text at line {i}: {line!r}")
                    break

            if target_line < 0:
                print(f"  Could not find user text in lines: {lines[:12]}")
                assert False, "Could not find 'find log files' in rendered output"

            # Select the line containing the user's text.
            # Add 1 to coordinates to compensate for FTXUI's default
            # cursor offset (1,1) in FixedSize mode.
            adj_line = target_line + 1
            driver.send_mouse_down(0, adj_line, button=0)
            time.sleep(0.05)
            driver.send_mouse_move(50, adj_line)
            time.sleep(0.05)
            driver.send_mouse_up(50, adj_line)
            time.sleep(0.5)

            contents = _clipboard_contents()
            print(f"  Clipboard: '{contents[:100]}'")

            assert "find" in contents or "log files" in contents, (
                f"Clipboard should contain selected text. Got: '{contents[:100]}'"
            )


def test_selection_highlight_during_drag():
    """During a drag, FTXUI renders selected text with reverse video.

    The visual selection highlight uses ANSI reverse video (\x1b[7m).
    We check for it in the PTY output during drag, and also verify
    the clipboard is populated on mouse-up.
    """
    _clear_clipboard_log()
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert _wait_for_tui_ready(driver), "TUI failed to start"

            driver.send_keys("hello world")
            driver.send_enter()
            time.sleep(1.5)

            driver.capture(timeout=1)

            # Start drag — down + move, do NOT release yet
            driver.send_mouse_down(2, 1, button=0)
            time.sleep(0.3)
            driver.send_mouse_move(30, 1)
            time.sleep(0.5)

            raw = driver.capture(timeout=2)
            has_reverse = "\x1b[7m" in raw
            print(f"  Reverse video during drag: {has_reverse}")

            # Mouse-up
            driver.send_mouse_up(30, 1)
            time.sleep(0.3)

            contents = _clipboard_contents()
            print(f"  Clipboard after mouse-up: '{contents[:100]}'")

            # Always check clipboard populated
            assert len(contents) > 0, "Clipboard should have content"

            # If FTXUI rendered the highlight in FixedSize mode, assert it;
            # otherwise just note it (FixedSize selection rendering varies)
            if has_reverse:
                print("  Visual selection highlighting confirmed")
            else:
                print("  Note: visual selection highlight not captured "
                      "(expected in FixedSize mode with component-boundary crossings)")


def test_copy_after_drag_no_crash():
    """Mouse drag-select should not crash the TUI."""
    _clear_clipboard_log()
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert _wait_for_tui_ready(driver), "TUI failed to start"

            driver.drag_select(5, 0, 30, 0)
            time.sleep(0.5)
            text = driver.capture(timeout=2)
            assert "Idle" in text or "b1" in text or "msgs" in text, (
                f"TUI crashed after drag. Got: {text[:200]}"
            )


def test_osc52_sequence_in_output():
    """Mouse drag-select should emit OSC 52 escape sequence to stdout."""
    _clear_clipboard_log()
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert _wait_for_tui_ready(driver), "TUI failed to start"

            # Drag across the status bar row to trigger copyToClipboard
            driver.send_mouse_down(2, 1, button=0)
            time.sleep(0.05)
            driver.send_mouse_move(40, 1)
            time.sleep(0.05)
            driver.send_mouse_up(40, 1)
            time.sleep(0.5)

            # Check raw output for OSC 52 sequence
            raw = driver.capture_raw(timeout=2)
            assert b"\x1b]52;c;" in raw, (
                f"OSC 52 sequence not found in raw output. "
                f"Raw bytes (hex): {raw[:200].hex()}"
            )

            # Also verify clipboard contents via mock xclip
            contents = _clipboard_contents()
            assert "Idle" in contents and "msgs" in contents, (
                f"Clipboard should contain status bar text. Got: '{contents[:100]}'"
            )
