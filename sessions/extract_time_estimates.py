#!/usr/bin/env python3
"""Extract prompter and SME time estimates from session .md files to JSON.

Parses "Prompter Time Estimate" and "Model-Equivalent SME Time Estimate"
sections from all 44 session evaluation files using regex, preserving ranges
and raw text verbatim.

Usage:
    python3 sessions/extract_time_estimates.py
"""
import os, sys, json, re, glob
from datetime import datetime, timezone

SESSIONS_DIR = os.path.dirname(os.path.abspath(__file__))
EXCLUDE = {"session-evaluation-prompt.md", "development-overview-prompt.md", "project-summary.md"}
OUTPUT = os.path.join(SESSIONS_DIR, "time-estimates-summary.json")

# ── Regex patterns ──────────────────────────────────────────────────────
TIME_RE = re.compile(
    r'~?\s*(\d+(?:\.\d+)?)\s*(?:[–\-]\s*(\d+(?:\.\d+)?))?\s*(hours?|h|m)'
)
BOLD_TOTAL = re.compile(r'\*\*Total:\s*(.*?)\*\*(.*)')
BOLD_TOTAL_TABLE = re.compile(
    r'\|\s*\*\*Total\*\*\s*\|\s*\*\*([\d.]+)\*\*'
)
PROMPTER_HEADER = re.compile(
    r'\*\*Prompter Time Estimate:\*\*|## Prompter Time Estimate'
)
SME_HEADER = re.compile(
    r'\*\*Model-Equivalent SME Time Estimate:\*\*|## Model-Equivalent SME Time Estimate'
)
SECTION_HEADER = re.compile(r'^\*\*(?!Total:)[^*]+:\*\*$')
DIVIDER = re.compile(r'^---+$')
H2 = re.compile(r'^## ')
ANNOTATION_RE = re.compile(r'\s*(\([^)]+\))\s*$')

# ── Helpers ─────────────────────────────────────────────────────────────


def parse_time(text):
    """Parse a time string into (min_hours, max_hours, is_range, raw).

    Handles: ``~1.5 hours``, ``40–60 hours``, ``~1.2h``, ``136.7m``, etc.
    Returns None if unparseable.
    """
    text = text.strip().strip('*').strip()
    m = TIME_RE.search(text)
    if not m:
        return None
    v1 = float(m.group(1))
    v2 = float(m.group(2)) if m.group(2) else v1
    unit = m.group(3)
    if unit == 'm':
        v1 /= 60
        v2 /= 60
    is_range = abs(v1 - v2) > 0.001
    return (round(v1, 4), round(v2, 4), is_range, m.group(0).strip())


def find_header(lines, pattern):
    """Return index of first line matching *pattern*, or None."""
    for i, line in enumerate(lines):
        if pattern.search(line):
            return i
    return None


def extract_section(lines, start):
    """Return lines from *start* up to the next section header / divider / H2 / EOF."""
    out = []
    for i in range(start, len(lines)):
        if i > start and (SECTION_HEADER.match(lines[i]) or DIVIDER.match(lines[i]) or H2.match(lines[i])):
            break
        out.append(lines[i])
    return out


def next_nonblank(lines, start):
    for i in range(start, len(lines)):
        if lines[i].strip():
            return i
    return len(lines)


# ── Prompter parser ────────────────────────────────────────────────────

STD_PROMPTER_BULLETS = {
    'reading': re.compile(r'^- Reading and digesting model responses:\s*(.*)$'),
    'thinking': re.compile(r'^- Thinking, strategizing, and weighing options:\s*(.*)$'),
    'writing': re.compile(r'^- Writing messages and directives:\s*(.*)$'),
}
EXTRA_BULLET = re.compile(r'^- ([A-Z][^:]+):\s*(.*)$')


