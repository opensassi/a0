#!/usr/bin/env python3
"""Backfill .stats.json and stats summary in .md for old sessions.

Scans the sessions directory for archives that lack a sibling .stats.json,
processes them in chronological order (by filename), and appends the
Extracted Session Stats section to the corresponding .md file.

Usage:
    python3 sessions/backfill_stats.py              # backfill
    python3 sessions/backfill_stats.py --dry-run    # preview only
"""
import os, sys, json, glob

SESSIONS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SESSIONS_DIR)
from session_stats import analyze


def find_archives(sessions_dir):
    """Return sorted list of .json.bz2 and .jsonl.gz archives."""
    archives = []
    for pat in ("*.json.bz2", "*.jsonl.gz"):
        archives.extend(glob.glob(os.path.join(sessions_dir, pat)))
    return sorted(archives)


def base_of(archive_path):
    """Strip .json.bz2 or .jsonl.gz suffix."""
    return archive_path.rsplit(".", 2)[0]


def find_md(archive_path):
    """Locate the .md for *archive_path*, handling name mismatches."""
    base = base_of(archive_path)
    md_path = base + ".md"
    if os.path.exists(md_path):
        return md_path
    # Fallback: the archive may carry a hex suffix that the .md lacks.
    # Look for an .md whose name is a prefix of the archive base.
    base_name = os.path.basename(base)
    for f in os.listdir(SESSIONS_DIR):
        if f.endswith(".md"):
            stem = f[:-3]
            if base_name.startswith(stem) and len(base_name) > len(stem):
                return os.path.join(SESSIONS_DIR, f)
    return None


def needs_backfill(archive_path):
    """Check if this archive still needs its .stats.json created."""
    stats_path = base_of(archive_path) + ".stats.json"
    return not os.path.exists(stats_path)


def append_stats_to_md(md_path, markdown):
    """Insert markdown before trailing --- if present, else append."""
    with open(md_path, "r") as f:
        content = f.read()

    if content.rstrip().endswith("\n---"):
        idx = content.rstrip().rfind("\n---")
        insert_at = content.rfind("\n", 0, idx) + 1
        new_content = content[:insert_at] + markdown + "\n" + content[insert_at:]
    else:
        new_content = content.rstrip() + "\n" + markdown + "\n"

    with open(md_path, "w") as f:
        f.write(new_content)


def main():
    dry_run = "--dry-run" in sys.argv

    archives = find_archives(SESSIONS_DIR)
    to_process = [a for a in archives if needs_backfill(a)]

    if not to_process:
        print("All sessions already backfilled.")
        return

    skipped = 0
    processed = 0
    errors = 0

    for archive_path in to_process:
        base = base_of(archive_path)
        md_path = find_md(archive_path)
        stats_path = base + ".stats.json"

        if not md_path:
            print(f"  SKIP: {os.path.basename(base)} — no .md")
            skipped += 1
            continue

        if dry_run:
            print(f"  WOULD PROCESS: {os.path.basename(base)}")
            processed += 1
            continue

        try:
            markdown, stats = analyze(archive_path)

            with open(stats_path, "w") as f:
                json.dump(stats, f, indent=2)

            append_stats_to_md(md_path, markdown)

            print(f"  OK: {os.path.basename(base)}")
            processed += 1
        except Exception as e:
            print(f"  ERR: {os.path.basename(base)} — {e}")
            errors += 1

    verb = "Would process" if dry_run else "Processed"
    print(f"\n{verb}: {processed}  Skipped: {skipped}  Errors: {errors}")


if __name__ == "__main__":
    main()
