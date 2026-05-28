#!/usr/bin/env python3
"""Mock DeepSeek API server for E2E testing."""

import http.server
import json
import os
import sys

FIXTURES_DIR = os.path.join(os.path.dirname(__file__), "fixtures")

RESPONSES = {
    "tool": json.load(open(os.path.join(FIXTURES_DIR, "infer_tool_response.json"))),
    "skill": json.load(open(os.path.join(FIXTURES_DIR, "infer_skill_response.json"))),
    "files": json.load(open(os.path.join(FIXTURES_DIR, "find_related_files_response.json"))),
}


class MockHandler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length).decode() if content_length else "{}"

        payload = json.loads(body) if body else {}
        messages = payload.get("messages", [])
        user_text = ""
        for msg in messages:
            if msg.get("role") == "user":
                user_text = msg.get("content", "")
                break

        system_text = ""
        for msg in messages:
            if msg.get("role") == "system":
                system_text = msg.get("content", "")
                break

        if "tool generator" in system_text.lower():
            response = RESPONSES["tool"]
        elif "skill generator" in system_text.lower():
            response = RESPONSES["skill"]
        elif "file" in user_text.lower() or "find" in user_text.lower():
            response = RESPONSES["files"]
        else:
            response = RESPONSES["files"]

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(response).encode())

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps({"status": "ok"}).encode())

    def log_message(self, format, *args):
        sys.stderr.write(f"[MOCK] {args[0]} {args[1]} {args[2]}\n")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 18080
    server = http.server.HTTPServer(("127.0.0.1", port), MockHandler)
    print(f"Mock DeepSeek API running on http://127.0.0.1:{port}")
    server.serve_forever()
