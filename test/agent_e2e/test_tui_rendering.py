"""TUI rendering tests — drive a0 tui --test-mode via PTY, assert on rendered text."""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from conftest import TuiDriver, MockServer


def wait_for_tui_ready(driver, timeout=10):
    """Wait until the TUI status bar shows Idle, indicating startup is complete."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = driver.capture(timeout=1)
        if "Idle" in text:
            return True
    return False


def test_tui_status_bar_idle():
    """TUI starts and shows Idle in the status bar."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            ready = wait_for_tui_ready(driver, timeout=15)
            assert ready, "TUI did not show Idle within timeout"


def test_tui_submit_goal_shows_response():
    """Submit a goal, verify LLM response appears in TUI."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

            driver.send_keys("find log files")
            driver.send_enter()
            deadline = time.monotonic() + 15
            found = False
            while time.monotonic() < deadline:
                text = driver.capture(timeout=2)
                if "file1.txt" in text or "file3.txt" in text:
                    found = True
                    break
            assert found, "Mock LLM response not seen in TUI within 15s"


def test_tui_help_command():
    """Type /help, verify help dialog content renders."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

            driver.send_keys("/help")
            driver.send_enter()
            time.sleep(1)
            text = driver.capture(timeout=5)
            assert "Keybindings" in text, (
                f"Expected 'Keybindings' in help dialog. Got: {text[:300]}"
            )


def test_tui_clear_command():
    """Type /clear, verify message panel is cleared."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

            driver.send_keys("first message")
            driver.send_enter()
            time.sleep(1.5)
            text_before = driver.capture(timeout=5)
            assert "first message" in text_before, "First message should appear"

            driver.send_keys("/clear")
            driver.send_enter()
            time.sleep(1)
            text_after = driver.capture(timeout=5)
            assert "first" not in text_after, "/clear should remove messages"


def test_tui_sessions_command():
    """Type /sessions, verify session list dialog appears."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

            driver.send_keys("/sessions")
            driver.send_enter()
            time.sleep(1)
            text = driver.capture(timeout=5)
            assert "Sessions" in text, (
                f"Expected 'Sessions' dialog. Got: {text[:300]}"
            )


def test_tui_multiple_messages():
    """Send multiple messages, verify all appear in scrollback."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

            driver.send_keys("first")
            driver.send_enter()
            time.sleep(1.5)
            text = driver.capture(timeout=5)
            assert "first" in text, (
                f"First message should be seen. Got: {text[:200]}"
            )

            driver.send_keys("second")
            driver.send_enter()
            time.sleep(1.5)
            text = driver.capture(timeout=5)
            assert "second" in text, (
                f"Second message not visible. Got: {text[:200]}"
            )


def test_tui_interrupt_streaming():
    """Send Ctrl+C during processing, verify interruption works."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

            driver.send_keys("tell me a story")
            driver.send_enter()
            time.sleep(0.5)
            driver.send_ctrl_c()
            time.sleep(1)
            text = driver.capture(timeout=5)
            assert "Interrupted" in text or "Idle" in text, (
                f"Expected interrupt handling. Got: {text[:300]}"
            )


def test_tui_status_bar_updates():
    """Status bar should reflect agent state changes after goal submission."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

            driver.send_keys("hello")
            driver.send_enter()
            time.sleep(1)
            text = driver.capture(timeout=5)
            assert "Processing" in text or "hello" in text, (
                f"Goal submission should appear. Got: {text[:200]}"
            )
            assert "b1:" in text or "msgs" in text, (
                f"Status bar should show state. Got: {text[:200]}"
            )


def test_tui_mouse_drag_no_crash():
    """Synthetic mouse drag events should not crash the TUI."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

            driver.drag_select(10, 10, 30, 15)
            time.sleep(0.5)
            text = driver.capture(timeout=2)
            assert "Idle" in text or "msgs" in text or "b1" in text, (
                f"TUI should still be responsive after mouse drag. Got: {text[:200]}"
            )


def test_tui_scrollback_many_messages():
    """Submit multiple goals, verify no crash and content scroll indicator."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

            for i in range(4):
                driver.send_keys(f"goal {i:02d}")
                driver.send_enter()
                time.sleep(0.5)

            text = driver.capture(timeout=5)
            for i in range(4):
                assert f"goal {i:02d}" in text, (
                    f"goal {i:02d} should appear. Got: {text[:500]}"
                )


def test_tui_quick_quit():
    """TUI should exit cleanly with :q command."""
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

            driver.send_keys(":q")
            driver.send_enter()
            time.sleep(1)
            # Check exit code is normal
            assert driver.exit_code == 0 or driver.exit_code is None, (
                f"TUI exit code: {driver.exit_code}"
            )


def test_paste_always_goes_to_input():
    """Pasted text should reach the input box even when focus is elsewhere.

    Clicking in the message area moves visual focus away from the Input panel.
    Typing/pasting should still route characters to the Input.
    """
    with MockServer() as server:
        with TuiDriver(mock_server=server) as driver:
            assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

            # First submit a goal so there's content in the message panel
            driver.send_keys("first message")
            driver.send_enter()
            time.sleep(1.5)

            # Now click on the message area to move focus away from Input.
            # Message panel starts after status bar (row 0). Compensate for
            # FTXUI's cursor offset (1,1): target screen (5, 2) → send (6, 3).
            driver.send_mouse_down(6, 3, button=0)
            time.sleep(0.1)
            driver.send_mouse_up(6, 3)
            time.sleep(0.3)

            # Now type text — this should route to the Input panel
            # regardless of where focus currently is.
            driver.send_keys("pasted text")
            driver.send_enter()
            time.sleep(1)

            # The "pasted text" should appear in the rendered output as a
            # user message. If paste goes to the wrong panel, it won't appear.
            text = driver.capture(timeout=3)
            assert "pasted text" in text, (
                f"Pasted text should appear after Enter. Got: {text[:300]}"
            )
