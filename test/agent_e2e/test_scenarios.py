"""Scenario-based agent tests — run real a0 binary against scenario-driven mock server."""

import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from conftest import MockServer, AgentSubprocess, SCENARIOS_DIR, A0_BIN


def _load_scenario(name):
    path = os.path.join(SCENARIOS_DIR, name)
    with open(path) as f:
        return json.load(f)


def _run_scenario(scenario_name, goal="find log files"):
    scenario_path = os.path.join(SCENARIOS_DIR, scenario_name)
    scenario = _load_scenario(scenario_name)

    with MockServer(scenario=scenario_path) as server:
        agent = AgentSubprocess(mock_server=server)
        result = agent.run(goal)
        return result, scenario


def test_simple_tool_call():
    """Agent processes a goal, calls a tool, returns valid JSON output."""
    result, scenario = _run_scenario("simple_tool_call.json", "list files in /tmp")
    assert result.returncode == 0, (
        f"Exit {result.returncode}. stdout: {result.stdout.decode()[:300]}"
    )
    stdout = result.stdout.decode().strip()
    import json
    try:
        parsed = json.loads(stdout)
        assert parsed is not None, "Expected non-null response"
    except json.JSONDecodeError:
        assert False, f"Expected valid JSON, got: {stdout[:200]}"


def test_multi_turn_workflow():
    """Multi-turn scenario: agent calls tools twice, returns final answer."""
    result, scenario = _run_scenario("multi_turn_workflow.json", "find log files and check for errors")
    assert result.returncode == 0, (
        f"Exit {result.returncode}. stdout: {result.stdout.decode()[:300]}"
    )
    stdout = result.stdout.decode().strip()
    assert len(stdout) > 0, "Expected non-empty output"


def test_tool_error_handling():
    """Tool returns error, agent handles it gracefully."""
    result, scenario = _run_scenario("tool_error.json", "read the file at /nonexistent/file.txt")
    assert result.returncode == 0 or result.returncode == 1, (
        f"Exit {result.returncode} — agent may return error gracefully"
    )


def test_all_scenarios_load():
    """Verify all scenario JSON files are valid."""
    for fname in sorted(os.listdir(SCENARIOS_DIR)):
        if not fname.endswith(".json"):
            continue
        path = os.path.join(SCENARIOS_DIR, fname)
        try:
            with open(path) as f:
                data = json.load(f)
            assert "name" in data, f"{fname}: missing 'name'"
            assert "turns" in data, f"{fname}: missing 'turns'"
            assert len(data["turns"]) > 0, f"{fname}: empty turns"
            print(f"  OK: {data['name']} ({len(data['turns'])} turns)")
        except json.JSONDecodeError as e:
            assert False, f"{fname}: invalid JSON — {e}"


def test_clipboard_wrappers_exist():
    """Verify mock clipboard wrappers exist and are executable."""
    for wrapper in ["xclip", "wl-copy"]:
        path = os.path.join(os.path.dirname(__file__), "mock_wrappers", wrapper)
        assert os.path.exists(path), f"Mock wrapper not found: {path}"
        assert os.access(path, os.X_OK), f"Mock wrapper not executable: {path}"
