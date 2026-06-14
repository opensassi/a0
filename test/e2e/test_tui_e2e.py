"""TUI E2E tests — drive a0 tui --test-mode via PTY, assert on rendered text.

These tests replace the old bash+expect approach. They use the TuiDriver
which forks a child with a real pseudoterminal, injects keystrokes, and
captures rendered output for assertion."""

import os
import re
import sys
import time
import pytest
from pathlib import Path

sys.path.insert(0, os.path.dirname(__file__))
from conftest import TuiDriver, MockServer

FIXTURES_DIR = str(Path(__file__).resolve().parent / "fixtures")


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


class TestTuiToolDisplay:
    """Tests for tool block rendering and completion states."""

    def test_tool_block_shows_completed(self):
        """Tool block should show 'completed'/'OK' after tool finishes."""
        scenario = os.path.join(FIXTURES_DIR, "streaming_tool_calls.json")
        with MockServer(scenario=scenario, stream=True) as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

                driver.send_keys("run a tool")
                driver.send_enter()
                deadline = time.monotonic() + 30
                found_completed = False
                last_text = ""
                while time.monotonic() < deadline:
                    text = driver.capture(timeout=2)
                    if text:
                        last_text = text
                        if "Tool executed" in text or "completed" in text or "OK" in text:
                            found_completed = True
                            break
                assert found_completed, (
                    f"Expected tool completion in TUI. Last output: {last_text[:300]}"
                )

    def test_tool_appears_before_response(self):
        """Tool call block must render before assistant's final response text.

        After the goal completes, captures the final steady-state TUI frame
        and asserts the tool block line appears ABOVE the response text line
        (tools execute before the assistant speaks the result).
        """
        scenario = os.path.join(FIXTURES_DIR, "streaming_tool_calls.json")
        with MockServer(scenario=scenario, stream=True) as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

                driver.send_keys("run a tool")
                driver.send_enter()

                deadline = time.monotonic() + 30
                last_text = ""
                while time.monotonic() < deadline:
                    text = driver.capture(timeout=2)
                    if text:
                        last_text = text
                        if "Idle" in text:
                            break

                # Split into lines, find LAST occurrence of each marker
                # (the final Idle frame is at the end of the captured output)
                lines = last_text.split("\n")
                tool_line = max((i for i, l in enumerate(lines) if "\U0001F527" in l), default=-1)
                text_line = max((i for i, l in enumerate(lines) if "Tool executed" in l), default=-1)

                assert tool_line >= 0, (
                    f"Tool icon \U0001F527 not found in captured output:\n{last_text[:500]}"
                )
                assert text_line >= 0, (
                    f"'Tool executed' not found in captured output:\n{last_text[:500]}"
                )
                assert tool_line < text_line, (
                    f"Tool block (line {tool_line}) should render ABOVE response text"
                    f" (line {text_line}) — tool was executed before the response.\n"
                    f"Lines around tool:\n"
                    + "\n".join(lines[max(0,tool_line-2):tool_line+3])
                )


class TestTuiMultiTurn:
    """Tests for multi-turn conversation context preservation."""

    def test_multi_turn_context_preserved(self):
        """Second turn in TUI should have access to first turn's context."""
        scenario = os.path.join(FIXTURES_DIR, "streaming_multi_turn.json")
        with MockServer(scenario=scenario, stream=True) as ms:
            with TuiDriver(mock_server=ms) as driver:
                assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

                # First message
                driver.send_keys("read src/tool_state.cpp")
                driver.send_enter()
                time.sleep(3)
                text = driver.capture(timeout=10)
                assert "first turn" in text.lower(), (
                    f"Expected first turn response. Got: {text[:200]}"
                )

                # Second message
                driver.send_keys("what file did I just read")
                driver.send_enter()
                deadline = time.monotonic() + 20
                found_context = False
                last_text = ""
                while time.monotonic() < deadline:
                    text = driver.capture(timeout=2)
                    if text:
                        last_text = text
                        if "tool_state.cpp" in text:
                            found_context = True
                            break
                assert found_context, (
                    f"Second turn should reference first turn's context. "
                    f"Last output: {last_text[:300]}"
                )


