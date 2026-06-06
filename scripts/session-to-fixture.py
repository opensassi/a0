#!/usr/bin/env python3
"""Convert a0 session export JSONL to mock scenario fixture.

Usage:
  a0 session export --session-id=<slug> | python3 scripts/session-to-fixture.py \
      --name "short 3-5 word description" \
      > test/e2e/fixtures/user_name.json

Tool output from the original session is saved to fixtures/<name>/<sha256>.txt
and tool calls in the scenario are rewritten to read() those files, making
all tool execution deterministic during replay.
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


def slugify(text):
    text = text.lower().strip()
    text = re.sub(r'[^a-z0-9]+', '_', text)
    text = text.strip('_')
    text = re.sub(r'_+', '_', text)
    return text


def convert(jsonl_lines, name=None, description=None, output_path=None):
    slug = slugify(name) if name else "exported"
    messages = []

    # Parse all messages first
    for line in jsonl_lines:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue
        sub_id = msg.get("sub_session_id", 0)
        if isinstance(sub_id, (int, float)) and sub_id < 0:
            continue
        messages.append(msg)

    # --- Pass 1: collect tool outputs, save fixture files ---
    output_map = {}  # tool_call_id → sha256
    if output_path:
        out_file = Path(output_path).resolve()
        fixture_dir = out_file.parent / out_file.stem
        fixture_dir.mkdir(parents=True, exist_ok=True)
    else:
        fixture_dir = None

    for msg in messages:
        if msg.get("role") == "tool":
            tc_id = msg.get("tool_call_id", "")
            content = msg.get("content", "")
            if not tc_id or not content:
                continue
            if isinstance(content, (dict, list)):
                content = json.dumps(content)
            else:
                content = str(content)
            sha = hashlib.sha256(content.encode()).hexdigest()
            output_map[tc_id] = sha
            if fixture_dir:
                fixture_path = fixture_dir / (sha + ".txt")
                if not fixture_path.exists():
                    fixture_path.write_text(content + "\n")
                    print(f"  wrote {fixture_path}", file=sys.stderr)

    # --- Pass 2: build turns, rewriting tool_calls to read(fixture_file) ---
    turns = []
    for msg in messages:
        role = msg.get("role", "")
        if role == "assistant":
            tool_calls = msg.get("tool_calls")
            content = msg.get("content", "")

            if tool_calls:
                simplified = []
                for tc in tool_calls:
                    tc_id = tc.get("id", "")
                    fn = tc.get("function", tc)
                    fn_name = fn.get("name", "")
                    fn_args = fn.get("arguments", {})
                    if isinstance(fn_args, str):
                        try:
                            fn_args = json.loads(fn_args)
                        except json.JSONDecodeError:
                            fn_args = {"command": fn_args}

                    if tc_id in output_map:
                        sha = output_map[tc_id]
                        if fixture_dir:
                            read_path = str((fixture_dir / (sha + ".txt")).resolve())
                        else:
                            read_path = sha + ".txt"
                        fn_name = "read"
                        fn_args = {"file_path": read_path}

                    simplified.append({"name": fn_name, "arguments": fn_args})
                turns.append({"tool_calls": simplified})

            elif content:
                turns.append({"content": content})

    return {
        "name": slug,
        "description": description or name or "Exported session converted to mock fixture",
        "analysis": None,
        "turns": turns,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Convert a0 session export JSONL to mock scenario fixture"
    )
    parser.add_argument("--name", type=str, required=True,
                        help="Short 3-5 word description (e.g. 'word wrapping long output')")
    parser.add_argument("--description", type=str,
                        help="Detailed description (defaults to --name)")
    parser.add_argument("--output", type=str,
                        help="Output file path")

    args = parser.parse_args()

    if sys.stdin.isatty():
        print("Usage: a0 session export --session-id=<slug> | python3 scripts/session-to-fixture.py --name ...",
              file=sys.stderr)
        sys.exit(1)

    jsonl_lines = sys.stdin.readlines()
    scenario = convert(jsonl_lines, name=args.name,
                       description=args.description, output_path=args.output)

    output_json = json.dumps(scenario, indent=2) + "\n"

    if args.output:
        Path(args.output).write_text(output_json)
        print(f"Wrote {args.output}", file=sys.stderr)
    else:
        sys.stdout.write(output_json)


if __name__ == "__main__":
    main()