def parse_prompter(lines):
    rv = {
        "total_hours_min": None,
        "total_hours_max": None,
        "is_range": None,
        "total_raw": None,
        "annotation": None,
        "sub_items": [],
    }

    for line in lines:
        s = line.strip()
        if not s:
            continue

        # Standard sub-bullets
        for key, pat in STD_PROMPTER_BULLETS.items():
            m = pat.match(s)
            if m:
                parsed = parse_time(m.group(1))
                rv["sub_items"].append({
                    "label": key.replace('_', ' ').title(),
                    **({} if parsed is None else {
                        "hours_min": parsed[0],
                        "hours_max": parsed[1],
                        "is_range": parsed[2],
                        "raw": parsed[3],
                    })
                })
                break
        else:
            # Extra 4th bullet (seen in 2 files)
            m = EXTRA_BULLET.match(s)
            if m:
                parsed = parse_time(m.group(2))
                rv["sub_items"].append({
                    "label": m.group(1),
                    **({} if parsed is None else {
                        "hours_min": parsed[0],
                        "hours_max": parsed[1],
                        "is_range": parsed[2],
                        "raw": parsed[3],
                    })
                })

        # **Total:** line
        m = BOLD_TOTAL.search(s)
        if m:
            t = m.group(1).strip()
            annot_raw = m.group(2).strip()
            ma = ANNOTATION_RE.search(annot_raw)
            annot = ma.group(1) if ma else (annot_raw if annot_raw else None)
            parsed = parse_time(t)
            if parsed:
                rv["total_hours_min"] = parsed[0]
                rv["total_hours_max"] = parsed[1]
                rv["is_range"] = parsed[2]
                rv["total_raw"] = parsed[3]
            if annot:
                rv["annotation"] = annot

        # Table **Total** row (file 44)
        m = BOLD_TOTAL_TABLE.search(s)
        if m:
            val = float(m.group(1))
            rv["total_hours_min"] = val
            rv["total_hours_max"] = val
            rv["is_range"] = False
            rv["total_raw"] = str(val)

        # Table "Prompter active" row (file 44)
        if '|' in s and 'Prompter active' in s:
            for cell in s.split('|'):
                parsed = parse_time(cell)
                if parsed:
                    rv["sub_items"].append({
                        "label": "Prompter active",
                        "hours_min": parsed[0],
                        "hours_max": parsed[1],
                        "is_range": parsed[2],
                        "raw": parsed[3],
                    })
                    break

    # Fallback: sum sub-items if no total found
    if rv["total_hours_min"] is None and rv["sub_items"]:
        mins = [x.get("hours_min") for x in rv["sub_items"] if "hours_min" in x]
        maxs = [x.get("hours_max") for x in rv["sub_items"] if "hours_max" in x]
        if mins:
            tmin = round(sum(mins), 4)
            tmax = round(sum(maxs), 4)
            rv["total_hours_min"] = tmin
            rv["total_hours_max"] = tmax
            rv["is_range"] = abs(tmin - tmax) > 0.001
            rv["total_raw"] = f"{tmin}–{tmax} hours" if rv["is_range"] else f"{tmin} hours"

    return rv


# ── SME parser ──────────────────────────────────────────────────────────

BULLET = re.compile(r'^-\s+(.*)$')


def parse_sme(lines):
    rv = {
        "total_hours_min": None,
        "total_hours_max": None,
        "is_range": None,
        "total_raw": None,
        "annotation": None,
        "sub_items": [],
    }

    full = '\n'.join(lines)

    # 1. Try **Total:** line
    for line in lines:
        s = line.strip()
        m = BOLD_TOTAL.search(s)
        if m:
            t = m.group(1).strip()
            annot_raw = m.group(2).strip()
            ma = ANNOTATION_RE.search(annot_raw)
            annot = ma.group(1) if ma else (annot_raw if annot_raw else None)
            parsed = parse_time(t)
            if parsed:
                rv["total_hours_min"] = parsed[0]
                rv["total_hours_max"] = parsed[1]
                rv["is_range"] = parsed[2]
                rv["total_raw"] = parsed[3]
            if annot:
                rv["annotation"] = annot
            break

    # 2. Try bullet-based sub-items
    for line in lines:
        s = line.strip()
        if not s:
            continue
        m = BULLET.match(s)
        if not m:
            continue
        bt = m.group(1)
        if '**Total' in bt:
            continue
        # Find last time value in the bullet text as the "total" for this item
        hits = list(TIME_RE.finditer(bt))
        if not hits:
            continue
        last = hits[-1]
        label = bt[:last.start()].strip().rstrip(':').rstrip()
        if not label:
            label = bt[:60].strip()
        parsed = parse_time(last.group(0))
        rv["sub_items"].append({
            "label": label,
            **({} if parsed is None else {
                "hours_min": parsed[0],
                "hours_max": parsed[1],
                "is_range": parsed[2],
                "raw": parsed[3],
            })
        })

    # 3. If no bullets found, try inline values (paragraph / single-line format)
    if not rv["sub_items"]:
        prev_end = 0
        is_first = True
        for m in TIME_RE.finditer(full):
            # Skip the very first inline value — it's likely the total
            # (e.g. "10–14 hours. A senior C++ build engineer would need ...")
            if is_first:
                is_first = False
                # Save as fallback total if none found later
                parsed = parse_time(m.group(0))
                if parsed:
                    fallback_total = parsed
                else:
                    fallback_total = None
                prev_end = m.end()
                continue
            ctx = full[prev_end:m.start()].strip().rstrip(':').strip()
            if not ctx:
                after = full[m.end():m.end() + 80]
                ctx = after.split(',')[0].split('. ')[0].strip()
                if not ctx:
                    prev_end = m.end()
                    continue
            parsed = parse_time(m.group(0))
            if parsed:
                rv["sub_items"].append({
                    "label": ctx,
                    "hours_min": parsed[0],
                    "hours_max": parsed[1],
                    "is_range": parsed[2],
                    "raw": parsed[3],
                })
            prev_end = m.end()
    else:
        fallback_total = None

    # 4. Try lead-in line for total (e.g. "Approximately 40–60 hours of ...")
    if rv["total_hours_min"] is None and lines:
        # Skip header line (lines[0]), find first content line
        for line in lines[1:]:
            s = line.strip()
            if not s or s.startswith('**') or s.startswith('|'):
                continue
            parsed = parse_time(s)
            if parsed:
                rv["total_hours_min"] = parsed[0]
                rv["total_hours_max"] = parsed[1]
                rv["is_range"] = parsed[2]
                rv["total_raw"] = parsed[3]
                break

    # 5. Try table **Total** row (file 44)
    if rv["total_hours_min"] is None:
        for line in lines:
            m = BOLD_TOTAL_TABLE.search(line)
            if m:
                val = float(m.group(1))
                rv["total_hours_min"] = val
                rv["total_hours_max"] = val
                rv["is_range"] = False
                rv["total_raw"] = str(val)
                break

    # 6. Use fallback total from skipped first inline value
    if rv["total_hours_min"] is None and fallback_total is not None:
        rv["total_hours_min"] = fallback_total[0]
        rv["total_hours_max"] = fallback_total[1]
        rv["is_range"] = fallback_total[2]
        rv["total_raw"] = fallback_total[3]

    # 7. Last resort: sum sub-items
    if rv["total_hours_min"] is None and rv["sub_items"]:
        mins = [x.get("hours_min") for x in rv["sub_items"] if "hours_min" in x]
        maxs = [x.get("hours_max") for x in rv["sub_items"] if "hours_max" in x]
        if mins:
            tmin = round(sum(mins), 4)
            tmax = round(sum(maxs), 4)
            rv["total_hours_min"] = tmin
            rv["total_hours_max"] = tmax
            rv["is_range"] = abs(tmin - tmax) > 0.001
            rv["total_raw"] = f"{tmin}–{tmax} hours" if rv["is_range"] else f"{tmin} hours"

    return rv