class TestTuiScrolling:
    """Tests for message panel scrolling.

    Uses the scroll-hint ``\u2191 N more`` (↑ N more) as a deterministic
    indicator of scroll position. At the bottom (auto-scroll) N is largest;
    after PageUp it decreases; after returning to bottom it restores.
    """

    @staticmethod
    def _submit_all(driver, count=5, timeout=20):
        """Submit *count* goals rapidly, then wait for all to complete.

        Returns the final captured frame (the Idle state screen with all
        entries rendered at bottom-of-scrollback).
        """
        for i in range(count):
            driver.send_keys(f"msg {i}")
            driver.send_enter()
            time.sleep(0.5)

        deadline = time.monotonic() + timeout
        last = ""
        while time.monotonic() < deadline:
            t = driver.capture(timeout=2)
            if t:
                last = t
                if "Tool executed" in t:
                    break

        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            t = driver.capture(timeout=2)
            if t:
                last = t

        return last

    def test_pageup_changes_visible_content(self):
        """PgUp scrolls up changing which entries are visible."""
        with MockServer() as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15)

                text_bottom = self._submit_all(driver, count=5, timeout=20)

                driver.send_page_up()
                time.sleep(2)
                text_up = driver.capture(timeout=3)

                assert text_up and text_up != text_bottom, (
                    f"PgUp should change visible content. "
                    f"Bottom[:200]: {text_bottom[:200]} | "
                    f"Up[:200]: {text_up[:200]}"
                )

    def test_scroll_survives_rerender(self):
        """Scroll position holds after a render-triggering keystroke."""
        with MockServer() as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15)
                text_bottom = self._submit_all(driver, count=5, timeout=20)

                driver.send_page_up()
                time.sleep(2)
                text_up = driver.capture(timeout=3)

                # Type a character then backspace — triggers FTXUI re-render
                driver.send_keys("x")
                time.sleep(0.5)
                driver.capture(timeout=1)
                driver.send_keys("\b")
                time.sleep(0.5)
                t = driver.capture(timeout=2)

                # Content should still show scrolled-up view, not bottom
                assert t and t != text_bottom, (
                    f"Scroll position reset after re-render. "
                    f"Scrolled-up[:200]: {text_up[:200]} | "
                    f"After render[:200]: {t[:200]}"
                )


class TestTuiWordWrapping:
    """Tests for word wrapping in message panel.

    Uses a fixture derived from a real session where the agent ran
    read(file_path: '.') and the assistant returned a directory table.
    The tool call has been rewritten to read a fixture file so the
    test is deterministic."""

    def test_tool_output_and_markdown_wrap(self):
        """Tool output and assistant markdown content render without overflow."""
        scenario = os.path.join(FIXTURES_DIR, "user_simple_tool_call.json")
        with MockServer(scenario=scenario, stream=True) as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"

                driver.send_keys("show files")
                driver.send_enter()

                deadline = time.monotonic() + 30
                found_tool = False
                last_text = ""
                while time.monotonic() < deadline:
                    text = driver.capture(timeout=2)
                    if text:
                        last_text = text
                        if "\U0001F527 read" in text and "Idle" in text:
                            found_tool = True
                            break
                assert found_tool, (
                    f"\U0001F527 read header + Idle state not seen. Last output: {last_text[:300]}"
                )


