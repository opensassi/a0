#!/usr/bin/env python3
"""Analyze an opencode session export, append markdown stats to its evaluation
.md, and save a structured .stats.json alongside.

Usage:
  python3 sessions/session_stats.py sessions/<archive>.json.bz2

Reads the bzip2-compressed JSON export, computes stats, appends an "Extracted
Session Stats" section to the sibling .md file (before any trailing "---"
marker if present), and writes a .stats.json file with the full data.
"""
import json, sys, os, bz2
from datetime import datetime, timezone
from collections import Counter


def analyze(path):
    """Run analysis on a session export, return (markdown, stats_dict)."""
    with bz2.open(path, "rt") as f:
        raw = json.load(f)
    data = raw.get("messages", [])

    user_msgs = asst_msgs = 0
    total_uc = total_uw = total_ac = total_aw = 0
    first_ts = last_ts = None

    tok_in = tok_out = tok_reason = tok_cr = tok_cw = 0
    session_cost = 0.0
    tool_names = Counter()
    tool_total = 0
    modes = Counter()
    finish_reasons = Counter()
    part_types = Counter()
    timeline = []

    for msg in data:
        info = msg.get("info", {})
        role = info.get("role", "")
        ts_ms = info.get("time", {}).get("created", 0)
        if ts_ms:
            t = datetime.fromtimestamp(ts_ms / 1000, tz=timezone.utc)
            if first_ts is None or t < first_ts:
                first_ts = t
            if last_ts is None or t > last_ts:
                last_ts = t
            timeline.append((ts_ms, role))

        content = ""
        for part in msg.get("parts", []):
            pt = part.get("type", "")
            part_types[pt] += 1
            if pt == "text":
                content += part.get("text", "")
            elif pt == "tool":
                tool_total += 1
                tn = part.get("tool", "")
                if tn:
                    tool_names[tn] += 1

        wc = len(content.split())
        cc = len(content)

        if role == "user":
            user_msgs += 1
            total_uc += cc
            total_uw += wc
        elif role == "assistant":
            asst_msgs += 1
            total_ac += cc
            total_aw += wc

        t = info.get("tokens", {})
        if isinstance(t, dict):
            tok_in += t.get("input", 0)
            tok_out += t.get("output", 0)
            tok_reason += t.get("reasoning", 0)
            c = t.get("cache", {})
            if isinstance(c, dict):
                tok_cr += c.get("read", 0)
                tok_cw += c.get("write", 0)
        cst = info.get("cost", None)
        if cst is not None:
            session_cost += float(cst)

        mode = info.get("mode", "")
        if mode:
            modes[mode] += 1
        finish = info.get("finish", "")
        if finish:
            finish_reasons[finish] += 1

    # Gap-based prompter active
    timeline.sort(key=lambda x: x[0])
    last_model_ts = None
    prompter_secs = 0
    gaps = []
    for ts, role in timeline:
        if role in ("assistant", "tool"):
            last_model_ts = ts
        elif role == "user" and last_model_ts is not None:
            gap_s = (ts - last_model_ts) / 1000
            capped = min(gap_s, 60)
            prompter_secs += capped
            gaps.append(gap_s)
            last_model_ts = ts

    prompter_min = prompter_secs / 60
    wall_min = ((last_ts - first_ts).total_seconds() / 60) if first_ts and last_ts else 0
    idle_min = wall_min - prompter_min

    input_total = tok_in + tok_cr
    cache_pct = (tok_cr / input_total * 100) if input_total > 0 else 0
    total_billed = tok_cr + tok_cw + tok_in + tok_out + tok_reason
    total_tc = sum(tool_names.values())

    # Gap distribution
    gap_dist = {}
    for lo, hi in [(0, 15), (15, 30), (30, 45), (45, 60), (60, 99999)]:
        label = f"{lo}-{hi}s" if hi < 99999 else f">{lo}s"
        cnt = sum(1 for g in gaps if lo <= g < hi)
        if cnt:
            gap_dist[label] = cnt

    # Build markdown
    lines = []
    lines.append("")
    lines.append("---")
    lines.append("## Extracted Session Stats")
    lines.append("")

    if first_ts and last_ts:
        dur = (last_ts - first_ts).total_seconds()
        lines.append(f"- **Duration:** {dur:.0f}s ({wall_min:.1f}m)")
        lines.append(f"  - First message: {first_ts.strftime('%H:%M:%S')}")
        lines.append(f"  - Last message:  {last_ts.strftime('%H:%M:%S')}")
    lines.append(f"- **Messages:** {len(data)} total ({user_msgs} user, {asst_msgs} assistant)")
    lines.append(f"- **Tool call parts:** {tool_total}")
    lines.append(f"- **Words:** {total_aw:,} assistant, {total_uw:,} user")
    lines.append("")
    lines.append("### Tokens & Cost")
    lines.append("")
    lines.append("| Metric | Value |")
    lines.append("|--------|-------|")
    lines.append(f"| Input Tokens — Total | {input_total:,} |")
    lines.append(f"| Input Tokens — Cached | {tok_cr:,} ({cache_pct:.1f}%) |")
    lines.append(f"| Input Tokens — Uncached | {tok_in:,} |")
    lines.append(f"| Output Tokens | {tok_out:,} |")
    lines.append(f"| Reasoning Tokens | {tok_reason:,} |")
    lines.append(f"| Total Billed | {total_billed:,} |")
    lines.append(f"| Cost | ${session_cost:.6f} |")
    lines.append("")
    lines.append("### Tool Usage")
    lines.append("")
    if tool_names:
        max_name = max(len(n) for n in tool_names)
        lines.append(f"| {'Tool':<{max_name}} | Calls | % |")
        lines.append(f"|{'-' * (max_name + 2)}-|-------|---|")
        for name, count in tool_names.most_common(20):
            pct = count / total_tc * 100
            lines.append(f"| {name:<{max_name}} | {count:5d} | {pct:5.1f}% |")
    lines.append("")
    lines.append("### Mode & Finish")
    lines.append("")
    total_mode = sum(modes.values())
    if total_mode:
        lines.append("| Mode | Count | % |")
        lines.append("|------|-------|---|")
        for mode, count in modes.most_common():
            pct = count / total_mode * 100
            lines.append(f"| {mode} | {count} | {pct:.1f}% |")
    lines.append("")
    total_fin = sum(finish_reasons.values())
    if total_fin:
        lines.append("| Finish Reason | Count | % |")
        lines.append("|---------------|-------|---|")
        for f, count in finish_reasons.most_common():
            pct = count / total_fin * 100
            lines.append(f"| {f} | {count} | {pct:.1f}% |")
    lines.append("")
    lines.append("### Prompter Active Time (gap-based)")
    lines.append("")
    lines.append(f"- **Prompter active:** {prompter_min:.1f}m")
    lines.append(f"- **Wall clock:** {wall_min:.1f}m")
    lines.append(f"- **Idle/waiting:** {idle_min:.1f}m")
    lines.append(f"- **Gaps >60s (capped):** {sum(1 for g in gaps if g > 60)} of {len(gaps)}")
    if gaps:
        lines.append("")
        lines.append("| Gap Range | Count |")
        lines.append("|-----------|-------|")
        for label, cnt in gap_dist.items():
            lines.append(f"| {label} | {cnt} |")

    markdown = "\n".join(lines)

    # Build stats dict
    stats = {
        "duration_seconds": dur if first_ts and last_ts else 0,
        "duration_minutes": wall_min,
        "first_message": first_ts.isoformat() if first_ts else None,
        "last_message": last_ts.isoformat() if last_ts else None,
        "messages": {
            "total": len(data),
            "user": user_msgs,
            "assistant": asst_msgs,
            "tool_parts": tool_total,
        },
        "words": {
            "user": total_uw,
            "assistant": total_aw,
        },
        "tokens": {
            "input": {"total": input_total, "cached": tok_cr, "uncached": tok_in},
            "cache_hit_pct": round(cache_pct, 1) if input_total else 0,
            "output": tok_out,
            "reasoning": tok_reason,
            "total_billed": total_billed,
        },
        "cost": round(session_cost, 6),
        "tools": {
            name: count for name, count in tool_names.most_common()
        },
        "modes": {mode: count for mode, count in modes.most_common()},
        "finish_reasons": {f: count for f, count in finish_reasons.most_common()},
        "part_types": {pt: count for pt, count in part_types.most_common()},
        "prompter_active": {
            "minutes_active": round(prompter_min, 1),
            "minutes_wall": round(wall_min, 1),
            "minutes_idle": round(idle_min, 1),
            "gaps_total": len(gaps),
            "gaps_exceeding_60s_cap": sum(1 for g in gaps if g > 60),
            "gap_distribution_seconds": gap_dist,
        },
    }

    return markdown, stats


