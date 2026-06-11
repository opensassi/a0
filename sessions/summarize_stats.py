#!/usr/bin/env python3
"""Summarize all .stats.json files into a structured JSON of actual measured times.

Reads every .stats.json in the sessions directory, extracts key metrics
(duration, prompter active, messages, tokens, cost, tools, part types),
and writes an aggregated summary alongside the per-session data.

Usage:
    python3 sessions/summarize_stats.py
"""
import os, sys, json, glob, statistics
from datetime import datetime, timezone, timedelta
from collections import Counter

SESSIONS_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT = os.path.join(SESSIONS_DIR, "actual-times-summary.json")


def extract_slug(stats_path):
    """Derive a readable slug from the .stats.json filename."""
    name = os.path.basename(stats_path).replace(".stats.json", "")
    return name


def parse_date_from_slug(slug):
    """Extract YYYY-MM-DD from the slug prefix."""
    return slug[:10] if len(slug) >= 10 else slug


def main():
    stats_files = sorted(glob.glob(os.path.join(SESSIONS_DIR, "*.stats.json")))

    if not stats_files:
        print("No .stats.json files found.")
        sys.exit(1)

    sessions = []
    all_tools = Counter()
    all_part_types = Counter()
    all_modes = Counter()
    all_finish_reasons = Counter()
    prompter_active_h = []
    total_messages = 0
    total_tokens_billed = 0
    total_cost = 0.0
    total_cache_hit_weighted = 0.0
    total_input = 0
    earliest_ts = None
    latest_ts = None

    for sp in stats_files:
        slug = extract_slug(sp)
        with open(sp) as f:
            raw = json.load(f)

        dur_min = raw.get("duration_minutes", 0)
        dur_h = round(dur_min / 60, 4)
        pa_min = raw.get("prompter_active", {}).get("minutes_active", 0)
        pa_h = round(pa_min / 60, 4)

        msg = raw.get("messages", {})
        words = raw.get("words", {})
        tok = raw.get("tokens", {})
        t_in = tok.get("input", {})
        cost = raw.get("cost", 0)
        tools = raw.get("tools", {})
        pts = raw.get("part_types", {})
        modes = raw.get("modes", {})
        finish = raw.get("finish_reasons", {})
        pa_data = raw.get("prompter_active", {})

        # Track calendar span (real elapsed time across all sessions)
        first_ts = raw.get("first_message")
        last_ts = raw.get("last_message")
        if first_ts:
            try:
                ft = datetime.fromisoformat(first_ts)
                if earliest_ts is None or ft < earliest_ts:
                    earliest_ts = ft
            except (ValueError, TypeError):
                pass
        if last_ts:
            try:
                lt = datetime.fromisoformat(last_ts)
                if latest_ts is None or lt > latest_ts:
                    latest_ts = lt
            except (ValueError, TypeError):
                pass

        # Accumulate for summary
        prompter_active_h.append(pa_h)
        total_messages += msg.get("total", 0)
        billed = tok.get("total_billed", 0)
        total_tokens_billed += billed
        total_cost += cost
        total_input += t_in.get("total", 0)
        chp = tok.get("cache_hit_pct", 0)
        total_cache_hit_weighted += chp * t_in.get("total", 0)

        for k, v in tools.items():
            all_tools[k] += v
        for k, v in pts.items():
            all_part_types[k] += v
        for k, v in modes.items():
            all_modes[k] += v
        for k, v in finish.items():
            all_finish_reasons[k] += v

        # Top 5 tools per session
        top_tools = dict(sorted(tools.items(), key=lambda x: -x[1])[:5])

        sessions.append({
            "slug": slug,
            "date": parse_date_from_slug(slug),
            "duration": {
                "minutes": round(dur_min, 4),
                "hours": dur_h,
            },
            "prompter_active": {
                "minutes": round(pa_min, 4),
                "hours": pa_h,
                "pct_of_wall": round(pa_min / dur_min * 100, 1) if dur_min > 0 else 0,
                "gaps_total": pa_data.get("gaps_total", 0),
                "gaps_exceeding_60s_cap": pa_data.get("gaps_exceeding_60s_cap", 0),
            },
            "messages": {
                "total": msg.get("total", 0),
                "user": msg.get("user", 0),
                "assistant": msg.get("assistant", 0),
                "tool_parts": msg.get("tool_parts", 0),
            },
            "words": {
                "user": words.get("user", 0),
                "assistant": words.get("assistant", 0),
            },
            "tokens": {
                "total_billed": billed,
                "input_cached": t_in.get("cached", 0),
                "input_uncached": t_in.get("uncached", 0),
                "output": tok.get("output", 0),
                "reasoning": tok.get("reasoning", 0),
                "cache_hit_pct": chp,
            },
            "cost": round(cost, 6),
            "top_tools": top_tools,
        })

    # ── Summary ─────────────────────────────────────────────────────────
    n = len(sessions)
    avg_cache = round(total_cache_hit_weighted / total_input, 1) if total_input > 0 else 0

    total_pa_h = round(sum(prompter_active_h), 4)
    avg_pa_h = round(statistics.mean(prompter_active_h), 2) if n else 0

    calendar_span_h = round((latest_ts - earliest_ts).total_seconds() / 3600, 2) if earliest_ts and latest_ts else None

    total_cost = round(total_cost, 6)
    avg_cost = round(total_cost / n, 6) if n else 0

    top_tools_all = dict(sorted(all_tools.items(), key=lambda x: -x[1]))
    top_part_types = dict(sorted(all_part_types.items(), key=lambda x: -x[1]))
    top_modes = dict(sorted(all_modes.items(), key=lambda x: -x[1]))
    top_finish = dict(sorted(all_finish_reasons.items(), key=lambda x: -x[1]))

    output = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "session_count": n,
        "calendar_span_hours": calendar_span_h,
        "sessions": sessions,
        "summary": {
            "total_prompter_active_hours": total_pa_h,
            "total_messages": total_messages,
            "total_tokens_billed": total_tokens_billed,
            "total_cost": total_cost,
            "avg_cost_per_session": avg_cost,
            "avg_prompter_active_hours": avg_pa_h,
            "avg_cache_hit_pct": avg_cache,
            "top_tools": top_tools_all,
            "part_type_totals": top_part_types,
            "mode_totals": top_modes,
            "finish_reason_totals": top_finish,
        },
    }

    with open(OUTPUT, "w") as f:
        json.dump(output, f, indent=2)

    print(f"Wrote {OUTPUT}")
    print(f"  Sessions: {n}")
    print(f"  Calendar span:        {calendar_span_h:.2f} hours" if calendar_span_h else "  Calendar span: unknown")
    print(f"  Total prompter active: {total_pa_h:>10.2f} hours")
    print(f"  Total messages:       {total_messages:>10}")
    print(f"  Total tokens billed:  {total_tokens_billed:>10,}")
    print(f"  Total cost:          ${total_cost:>9.6f}")
    print(f"  Top tools:            {', '.join(list(top_tools_all)[:5])}")


if __name__ == "__main__":
    main()
