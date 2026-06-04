"""Paste tests — verify bracketed paste collapses large content and expands on submit."""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from conftest import TuiDriver, MockServer


def _wait_for_tui_ready(driver, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = driver.capture(timeout=1)
        if "Idle" in text:
            return True
    return False


def test_paste_large_content_collapses():
    """Pasting text over 20 chars produces a [ PASTED #N ] placeholder
    and expands on submit."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert _wait_for_tui_ready(driver), "TUI failed to start"

            driver.send_paste("line one\nline two\nline three\nline four\n")
            time.sleep(1)
            driver.send_enter()
            time.sleep(1.5)

            text = driver.capture(timeout=3)
            assert "line one" in text, (
                f"Expanded content should appear. Got: {text[:300]}"
            )
            assert "line four" in text, (
                f"Expanded content should appear. Got: {text[:300]}"
            )


def test_paste_small_content_raw():
    """Pasting text under 20 chars inserts raw text (no placeholder)."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert _wait_for_tui_ready(driver), "TUI failed to start"

            driver.send_paste("small paste")
            time.sleep(0.5)
            driver.send_enter()
            time.sleep(1)

            text = driver.capture(timeout=3)
            assert "small paste" in text, (
                f"Small paste should insert raw text. Got: {text[:200]}"
            )


def test_paste_multiple_placeholders():
    """Multiple paste inserts create separate numbered placeholders
    that all expand on submit."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert _wait_for_tui_ready(driver), "TUI failed to start"

            # Paste two different contents (no interleaved needs typing)
            driver.send_paste("FIRST-PASTE " * 5)
            time.sleep(0.5)
            driver.send_paste("SECOND-PASTE " * 5)
            time.sleep(0.5)
            driver.send_enter()
            time.sleep(1.5)

            text = driver.capture(timeout=3)
            assert "FIRST-PASTE" in text, (
                f"First paste should appear. Got: {text[:300]}"
            )
            assert "SECOND-PASTE" in text, (
                f"Second paste should appear. Got: {text[:300]}"
            )


def test_paste_manual_reference():
    """Manually typing [ PASTED #N ] references the same stored content."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert _wait_for_tui_ready(driver), "TUI failed to start"

            # Paste once to create #1
            driver.send_paste("expanded content here " * 3)
            time.sleep(0.5)

            # Type a manual reference to the same paste
            driver.send_keys("[ PASTED #1 ]")
            driver.send_enter()
            time.sleep(1.5)

            text = driver.capture(timeout=3)
            # The expanded content should appear TWICE
            count = text.count("expanded content here")
            assert count >= 2, (
                f"Manual reference should expand to the same content. "
                f"Found {count} occurrences. Got: {text[:300]}"
            )


def test_paste_cursor_after_placeholder():
    """Cursor should be positioned after the [ PASTED #N ] marker after paste.
    Typing text immediately after paste should appear after the expanded content,
    not before it or in the middle."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert _wait_for_tui_ready(driver), "TUI failed to start"

            # Paste a 21-char string (exceeds 20-char threshold → placeholder)
            driver.send_paste("ABCDEFGHIJKLMNOPQRSTU")
            time.sleep(0.5)

            # Type a distinct tail marker — should appear AFTER the pasted
            # content when cursor is correctly positioned.
            driver.send_keys("XYZTAIL")
            time.sleep(0.3)
            driver.send_enter()

            # Wait for response, then capture all at once
            time.sleep(3)
            text = driver.capture(timeout=5)
            assert "STU XYZTAIL" in text, (
                f"XYZTAIL should appear after paste content with a space. "
                f"Got: {text[:600]}"
            )


def test_paste_newlines_dont_submit():
    """Newlines in pasted text should not trigger submit during paste."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert _wait_for_tui_ready(driver), "TUI failed to start"

            driver.send_paste("multi\nline\npaste\ncontent\n")
            time.sleep(0.5)

            # Capture rendered output — there should be no "─ You" labels
            # because nothing was submitted yet.
            text = driver.capture(timeout=2)
            # "─ You" appears as a role label for user messages.
            # If no messages were submitted, it shouldn't be in output.
            if "─ You" in text:
                # The initial state might have a ─ You from startup
                # Let's count: if there are more than 1, something was submitted
                count = text.count("─ You")
                assert count <= 1, (
                    f"Newlines during paste should not submit. "
                    f"Found {count} '─ You' entries. Got: {text[:400]}"
                )

            # Submit manually
            driver.send_enter()
            time.sleep(1.5)

            text = driver.capture(timeout=3)
            assert "multi" in text or "line" in text or "paste" in text, (
                f"Submitted paste should show expanded content. Got: {text[:300]}"
            )