# ── Main ────────────────────────────────────────────────────────────────


def main():
    md_files = sorted(
        f for f in glob.glob(os.path.join(SESSIONS_DIR, "*.md"))
        if os.path.basename(f) not in EXCLUDE
    )

    sessions = []
    partial = 0

    for md_path in md_files:
        slug = os.path.basename(md_path).replace(".md", "")
        with open(md_path) as f:
            lines = f.read().split('\n')

        pi = find_header(lines, PROMPTER_HEADER)
        si = find_header(lines, SME_HEADER)

        prompter_rv = parse_prompter(extract_section(lines, pi)) if pi is not None else {
            "total_hours_min": None, "total_hours_max": None,
            "is_range": None, "total_raw": None, "annotation": None, "sub_items": []
        }
        sme_rv = parse_sme(extract_section(lines, si)) if si is not None else {
            "total_hours_min": None, "total_hours_max": None,
            "is_range": None, "total_raw": None, "annotation": None, "sub_items": []
        }

        if prompter_rv["total_hours_min"] is None or sme_rv["total_hours_min"] is None:
            partial += 1

        sessions.append({
            "slug": slug,
            "prompter": prompter_rv,
            "sme": sme_rv,
        })

    # ── Summary aggregation ─────────────────────────────────────────────
    p_mins = [s["prompter"]["total_hours_min"] for s in sessions
              if s["prompter"]["total_hours_min"] is not None]
    p_maxs = [s["prompter"]["total_hours_max"] for s in sessions
              if s["prompter"]["total_hours_max"] is not None]
    s_mins = [s["sme"]["total_hours_min"] for s in sessions
              if s["sme"]["total_hours_min"] is not None]
    s_maxs = [s["sme"]["total_hours_max"] for s in sessions
              if s["sme"]["total_hours_max"] is not None]

    data = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "session_count": len(sessions),
        "sessions": sessions,
        "summary": {
            "total_prompter_hours_min": round(sum(p_mins), 4),
            "total_prompter_hours_max": round(sum(p_maxs), 4),
            "total_sme_hours_min": round(sum(s_mins), 4),
            "total_sme_hours_max": round(sum(s_maxs), 4),
            "session_count": len(sessions),
            "sessions_fully_parsed": len(sessions) - partial,
            "sessions_partial": partial,
        },
    }

    with open(OUTPUT, "w") as f:
        json.dump(data, f, indent=2)

    def _fmt(mn, mx):
        return f"{mn}–{mx} hours" if abs(mn - mx) > 0.001 else f"{mn} hours"

    print(f"Wrote {OUTPUT}")
    print(f"  Sessions: {len(sessions)} ({len(sessions) - partial} full, {partial} partial)")
    print(f"  Prompter: {_fmt(round(sum(p_mins), 4), round(sum(p_maxs), 4))}")
    print(f"  SME:      {_fmt(round(sum(s_mins), 4), round(sum(s_maxs), 4))}")


if __name__ == "__main__":
    main()