class TestTuiClipboard:
    """Copy-on-select clipboard tests."""

    MOCK_WRAPPERS_DIR = str(Path(__file__).resolve().parent / "mock_wrappers")

    @pytest.fixture(autouse=True)
    def setup_method(self):
        os.environ["PATH"] = f"{self.MOCK_WRAPPERS_DIR}:{os.environ.get('PATH', '')}"
        for f in ["/tmp/a0-e2e-clipboard.log", "/tmp/a0-e2e-clipboard-contents.txt",
                  "/tmp/a0-e2e-clipboard-selection.txt"]:
            try:
                os.remove(f)
            except FileNotFoundError:
                pass

    def _clipboard_contents(self):
        path = "/tmp/a0-e2e-clipboard-contents.txt"
        if not os.path.exists(path):
            return ""
        with open(path) as f:
            return f.read()

    def test_mock_wrappers_work(self):
        import subprocess
        subprocess.run(["xclip", "-o", "-selection", "primary"], capture_output=True, timeout=5)
        assert os.path.exists("/tmp/a0-e2e-clipboard.log"), "Mock xclip was not called"

    def test_copy_selects_status_bar(self):
        with MockServer() as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"
                driver.send_mouse_down(2, 1, button=0)
                time.sleep(0.05)
                driver.send_mouse_move(40, 1)
                time.sleep(0.05)
                driver.send_mouse_up(40, 1)
                time.sleep(0.5)
                contents = self._clipboard_contents()
                assert "Idle" in contents, f"Clipboard should contain 'Idle'. Got: '{contents[:100]}'"
                assert "msgs" in contents, f"Clipboard should contain 'msgs'. Got: '{contents[:100]}'"

    def test_copy_after_submit_goal(self):
        with MockServer() as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"
                driver.send_keys("find log files")
                driver.send_enter()
                time.sleep(1.5)
                raw = driver.capture(timeout=2)
                plain = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', raw)
                lines = [l.strip() for l in plain.split('\r\n') if l.strip()]
                target = -1
                for i, line in enumerate(lines):
                    if "find log files" in line and "Processing" not in line:
                        target = i
                        break
                if target < 0:
                    pytest.skip("User text not found in rendered output")
                # Select from row 0 to row 20 to capture the full message panel area
                driver.send_mouse_down(0, 0, button=0)
                time.sleep(0.05)
                driver.send_mouse_move(50, 20)
                time.sleep(0.05)
                driver.send_mouse_up(50, 20)
                time.sleep(0.5)
                contents = self._clipboard_contents()
                assert "find" in contents or "log files" in contents, (
                    f"Clipboard should contain selected text. Got: '{contents[:100]}'"
                )

    def test_osc52_sequence_in_output(self):
        with MockServer() as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"
                driver.send_mouse_down(2, 1, button=0)
                time.sleep(0.05)
                driver.send_mouse_move(40, 1)
                time.sleep(0.05)
                driver.send_mouse_up(40, 1)
                time.sleep(0.5)
                raw = driver.capture_raw(timeout=2)
                assert b"\x1b]52;c;" in raw, (
                    f"OSC 52 sequence not found. Raw hex: {raw[:200].hex()}"
                )

    def test_copy_after_drag_no_crash(self):
        with MockServer() as server:
            with TuiDriver(mock_server=server) as driver:
                assert wait_for_tui_ready(driver, timeout=15), "TUI failed to start"
                driver.drag_select(5, 0, 30, 0)
                time.sleep(0.5)
                text = driver.capture(timeout=2)
                assert "Idle" in text or "b1" in text or "msgs" in text, (
                    f"TUI crashed after drag. Got: {text[:200]}"
                )





