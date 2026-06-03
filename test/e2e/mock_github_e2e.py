#!/usr/bin/env python3
"""Stateful mock DeepSeek API for github skill E2E test.

Guides the agent through a multi-turn issue creation + board setup workflow.
The mock detects whether the request is a tools_for_prompt analysis call
(no 'tools' in payload) or a forked-loop inference call (has 'tools').

Usage:
  python3 mock_github_e2e.py <port> '<json-config>'
"""

import http.server
import json
import re
import sys

CONFIG = {}


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


_TOOLS = [
    {"name": "issue_create", "schema": {
        "type": "object",
        "properties": {
            "repo": {"type": "string", "description": "Repository"},
            "title": {"type": "string", "description": "Issue title"},
            "label": {"type": "string", "description": "Labels"},
            "body": {"type": "string", "description": "Issue body"}
        },
        "required": ["repo", "title"]
    }},
    {"name": "issue_edit", "schema": {
        "type": "object",
        "properties": {
            "_": {"type": "number", "description": "Issue number"},
            "repo": {"type": "string", "description": "Repository"},
            "milestone": {"type": "string", "description": "Milestone"}
        },
        "required": ["_", "repo"]
    }},
    {"name": "graphql", "schema": {
        "type": "object",
        "properties": {
            "field": {"type": "string", "description": "GraphQL query field"},
            "jq": {"type": "string", "description": "jq filter"}
        },
        "required": ["field"]
    }}
]

_ANALYSIS_CONTENT = json.dumps({
    "intent": "Create a github issue and set up its project board fields",
    "plan": (
        "1. Create the issue with issue_create\n"
        "2. Set milestone with issue_edit\n"
        "3. Get GraphQL node ID\n"
        "4. Add to project board via graphql\n"
        "5. Set Phase field via graphql\n"
        "6. Set Status field via graphql\n"
        "7. Set Size field via graphql"
    ),
    "tools": _TOOLS
})

TOOLS_FOR_PROMPT_RESPONSE = {
    "id": "mock-analysis",
    "choices": [{"index": 0, "message": {"role": "assistant", "content": _ANALYSIS_CONTENT}}]
}


class MultiTurnHandler(http.server.BaseHTTPRequestHandler):
    turn = 0
    created_issue_num = None
    node_id = None
    item_id = None

    def do_POST(self):
        content_length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(content_length).decode() if content_length else "{}"
        payload = json.loads(raw) if raw else {}

        cfg = CONFIG
        has_tools = bool(payload.get("tools"))
        messages = payload.get("messages", [])

        # --- tools_for_prompt analysis call (no tools array) ---
        if not has_tools:
            # Return recommended tools for the workflow
            self._respond(TOOLS_FOR_PROMPT_RESPONSE)
            return

        # --- Forked-loop call (has tools array) ---
        # Count tool result messages to determine current turn
        tool_results = [m for m in messages if m.get("role") == "tool"]
        self.__class__.turn = len(tool_results)

        for m in tool_results:
            self._ingest_tool_result(m)

        turn = self.__class__.turn
        tool_calls = None

        if turn == 0:
            args = {"repo": "opensassi/a0", "title": cfg["title"], "body": cfg["body"]}
            if cfg.get("labels"):
                args["label"] = cfg["labels"]
            tool_calls = [build_tool_call("issue_create", args, "call_create")]
        elif turn == 1:
            num = self.__class__.created_issue_num or cfg.get("epic_number", 1)
            tool_calls = [build_tool_call("issue_edit", {
                "_": num, "repo": "opensassi/a0", "milestone": cfg["milestone"]
            }, "call_milestone")]
        elif turn == 2:
            num = self.__class__.created_issue_num or cfg.get("epic_number", 1)
            q = f'query {{ repository(owner:"opensassi", name:"a0") {{ issue(number:{num}) {{ id }} }} }}'
            tool_calls = [build_tool_call("graphql", {
                "field": f"query={q}",
                "jq": ".data.repository.issue.id"
            }, "call_nodeid")]
        elif turn == 3:
            nid = self.__class__.node_id or cfg.get("node_id", "")
            q = f'mutation {{ addProjectV2ItemById(input: {{ projectId: "{cfg["project_id"]}" contentId: "{nid}" }}) {{ item {{ id }} }} }}'
            tool_calls = [build_tool_call("graphql", {
                "field": f"query={q}",
                "jq": ".data.addProjectV2ItemById.item.id"
            }, "call_addtoproj")]
        elif turn == 4:
            iid = self.__class__.item_id or cfg.get("item_id", "")
            tool_calls = [build_tool_call("graphql", {
                "field": f'query=mutation {{ updateProjectV2ItemFieldValue(input: {{ projectId: "{cfg["project_id"]}" itemId: "{iid}" fieldId: "{cfg["phase_field_id"]}" value: {{ singleSelectOptionId: "{cfg["phase_option_id"]}" }} }}) {{ projectV2Item {{ id }} }} }}'
            }, "call_phase")]
        elif turn == 5:
            iid = self.__class__.item_id or cfg.get("item_id", "")
            tool_calls = [build_tool_call("graphql", {
                "field": f'query=mutation {{ updateProjectV2ItemFieldValue(input: {{ projectId: "{cfg["project_id"]}" itemId: "{iid}" fieldId: "{cfg["status_field_id"]}" value: {{ singleSelectOptionId: "f75ad846" }} }}) {{ projectV2Item {{ id }} }} }}'
            }, "call_status")]
        elif turn == 6:
            iid = self.__class__.item_id or cfg.get("item_id", "")
            tool_calls = [build_tool_call("graphql", {
                "field": f'query=mutation {{ updateProjectV2ItemFieldValue(input: {{ projectId: "{cfg["project_id"]}" itemId: "{iid}" fieldId: "{cfg["size_field_id"]}" value: {{ singleSelectOptionId: "{cfg["size_option_id"]}" }} }}) {{ projectV2Item {{ id }} }} }}'
            }, "call_size")]

        if tool_calls:
            self._respond(tool_call_response(tool_calls, turn))
        else:
            self._respond(content_response("All steps completed.", turn))

    def _ingest_tool_result(self, msg):
        content = msg.get("content", "")
        cid = msg.get("tool_call_id", "")

        if "call_create" in cid:
            m = re.search(r'issues/(\d+)', content)
            if m:
                self.__class__.created_issue_num = int(m.group(1))
                print(f"[MOCK] issue #{m.group(1)}", file=sys.stderr)

        if "call_nodeid" in cid:
            val = content.strip().strip('"')
            if val:
                self.__class__.node_id = val
                print(f"[MOCK] node_id OK", file=sys.stderr)

        if "call_addtoproj" in cid:
            val = content.strip().strip('"')
            if val:
                self.__class__.item_id = val
                print(f"[MOCK] item_id OK", file=sys.stderr)

    def _respond(self, data):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(data).encode())

    def do_GET(self):
        self._respond({"status": "ok"})

    def log_message(self, fmt, *args):
        print(f"[MOCK] turn={self.__class__.turn} {args}", file=sys.stderr)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 18082
    if len(sys.argv) > 2:
        CONFIG.update(json.loads(sys.argv[2]))
    server = http.server.HTTPServer(("127.0.0.1", port), MultiTurnHandler)
    print(f"Mock GitHub E2E on http://127.0.0.1:{port}", file=sys.stderr)
    server.serve_forever()
