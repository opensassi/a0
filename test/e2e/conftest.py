"""E2E test infrastructure: TuiDriver, AgentSubprocess, MockServer, and shared fixtures."""

import json
import os
import pty
import re
import select
import signal
import subprocess
import sys
import time
import socket
from pathlib import Path

PROJECT_ROOT = str(Path(__file__).resolve().parent.parent.parent)
A0_BIN = str(Path(__file__).resolve().parent.parent.parent / "build" / "a0")
SKILLS_DIR = str(Path(__file__).resolve().parent.parent.parent / "skills")
MOCK_SERVER = str(Path(__file__).resolve().parent / "mock_deepseek_server.py")


def strip_ansi(text):
    """Remove ANSI escape sequences from text."""
    return re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text)


def find_free_port():
    """Find a free TCP port on localhost."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


class MockServer:
    """Manages the mock DeepSeek API server process."""

    def __init__(self, scenario=None, port=None, stream=False, chunk_delay=0.0):
        self.port = port or find_free_port()
        self.scenario = scenario
        self.stream = stream
        self.chunk_delay = chunk_delay
        self.process = None

    def __enter__(self):
        cmd = [sys.executable, MOCK_SERVER, str(self.port)]
        if self.scenario:
            cmd += ["--scenario", self.scenario]
        if self.stream:
            cmd += ["--stream"]
        if self.chunk_delay > 0:
            cmd += ["--chunk-delay", str(self.chunk_delay)]
        self.process = subprocess.Popen(
            cmd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        self._wait_ready()
        return self

    def __exit__(self, *args):
        if self.process:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()

    def _wait_ready(self, timeout=10):
        import urllib.request
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/") as r:
                    if r.status == 200:
                        return
            except Exception:
                pass
            time.sleep(0.2)
        raise RuntimeError(f"Mock server on port {self.port} failed to start")


class TuiDriver:
    """Drives a0 tui --test-mode via a real PTY.

    Usage:
        with MockServer() as server:
            with TuiDriver(mock_server=server) as driver:
                driver.send_keys("hello\\r")
                text = driver.capture(timeout=3)
                assert "Idle" in text
    """

    def __init__(self, mock_server=None, a0_dir=None, extra_args=None,
                 test_mode=True):
        self.mock_server = mock_server
        self.a0_dir = a0_dir
        self.extra_args = extra_args or []
        self.test_mode = test_mode
        self.master_fd = None
        self.slave_fd = None
        self.pid = None
        self.exit_code_val = None

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *args):
        self.stop()

    def start(self):
        master_fd, slave_fd = pty.openpty()
        self.master_fd = master_fd
        self.slave_fd = slave_fd

        a0_dir = self.a0_dir or f"/tmp/a0-e2e-{os.getpid()}"
        os.makedirs(a0_dir, exist_ok=True)
        env = os.environ.copy()
        env["TERM"] = "xterm-256color"

        cmd = [
            A0_BIN,
            "--a0-dir", a0_dir,
            "--skills-dir", SKILLS_DIR,
            "--no-docker",
            "--no-b1",
            "--external-repo", "",
        ]
        if self.mock_server:
            cmd += ["--mock-api", f"http://127.0.0.1:{self.mock_server.port}"]
        # Redirect stderr to file to prevent curl verbose output from
        # polluting the PTY-captured TUI output.
        cmd += ["--log-file", f"{a0_dir}/a0.log"]
        cmd += self.extra_args
        cmd += ["tui"]
        if self.test_mode:
            cmd += ["--test-mode"]
        cmd += ["--no-permissions"]

        self.pid = os.fork()
        if self.pid == 0:
            os.close(master_fd)
            os.setsid()
            os.dup2(slave_fd, 0)
            os.dup2(slave_fd, 1)
            os.dup2(slave_fd, 2)
            if slave_fd > 2:
                os.close(slave_fd)
            os.execve(cmd[0], cmd, env)
            os._exit(1)
        else:
            os.close(slave_fd)
            time.sleep(1.0)

    def stop(self):
        if self.pid and self.pid > 0:
            try:
                self._write_raw(b":q\r")
                time.sleep(0.5)
                os.kill(self.pid, signal.SIGTERM)
                # Wait for child with timeout (WNOHANG loop)
                for _ in range(50):
                    try:
                        _, status = os.waitpid(self.pid, os.WNOHANG)
                        if status != 0:
                            self.exit_code_val = os.waitstatus_to_exitcode(status)
                            break
                    except ChildProcessError:
                        break
                    time.sleep(0.1)
                else:
                    # Still alive after 5s — force kill
                    try:
                        os.kill(self.pid, signal.SIGKILL)
                        os.waitpid(self.pid, 0)
                    except OSError:
                        pass
            except (OSError, ChildProcessError):
                pass
        for fd in (self.master_fd, self.slave_fd):
            if fd and fd > 0:
                try:
                    os.close(fd)
                except OSError:
                    pass

    @property
    def exit_code(self):
        if self.exit_code_val is not None:
            return self.exit_code_val
        if self.pid:
            try:
                _, status = os.waitpid(self.pid, os.WNOHANG)
                if status != 0:
                    self.exit_code_val = os.waitstatus_to_exitcode(status)
                else:
                    self.exit_code_val = 0
            except OSError:
                pass
        return self.exit_code_val

    def send_keys(self, text):
        self._write_raw(text.encode())

    def send_enter(self):
        self._write_raw(b"\n")

    def send_ctrl_c(self):
        self._write_raw(b"\x03")

    def send_mouse_down(self, x, y, button=0):
        self._write_raw(f"\x1b[<{button};{x};{y}M".encode())

    def send_mouse_up(self, x, y, button=0):
        self._write_raw(f"\x1b[<{button};{x};{y}m".encode())

    def send_mouse_move(self, x, y):
        self._write_raw(f"\x1b[<{32};{x};{y}M".encode())

    def send_page_up(self):
        self._write_raw(b"\x1b[5~")

    def send_page_down(self):
        self._write_raw(b"\x1b[6~")

    def send_home(self):
        self._write_raw(b"\x1b[1~")

    def send_end(self):
        self._write_raw(b"\x1b[4~")

    def send_paste(self, text):
        self._write_raw(b"\x1b[200~")
        self._write_raw(text.encode())
        self._write_raw(b"\x1b[201~")

    def drag_select(self, x1, y1, x2, y2):
        self.send_mouse_down(x1, y1)
        time.sleep(0.05)
        self.send_mouse_move(x2, y2)
        time.sleep(0.05)
        self.send_mouse_up(x2, y2)

    def capture(self, timeout=2.0):
        """Read all available rendered output within timeout, return plain text."""
        output = b""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            r, _, _ = select.select([self.master_fd], [], [], 0.5)
            if r:
                try:
                    data = os.read(self.master_fd, 65536)
                    if data:
                        output += data
                        continue  # keep reading if there's immediate data
                except OSError:
                    break
            else:
                # No data this iteration — if we have output, return it
                if output:
                    break
        return strip_ansi(output.decode("utf-8", errors="replace"))

    def capture_raw(self, timeout=2.0):
        """Read available rendered output, return raw bytes (no ANSI stripping)."""
        output = b""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            r, _, _ = select.select([self.master_fd], [], [], 0.5)
            if r:
                try:
                    data = os.read(self.master_fd, 65536)
                    if data:
                        output += data
                        continue
                except OSError:
                    break
            else:
                if output:
                    break
        return output

    def _write_raw(self, data):
        if self.master_fd and self.master_fd > 0:
            os.write(self.master_fd, data)


class AgentSubprocess:
    """Runs a0 as a subprocess (non-TUI / headless mode) with assertions.

    Uses the ``run`` subcommand. Global flags come before the subcommand.
    """

    def __init__(self, mock_server=None, a0_dir=None, skills_dir=None, extra_args=None):
        self.mock_server = mock_server
        self.a0_dir = a0_dir or f"/tmp/a0-e2e-sub-{os.getpid()}"
        self.skills_dir = skills_dir
        self.extra_args = extra_args or []
        self.result = None

    def run(self, goal, timeout=30):
        os.makedirs(self.a0_dir, exist_ok=True)
        cmd = [
            A0_BIN,
            "--a0-dir", self.a0_dir,
            "--no-docker",
            "--no-b1",
        ]
        if self.skills_dir:
            cmd += ["--skills-dir", self.skills_dir]
        else:
            cmd += ["--skills-dir", SKILLS_DIR]
        if self.mock_server:
            cmd += ["--mock-api", f"http://127.0.0.1:{self.mock_server.port}"]
        cmd += self.extra_args
        cmd += ["run", goal]

        self.result = subprocess.run(
            cmd,
            capture_output=True,
            timeout=timeout,
        )
        return self.result

    def succeeded(self):
        return self.result and self.result.returncode == 0

    def stdout_contains(self, text):
        return self.result and text in self.result.stdout.decode()

    def stderr_contains(self, text):
        return self.result and text in self.result.stderr.decode()


def session_export_prefix(a0_dir, prefix):
    """Export a session using an 8-char prefix, return list of message dicts."""
    result = subprocess.run(
        [A0_BIN, "--a0-dir", a0_dir, "session", "export", "--session-id", prefix],
        capture_output=True, timeout=15
    )
    if result.returncode != 0:
        return []
    lines = result.stdout.decode().strip().split("\n")
    msgs = []
    for line in lines:
        if not line.strip():
            continue
        try:
            msgs.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return msgs