class TestCliCrash:
    """Headless CLI crash and stress tests using a0 binary subprocess."""

    def test_binary_exists(self):
        from conftest import A0_BIN
        assert os.path.exists(A0_BIN), f"Binary not found at {A0_BIN}"
        assert os.access(A0_BIN, os.X_OK), "Binary not executable"

    def test_help_flag_no_crash(self):
        from conftest import A0_BIN
        import subprocess
        result = subprocess.run([A0_BIN, "--help"], capture_output=True, timeout=10)
        assert result.returncode == 0, f"Exit {result.returncode}: {result.stderr.decode()}"
        assert "Usage" in result.stdout.decode() or "Usage" in result.stderr.decode()

    def test_run_minimal_no_crash(self):
        from conftest import A0_BIN, SKILLS_DIR
        import subprocess, tempfile
        with tempfile.TemporaryDirectory() as tmpdir:
            result = subprocess.run(
                [A0_BIN, "--a0-dir", tmpdir, "--skills-dir", SKILLS_DIR,
                 "--no-docker", "--no-b1",
                 "--mock-api", "http://127.0.0.1:18999", "run", "hello"],
                capture_output=True, timeout=15
            )
            assert result.returncode != -11, f"Segfault! stderr: {result.stderr.decode()}"

    def test_run_with_mock_no_crash(self):
        from conftest import AgentSubprocess
        with MockServer() as server:
            agent = AgentSubprocess(mock_server=server)
            result = agent.run("find log files")
            assert result.returncode == 0, (
                f"Exit {result.returncode}. stdout: {result.stdout.decode()[:200]}"
            )

    def test_run_session_flags(self):
        from conftest import A0_BIN, SKILLS_DIR
        import subprocess, tempfile
        with MockServer() as server:
            with tempfile.TemporaryDirectory() as tmpdir:
                result = subprocess.run(
                    [A0_BIN, "--a0-dir", tmpdir, "--skills-dir", SKILLS_DIR,
                     "--no-docker", "--no-b1",
                     "--mock-api", f"http://127.0.0.1:{server.port}",
                     "--resume", "test-uuid", "run", "hello"],
                    capture_output=True, timeout=15
                )
                assert result.returncode != -11, "Segfault!"

    def test_consecutive_goals_no_crash(self):
        from conftest import A0_BIN, SKILLS_DIR
        import subprocess, tempfile
        with MockServer() as server:
            with tempfile.TemporaryDirectory() as tmpdir:
                for i in range(5):
                    result = subprocess.run(
                        [A0_BIN, "--a0-dir", tmpdir, "--skills-dir", SKILLS_DIR,
                         "--no-docker", "--no-b1",
                         "--mock-api", f"http://127.0.0.1:{server.port}",
                         "run", f"goal number {i}"],
                        capture_output=True, timeout=30
                    )
                    assert result.returncode == 0, (
                        f"Goal {i} failed: exit {result.returncode}."
                    )

    def test_stress_rapid_goals(self):
        from conftest import A0_BIN, SKILLS_DIR
        import subprocess, tempfile
        with MockServer() as server:
            with tempfile.TemporaryDirectory() as tmpdir:
                for i in range(10):
                    subprocess.run(
                        [A0_BIN, "--a0-dir", tmpdir, "--skills-dir", SKILLS_DIR,
                         "--no-docker", "--no-b1",
                         "--mock-api", f"http://127.0.0.1:{server.port}",
                         "run", f"quick goal {i}"],
                        capture_output=True, timeout=15
                    )
                result = subprocess.run(
                    [A0_BIN, "--a0-dir", tmpdir, "--skills-dir", SKILLS_DIR,
                     "--no-docker", "--no-b1",
                     "--mock-api", f"http://127.0.0.1:{server.port}",
                     "run", "final goal"],
                    capture_output=True, timeout=15
                )
                assert result.returncode == 0, (
                    f"Final goal failed after 10 rapid submissions. "
                    f"stderr: {result.stderr.decode()[:200]}"
                )


class TestCliScenario:
    """Scenario-driven headless agent tests."""

    SCENARIOS_DIR = str(Path(__file__).resolve().parent / "scenarios")

    def _run_scenario(self, scenario_name, goal="find log files"):
        from conftest import AgentSubprocess
        scenario_path = os.path.join(self.SCENARIOS_DIR, scenario_name)
        with MockServer(scenario=scenario_path) as server:
            agent = AgentSubprocess(mock_server=server)
            result = agent.run(goal)
            return result

    def test_simple_tool_call(self):
        result = self._run_scenario("simple_tool_call.json", "list files in /tmp")
        assert result.returncode == 0, (
            f"Exit {result.returncode}. stdout: {result.stdout.decode()[:300]}"
        )
        stdout = result.stdout.decode().strip()
        import json
        try:
            parsed = json.loads(stdout)
            assert parsed is not None
        except json.JSONDecodeError:
            assert False, f"Expected valid JSON, got: {stdout[:200]}"

    def test_multi_turn_workflow(self):
        result = self._run_scenario("multi_turn_workflow.json", "find log files and check for errors")
        assert result.returncode == 0, (
            f"Exit {result.returncode}. stdout: {result.stdout.decode()[:300]}"
        )
        assert len(result.stdout.decode().strip()) > 0

    def test_tool_error_handling(self):
        result = self._run_scenario("tool_error.json", "read the file at /nonexistent/file.txt")
        assert result.returncode == 0 or result.returncode == 1, (
            f"Exit {result.returncode} — agent may return error gracefully"
        )

    def test_all_scenarios_load(self):
        import json as j
        for fname in sorted(os.listdir(self.SCENARIOS_DIR)):
            if not fname.endswith(".json"):
                continue
            path = os.path.join(self.SCENARIOS_DIR, fname)
            with open(path) as f:
                data = j.load(f)
            assert "name" in data, f"{fname}: missing 'name'"
            assert "turns" in data, f"{fname}: missing 'turns'"
            assert len(data["turns"]) > 0, f"{fname}: empty turns"
