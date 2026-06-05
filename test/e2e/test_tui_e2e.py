"""TUI E2E tests — drive a0 tui --test-mode via PTY, assert on rendered text.

These tests replace the old bash+expect approach. They use the TuiDriver
which forks a child with a real pseudoterminal, injects keystrokes, and
captures rendered output for assertion."""

import os
import sys
import time
import pytest

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


class TestTuiBasic:
    """Basic TUI startup and command tests."""

    def test_status_bar_idle(self):
        """TUI starts and shows Idle in the status bar."""
        with MockServer() as server:
            with TuiDriver(mock_server=server) as driver:
                ready = wait_for_tui_ready(driver, timeout=15)
                assert ready, "TUI did not show Idle within timeout"

    def test_submit_goal_shows_response(self):
        """Submit a goal, verify LLM response appears in TUI."""
        with MockServer() as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

                driver.send_keys("find log files")
                driver.send_enter()
                deadline = time.monotonic() + 30
                found = False
                last_text = ""
                while time.monotonic() < deadline:
                    text = driver.capture(timeout=3)
                    if text:
                        last_text = text
                        if "Tool executed" in text:
                            found = True
                            break
                assert found, (
                    f"Mock LLM response not seen in TUI within 30s. "
                    f"Last output: {last_text[:200]}"
                )

    def test_help_command(self):
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

    def test_clear_command(self):
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

    def test_quick_quit(self):
        """TUI should exit cleanly with :q command."""
        with MockServer() as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"
                driver.send_keys(":q")
                driver.send_enter()
                time.sleep(1)
                assert driver.exit_code == 0 or driver.exit_code is None, (
                    f"TUI exit code: {driver.exit_code}"
                )


class TestTuiAgentInteraction:
    """Tests that exercise agent interaction through the TUI."""

    def test_multiple_messages(self):
        """Send multiple messages, verify all appear in scrollback."""
        with MockServer() as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

                driver.send_keys("first")
                driver.send_enter()
                time.sleep(1.5)
                text = driver.capture(timeout=5)
                assert "first" in text, (
                    f"First message not visible. Got: {text[:200]}"
                )

                driver.send_keys("second")
                driver.send_enter()
                time.sleep(1.5)
                text = driver.capture(timeout=5)
                assert "second" in text, (
                    f"Second message not visible. Got: {text[:200]}"
                )

    def test_interrupt_streaming(self):
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

    def test_status_bar_updates(self):
        """Status bar should reflect agent state changes after goal submission."""
        with MockServer() as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

                driver.send_keys("hello")
                driver.send_enter()
                time.sleep(1)
                text = driver.capture(timeout=5)
                assert "b1:" in text or "msgs" in text, (
                    f"Status bar should show state. Got: {text[:200]}"
                )

    def test_sessions_command(self):
        """Type /sessions, verify session list dialog appears."""
        with MockServer() as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

                driver.send_keys("/sessions")
                driver.send_enter()
                deadline = time.monotonic() + 12
                found = False
                while time.monotonic() < deadline:
                    text = driver.capture(timeout=1)
                    if "no sessions" in text.lower() or "Sessions" in text:
                        found = True
                        break
                assert found, (
                    f"Expected 'Sessions' dialog. Got: {text[:300]}"
                )


class TestTuiEdgeCases:
    """Edge case tests: mouse, scrollback, paste."""

    def test_mouse_drag_no_crash(self):
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

    def test_scrollback_many_messages(self):
        """Submit multiple goals, verify TUI remains responsive."""
        with MockServer() as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

                for i in range(4):
                    driver.send_keys(f"goal {i:02d}")
                    driver.send_enter()
                    time.sleep(0.5)

                # After submitting all 4, wait for last response
                deadline = time.monotonic() + 20
                found = False
                while time.monotonic() < deadline:
                    text = driver.capture(timeout=2)
                    if "Tool executed" in text:
                        found = True
                        break
                assert found, "TUI should still respond after multiple submissions"

    def test_paste_always_goes_to_input(self):
        """Pasted text should reach the input box even when focus is elsewhere."""
        with MockServer() as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

                driver.send_keys("first message")
                driver.send_enter()
                time.sleep(1.5)

                driver.send_mouse_down(6, 3, button=0)
                time.sleep(0.1)
                driver.send_mouse_up(6, 3)
                time.sleep(0.3)

                driver.send_keys("pasted text")
                driver.send_enter()
                time.sleep(1)

                text = driver.capture(timeout=3)
                assert "pasted text" in text, (
                    f"Pasted text should appear after Enter. Got: {text[:300]}"
                )
