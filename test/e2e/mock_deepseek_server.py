#!/usr/bin/env python3
"""Mock DeepSeek API server for E2E testing.

Supports:
  - Keyword-based responses (legacy mode, no args)
  - Scenario-driven multi-turn conversations (--scenario <path>)
  - GPT-style streaming simulation (--stream)

Usage:
  python3 mock_deepseek_server.py [port] [--scenario <path>] [--stream]
"""

import http.server
import json
import os
import re
import sys
import argparse
from pathlib import Path

FIXTURES_DIR = os.path.join(os.path.dirname(__file__), "fixtures")

def read_fixture(name):
    path = os.path.join(FIXTURES_DIR, name)
    with open(path) as f:
        return json.load(f)

LEGACY_RESPONSES = {
    "tool": read_fixture("infer_tool_response.json"),
    "skill": read_fixture("infer_skill_response.json"),
    "files": read_fixture("find_related_files_response.json"),
    "tool_calls": read_fixture("tool_calls_response.json"),
    "tools_for_prompt": read_fixture("tools_for_prompt_response.json"),
    "tool_result": {"id":"mock-completion-5","choices":[{"index":0,"message":{"role":"assistant","content":"Tool executed successfully"}}]},
}

def build_tool_call(tool_name, args_dict, call_id=None):
    return {
        "id": call_id or f"call_{tool_name}",
        "type": "function",
        "function": {"name": tool_name, "arguments": json.dumps(args_dict)}
    }

def tool_call_response(tool_calls_list, turn):
    return {
        "id": f"mock-turn-{turn}",
        "choices": [{"index": 0, "message": {
            "role": "assistant", "content": None,
            "tool_calls": tool_calls_list
        }}]
    }

def content_response(text, turn):
    return {
        "id": f"mock-done-{turn}",
        "choices": [{"index": 0, "message": {
            "role": "assistant", "content": text
        }}]
    }


class ScenarioRunner:
    """Loads and runs a multi-turn scenario from a JSON file.

    Scenario format:
      {
        "name": "my_scenario",
        "analysis": {"content": "..."} | None,  // optional, default=tools_for_prompt
        "turns": [
          {"tool_calls": [{"name":"bash", "arguments":{"command":"..."}}]},
          {"content": "Final answer."}
        ]
      }
    """

    def __init__(self, scenario_path):
        with open(scenario_path) as f:
            self.scenario = json.load(f)
        self.turn = 0
        self.analysis_response = None
        analysis = self.scenario.get("analysis")
        if analysis and "tool_calls" in analysis:
            self.analysis_response = {
                "id": "mock-analysis-custom",
                "choices": [{"index": 0, "message": {
                    "role": "assistant",
                    "content": json.dumps({
                        "intent": analysis.get("intent", ""),
                        "plan": analysis.get("plan", ""),
                        "tools": analysis.get("tool_calls", [])
                    })
                }}]
            }
        if not self.analysis_response:
            self.analysis_response = LEGACY_RESPONSES["tools_for_prompt"]

    def next_turn(self, messages, has_tools):
        if not has_tools:
            return self.analysis_response
        tool_results = [m for m in messages if m.get("role") == "tool"]
        self.turn = len(tool_results)
        turns = self.scenario.get("turns", [])
        if self.turn >= len(turns):
            return content_response("Done.", self.turn)
        step = turns[self.turn]
        if "tool_calls" in step:
            calls = []
            for tc in step["tool_calls"]:
                calls.append(build_tool_call(tc["name"], tc.get("arguments", {}), f"call_{tc['name']}_{self.turn}"))
            return tool_call_response(calls, self.turn)
        if "content" in step:
            return content_response(step["content"], self.turn)
        return content_response("Done.", self.turn)


class MockHandler(http.server.BaseHTTPRequestHandler):
    scenario_runner = None

    def do_POST(self):
        content_length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(content_length).decode() if content_length else "{}"
        payload = json.loads(raw) if raw else {}
        messages = payload.get("messages", [])

        if self.__class__.scenario_runner:
            has_tools = bool(payload.get("tools"))
            response = self.__class__.scenario_runner.next_turn(messages, has_tools)
        else:
            response = self._legacy_respond(payload, messages)

        self._respond(response)

    def _legacy_respond(self, payload, messages):
        user_text = ""
        system_text = ""
        has_tool_role = False
        for msg in messages:
            role = msg.get("role", "")
            if role == "user":
                user_text = msg.get("content", "")
            elif role == "system":
                system_text = msg.get("content", "")
            elif role == "tool":
                has_tool_role = True

        if "Analyze the user's request" in system_text:
            return LEGACY_RESPONSES["tools_for_prompt"]
        elif not has_tool_role and payload.get("tools"):
            return LEGACY_RESPONSES["tool_calls"]
        elif has_tool_role:
            return LEGACY_RESPONSES["tool_result"]
        elif "tool generator" in system_text.lower():
            return LEGACY_RESPONSES["tool"]
        elif "skill generator" in system_text.lower():
            return LEGACY_RESPONSES["skill"]
        elif "file" in user_text.lower() or "find" in user_text.lower():
            return LEGACY_RESPONSES["files"]
        else:
            return LEGACY_RESPONSES["files"]

    def _respond(self, data):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(data).encode())

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps({"status": "ok"}).encode())

    def log_message(self, format, *args):
        print(f"[MOCK] {args[0]} {args[1]} {args[2]}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description="Mock DeepSeek API server")
    parser.add_argument("port", nargs="?", type=int, default=18080, help="Port to listen on")
    parser.add_argument("--scenario", type=str, help="Path to scenario JSON file")
    parser.add_argument("--stream", action="store_true", help="Simulate streaming SSE responses")
    args = parser.parse_args()

    if args.scenario:
        runner = ScenarioRunner(args.scenario)
        MockHandler.scenario_runner = runner
        print(f"[MOCK] Loaded scenario: {runner.scenario.get('name', args.scenario)}", file=sys.stderr)

    server = http.server.HTTPServer(("127.0.0.1", args.port), MockHandler)
    print(f"[MOCK] DeepSeek API on http://127.0.0.1:{args.port}", file=sys.stderr)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("[MOCK] Shutting down", file=sys.stderr)
        server.server_close()

if __name__ == "__main__":
    main()
