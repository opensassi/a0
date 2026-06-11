#!/usr/bin/env python3
"""Count tokens in all session .md files using tiktoken cl100k_base.

Usage:
    python3 sessions/count_md_tokens.py
"""
import os, glob

SESSIONS_DIR = os.path.dirname(os.path.abspath(__file__))
EXCLUDE = {"session-evaluation-prompt.md"}


def main():
    import tiktoken

    enc = tiktoken.get_encoding("cl100k_base")

    md_files = sorted(
        f for f in glob.glob(os.path.join(SESSIONS_DIR, "*.md"))
        if os.path.basename(f) not in EXCLUDE
    )

    if not md_files:
        print("No session .md files found.")
        return

    total = 0
    max_name = max(len(os.path.basename(f).replace(".md", "")) for f in md_files)

    for md_path in md_files:
        name = os.path.basename(md_path).replace(".md", "")
        with open(md_path, "r") as f:
            content = f.read()
        count = len(enc.encode(content))
        total += count
        print(f"  {name:<{max_name}}  {count:>8,} tokens")

    print(f"  {'─' * max_name}  ───────────")
    print(f"  {'Total':<{max_name}}  {total:>8,} tokens across {len(md_files)} files")


if __name__ == "__main__":
    main()