def main():
    if len(sys.argv) < 2:
        print("Usage: session_stats.py <archive.json.bz2>", file=sys.stderr)
        sys.exit(1)

    archive_path = sys.argv[1]
    if not archive_path.endswith(".json.bz2"):
        print("Expected a .json.bz2 archive", file=sys.stderr)
        sys.exit(1)

    md_path = archive_path.replace(".json.bz2", ".md")
    if not os.path.exists(md_path):
        print(f"Evaluation file not found: {md_path}", file=sys.stderr)
        sys.exit(1)

    markdown, stats = analyze(archive_path)

    # Write .stats.json
    stats_path = archive_path.replace(".json.bz2", ".stats.json")
    with open(stats_path, "w") as f:
        json.dump(stats, f, indent=2)
    print(f"Wrote {stats_path}")

    # Append to .md — insert before trailing "---" if present, else append
    with open(md_path, "r") as f:
        md_content = f.read()

    if md_content.rstrip().endswith("\n---"):
        # Find the last "---" and insert before it
        idx = md_content.rstrip().rfind("\n---")
        insert_at = md_content.rfind("\n", 0, idx) + 1
        new_content = md_content[:insert_at] + markdown + "\n" + md_content[insert_at:]
    else:
        new_content = md_content.rstrip() + "\n" + markdown + "\n"

    with open(md_path, "w") as f:
        f.write(new_content)
    print(f"Updated {md_path}")


if __name__ == "__main__":
    main()
