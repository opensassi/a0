"""Crash and stress tests — run a0 binary subprocess with various flags/goals."""

import os
import sys
import subprocess
import tempfile

sys.path.insert(0, os.path.dirname(__file__))
from conftest import A0_BIN, SKILLS_DIR, MockServer, AgentSubprocess


def test_binary_exists():
    assert os.path.exists(A0_BIN), f"a0 binary not found at {A0_BIN}"
    assert os.access(A0_BIN, os.X_OK), "a0 binary is not executable"


def test_help_flag_no_crash():
    result = subprocess.run([A0_BIN, "--help"], capture_output=True, timeout=10)
    assert result.returncode == 0, f"Exit {result.returncode}: {result.stderr.decode()}"
    assert "Usage" in result.stdout.decode() or "Usage" in result.stderr.decode()


def test_run_minimal_no_crash():
    """a0 run with a basic prompt should not crash even without mock server."""
    with tempfile.TemporaryDirectory() as tmpdir:
        result = subprocess.run(
            [A0_BIN, "--a0-dir", tmpdir, "--skills-dir", SKILLS_DIR,
             "--no-docker", "--no-b1",
             "--mock-api", "http://127.0.0.1:18999", "run", "hello"],
            capture_output=True, timeout=15
        )
        assert result.returncode != -11, f"Segfault! stderr: {result.stderr.decode()}"
        assert not (
            result.returncode == -6 or result.returncode == -11
        ), f"Process killed by signal {abs(result.returncode)}"


def test_run_with_mock_no_crash():
    """Full path: mock server + goal processing, verify no crash and valid output."""
    with MockServer() as server:
        agent = AgentSubprocess(mock_server=server)
        result = agent.run("find log files")
        assert result.returncode == 0, (
            f"Exit {result.returncode}. stdout: {result.stdout.decode()[:200]}. "
            f"stderr: {result.stderr.decode()[:200]}"
        )
        stdout = result.stdout.decode()
        assert len(stdout) > 0, "Expected non-empty output"
        import json
        try:
            parsed = json.loads(stdout.strip())
            assert parsed is not None, "Expected non-null JSON"
        except json.JSONDecodeError:
            assert False, f"Expected valid JSON, got: {stdout[:200]}"


def test_run_session_flags():
    """--resume and other session flags should not crash."""
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


def test_run_with_skill_arg():
    """--skill-arg flag should not cause crash."""
    with MockServer() as server:
        agent = AgentSubprocess(mock_server=server, extra_args=["--skill-arg", "test-key=test-val"])
        result = agent.run("find files")
        assert result.returncode != -11, "Segfault!"


def test_consecutive_goals_no_crash():
    """Run multiple goals in sequence using the same a0-dir, check no resource leak."""
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
                    f"Goal {i} failed: exit {result.returncode}. "
                    f"stderr: {result.stderr.decode()[:200]}"
                )


def test_max_parallel_flag():
    """--max-parallel should be accepted without crash."""
    with MockServer() as server:
        agent = AgentSubprocess(mock_server=server, extra_args=["--max-parallel", "2"])
        result = agent.run("find files")
        assert result.returncode != -11, "Segfault!"


def test_cli_flag_combinations():
    """Various flag combinations should not crash."""
    combos = [
        [],
        ["--no-docker"],
        ["--no-docker", "--no-b1"],
        ["--no-docker", "--no-b1", "--max-parallel", "8"],
    ]
    with MockServer() as server:
        for combo in combos:
            with tempfile.TemporaryDirectory() as tmpdir:
                result = subprocess.run(
                    [A0_BIN, "--a0-dir", tmpdir] + combo +
                    ["--mock-api", f"http://127.0.0.1:{server.port}", "run", "test"],
                    capture_output=True, timeout=15
                )
                assert result.returncode != -11, f"Segfault with flags: {combo}"


def test_stress_rapid_goals():
    """Rapid consecutive goal submissions to stress-test resource cleanup."""
    with MockServer() as server:
        with tempfile.TemporaryDirectory() as tmpdir:
            for i in range(10):
                result = subprocess.run(
                    [A0_BIN, "--a0-dir", tmpdir, "--no-docker", "--no-b1",
                     "--mock-api", f"http://127.0.0.1:{server.port}",
                     "run", f"quick goal {i}"],
                    capture_output=True, timeout=15
                )
                if result.returncode != 0:
                    print(f"  Goal {i} failed (exit {result.returncode}), continuing...", file=sys.stderr)
            result = subprocess.run(
                [A0_BIN, "--a0-dir", tmpdir, "--no-docker", "--no-b1",
                 "--mock-api", f"http://127.0.0.1:{server.port}",
                 "run", "final goal"],
                capture_output=True, timeout=15
            )
            assert result.returncode == 0, (
                f"Final goal failed after 10 rapid submissions. "
                f"stderr: {result.stderr.decode()[:200]}"
            )
