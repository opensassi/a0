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
import time
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


# ---------------------------------------------------------------------------
# SSE helpers for --stream mode
# ---------------------------------------------------------------------------

def sse_data(delta=None, finish_reason=None):
    """Build an SSE data line for a streaming chunk.

    finish_reason is placed at the choice level (not inside delta)
    per the OpenAI streaming spec.
    """
    if delta is None:
        delta = {}
    choice = {"index": 0, "delta": delta}
    if finish_reason:
        choice["finish_reason"] = finish_reason
    obj = {"choices": [choice]}
    return "data: " + json.dumps(obj) + "\n\n"

def sse_done():
    return "data: [DONE]\n\n"

def sse_tool_call_chunks(tool_calls_list):
    """Yield SSE chunks for a tool-calls response, splitting args across chunks.

    OpenAI streaming format:
      1. delta with role + tool_call (id + name + empty args)
      2. delta with tool_call arguments only
      3. choice with empty delta + finish_reason "tool_calls"
      4. [DONE]
    """
    for idx, tc in enumerate(tool_calls_list):
        name = tc["function"]["name"]
        args = tc["function"]["arguments"]
        call_id = tc["id"]
        # Chunk 1: role + id + name, args empty
        yield sse_data(delta={
            "role": "assistant",
            "tool_calls": [
                {"index": idx, "id": call_id, "type": "function",
                 "function": {"name": name, "arguments": ""}}
            ]
        })
        # Chunk 2: arguments only
        yield sse_data(delta={
            "tool_calls": [
                {"index": idx, "function": {"arguments": args}}
            ]
        })
    # Finish: empty delta + finish_reason at choice level
    yield sse_data(delta={}, finish_reason="tool_calls")
    yield sse_done()

def sse_content_chunks(text):
    """Yield SSE chunks for a plain text response."""
    # Role chunk
    yield sse_data(delta={"role": "assistant", "content": ""})
    # Split text into ~10 char tokens for realistic streaming
    pos = 0
    while pos < len(text):
        chunk = text[pos:pos+10]
        yield sse_data(delta={"content": chunk})
        pos += 10
    # Finish: empty delta + finish_reason at choice level
    yield sse_data(delta={}, finish_reason="stop")
    yield sse_done()


class ScenarioRunner:
    """Loads and runs a multi-turn scenario from a JSON file.

    Scenario format:
      {
        "name": "my_scenario",
        "analysis": {"content": "..."} | None,
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

    def next_turn(self, messages, has_tools=True):
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

    def next_turn_streaming(self, messages, has_tools=True):
        """Like next_turn but yields SSE chunk strings."""
        response = self.next_turn(messages, has_tools)
        choices = response.get("choices", [])
        if not choices:
            yield sse_done()
            return
        choice = choices[0]
        msg = choice.get("message", {})
        if msg.get("tool_calls"):
            yield from sse_tool_call_chunks(msg["tool_calls"])
        elif msg.get("content"):
            yield from sse_content_chunks(msg["content"])
        else:
            yield sse_done()


class MockHandler(http.server.BaseHTTPRequestHandler):
    scenario_runner = None
    stream_mode = False
    chunk_delay = 0.0

    def do_POST(self):
        content_length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(content_length).decode() if content_length else "{}"
        payload = json.loads(raw) if raw else {}
        messages = payload.get("messages", [])

        if self.__class__.stream_mode:
            self._respond_streaming(messages, payload)
        else:
            if self.__class__.scenario_runner:
                response = self.__class__.scenario_runner.next_turn(messages)
            else:
                response = self._legacy_respond(payload, messages)
            self._respond_json(response)

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

        # If the last message is a user message, this is a new goal — tool messages
        # in the conversation are from prior goals and should be ignored.
        if messages and messages[-1].get("role") == "user":
            has_tool_role = False
        last_is_user = False
        for i, msg in enumerate(messages):
            role = msg.get("role", "")
            if role == "user":
                user_text = msg.get("content", "")
                last_is_user = True
            elif role == "system":
                system_text = msg.get("content", "")
            elif role == "tool":
                if last_is_user:
                    pass  # tool immediately following user — part of current goal
                else:
                    has_tool_role = True
                last_is_user = False

        # If the last message is a user message, treat it as a new goal
        if last_is_user:
            has_tool_role = False
 
        if "Analyze the user's request" in system_text:
            return LEGACY_RESPONSES["tools_for_prompt"]
        elif not has_tool_role:
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

    def _respond_streaming(self, messages, payload):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self.end_headers()
        delay = self.__class__.chunk_delay
        try:
            if self.__class__.scenario_runner:
                for chunk in self.__class__.scenario_runner.next_turn_streaming(messages):
                    self.wfile.write(chunk.encode())
                    self.wfile.flush()
                    if delay > 0:
                        time.sleep(delay)
            else:
                response = self._legacy_respond(payload, messages)
                choices = response.get("choices", [])
                if choices and choices[0].get("message", {}).get("tool_calls"):
                    for chunk in sse_tool_call_chunks(choices[0]["message"]["tool_calls"]):
                        self.wfile.write(chunk.encode())
                        self.wfile.flush()
                        if delay > 0:
                            time.sleep(delay)
                else:
                    for chunk in sse_content_chunks("Tool executed successfully"):
                        self.wfile.write(chunk.encode())
                        self.wfile.flush()
                        if delay > 0:
                            time.sleep(delay)
        except (BrokenPipeError, ConnectionError):
            pass

    def _respond_json(self, data):
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
    parser.add_argument("--chunk-delay", type=float, default=0.0,
                        help="Seconds to sleep between SSE chunks (simulate latency)")
    args = parser.parse_args()

    if args.scenario:
        runner = ScenarioRunner(args.scenario)
        MockHandler.scenario_runner = runner
        print(f"[MOCK] Loaded scenario: {runner.scenario.get('name', args.scenario)}", file=sys.stderr)
    if args.stream:
        MockHandler.stream_mode = True
        print(f"[MOCK] Streaming SSE mode enabled", file=sys.stderr)
    if args.chunk_delay > 0:
        MockHandler.chunk_delay = args.chunk_delay
        print(f"[MOCK] Chunk delay: {args.chunk_delay}s", file=sys.stderr)

    server = http.server.HTTPServer(("127.0.0.1", args.port), MockHandler)
    print(f"[MOCK] DeepSeek API on http://127.0.0.1:{args.port}", file=sys.stderr)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("[MOCK] Shutting down", file=sys.stderr)
        server.server_close()

if __name__ == "__main__":
    main()
