#!/usr/bin/env python3
"""Validate a session evaluation .md file against the canonical template.

Usage:
  python3 sessions/session_md_validator.py sessions/<slug>.md
  python3 sessions/session_md_validator.py sessions/<slug>.md --stats sessions/<slug>.stats.json

Reads the .md, parses its sections, validates structure and content, and
optionally cross-checks against the auto-generated .stats.json.

Returns a JSON report with validation errors/warnings that an agent can use
to regenerate the file via the session-evaluation generate command.
"""
import json, os, re, sys
from collections import OrderedDict


# Canonical section definitions
SECTION_ORDER = [
    "Session ID",
    "Date / Duration",
    "Project / Context",
    "Top-Level Component",
    "Second-Level Modules",
    "Prompter Contributions",
    "Model Contributions",
    "Prompter Time Estimate",
    "Model-Equivalent SME Time Estimate",
    "Required SME Expertise",
    "Aggregation Tags",
]

SECTION_NAMES = [s.lower() for s in SECTION_ORDER]

# Patterns for detecting structure
# Bold heading: **Name:** rest-of-line
# Markdown heading: ## Name
HEADING_RE = re.compile(r'^\*\*(.+?):\*\*(.*)$|^##\s+(.+?)\s*$')
BOLD_TOTAL_RE = re.compile(r'^\s*[-|]?\s*\**Total\**\s*:?\s*\**\s*([\d.]+)\s*h', re.IGNORECASE)
DASH_ITEM_RE = re.compile(r'^\s*-\s+(.*)')
TABLE_ROW_RE = re.compile(r'^\|')
EMPTY_LINE_RE = re.compile(r'^\s*$')
STATS_SEPARATOR_RE = re.compile(r'^---+\s*$')
STATS_HEADING_RE = re.compile(r'^##\s+Extracted Session Stats')


def normalize_heading_name(name):
    """Normalize heading name for matching."""
    n = name.strip().rstrip(":").lower()
    # Handle variations
    alias = {
        "date / duration": "date / duration",
        "date": "date / duration",
        "duration": "date / duration",
        "project / context": "project / context",
        "project": "project / context",
        "context": "project / context",
        "top-level component": "top-level component",
        "top level component": "top-level component",
        "second-level modules": "second-level modules",
        "second level modules": "second-level modules",
        "prompter contributions": "prompter contributions",
        "model contributions": "model contributions",
        "prompter time estimate": "prompter time estimate",
        "prompter time": "prompter time estimate",
        "model-equivalent sme time estimate": "model-equivalent sme time estimate",
        "model equivalent sme time estimate": "model-equivalent sme time estimate",
        "sme time estimate": "model-equivalent sme time estimate",
        "required sme expertise": "required sme expertise",
        "sme expertise": "required sme expertise",
        "aggregation tags": "aggregation tags",
        "tags": "aggregation tags",
    }
    # Allow fuzzy match: "Session ID" and "Session Id" both match
    for key in sorted(alias.keys(), key=len, reverse=True):
        if key in n:
            return alias[key]
    return n


def parse_sections(text):
    """Parse the .md text into a list of sections.

    Returns list of dicts:
      {name, body_lines, start_line, end_line, heading_raw, heading_line}
    Also returns any pre-heading frontmatter.
    """
    lines = text.split("\n")
    sections = []
    current = None
    frontmatter = []
    in_stats = False
    found_first_heading = False

    for i, line in enumerate(lines):
        raw = i + 1  # 1-indexed

        # Detect auto-generated stats section boundary
        if STATS_HEADING_RE.match(line):
            in_stats = True
        if in_stats:
            continue

        m = HEADING_RE.match(line)
        if m:
            is_bold = m.group(1) is not None
            raw_name = (m.group(1) or m.group(3) or "").strip()
            name = normalize_heading_name(raw_name)
            rest = (m.group(2) or "").strip() if is_bold else ""
            # Only create a section for recognized canonical names, or ## headings.
            # Inline **Bold:** items (like **Method:**) are treated as body content.
            is_recognized = name in SECTION_NAMES or not is_bold
            if is_recognized:
                found_first_heading = True
                if current:
                    # Merge consecutive date + duration → date / duration
                    if current["name"] == "date / duration" and name == "date / duration":
                        current["inline_content"] += " " + rest
                        current["body_lines"].append(line)
                        current["end_line"] = raw
                        continue
                    sections.append(current)
                current = {
                    "name": name,
                    "raw_name": raw_name,
                    "body_lines": [],
                    "start_line": raw,
                    "end_line": raw,
                    "heading_raw": line,
                    "heading_line": raw,
                    "inline_content": rest,
                }
                continue
        if current:
            current["body_lines"].append(line)
            current["end_line"] = raw
        elif not found_first_heading:
            frontmatter.append(line)

    if current:
        sections.append(current)

    return sections, frontmatter


def error(field, message, severity="error", line=None, **kwargs):
    d = {"field": field, "message": message, "severity": severity}
    if line:
        d["line"] = line
    d.update(kwargs)
    return d


def validate_section_order(sections):
    """Check that sections appear in the canonical order."""
    errors = []
    found = []
    for s in sections:
        if s["name"] in SECTION_NAMES:
            found.append(s["name"])

    # Check for missing sections
    for i, expected in enumerate(SECTION_NAMES):
        if expected not in found:
            errors.append(error(
                expected,
                f"Missing required section: {SECTION_ORDER[i]}",
                line=sections[i]["start_line"] if i < len(sections) else None,
            ))

    # Check order
    filtered = [s for s in sections if s["name"] in SECTION_NAMES]
    for i in range(1, len(filtered)):
        prev_idx = SECTION_NAMES.index(filtered[i-1]["name"])
        curr_idx = SECTION_NAMES.index(filtered[i]["name"])
        if curr_idx <= prev_idx:
            errors.append(error(
                filtered[i]["name"],
                f"Section out of order: '{SECTION_ORDER[curr_idx]}' "
                f"appears after '{SECTION_ORDER[prev_idx]}'",
                line=filtered[i]["start_line"],
            ))

    return errors, [s for s in sections if s["name"] in SECTION_NAMES]


def validate_inline_field(section, field_name, pattern=None):
    """Validate an inline field like '**Session ID:** <value>'."""
    errors = []
    value = section.get("inline_content", "").strip()
    if not value:
        # Check body lines
        for line in section["body_lines"]:
            line = line.strip()
            if line:
                value = line
                break
    if not value:
        errors.append(error(
            field_name,
            f"Empty value in {section['raw_name']}",
            line=section["heading_line"],
        ))
    if pattern and not re.match(pattern, value):
        errors.append(error(
            field_name,
            f"Format mismatch in {section['raw_name']}: '{value}' "
            f"does not match expected pattern '{pattern}'",
            line=section["heading_line"],
        ))
    return errors, value


def validate_dash_list_body(section, field_name):
    """Validate that body contains at least one dash-list item."""
    errors = []
    items = []
    for line in section["body_lines"]:
        dm = DASH_ITEM_RE.match(line)
        if dm:
            items.append(dm.group(1))
    if not items and not section.get("inline_content"):
        errors.append(error(
            field_name,
            f"Empty {field_name}: expected dash-list items",
            line=section["heading_line"],
        ))
    return errors, items


def validate_prose_body(section, field_name):
    """Validate that body has non-trivial prose content."""
    errors = []
    text = section.get("inline_content", "")
    for line in section["body_lines"]:
        line = line.strip()
        if line and not DASH_ITEM_RE.match(line) and not TABLE_ROW_RE.match(line):
            text += " " + line
    text = text.strip()
    if len(text) < 10:
        errors.append(error(
            field_name,
            f"Too short in {field_name} ({len(text)} chars)",
            line=section["heading_line"],
        ))
    return errors, text


def validate_date_duration(section):
    """Validate Date / Duration field format."""
    errors = []
    raw = section.get("inline_content", "")
    for line in section["body_lines"]:
        if line.strip():
            raw += " " + line.strip()
    raw = raw.strip()

    if not raw:
        errors.append(error(
            "Date / Duration",
            "Empty value",
            line=section["heading_line"],
        ))
        return errors, None, None

    # Extract date part
    date_match = re.search(r'(\d{4}-\d{2}-\d{2})', raw)
    if not date_match:
        errors.append(error(
            "Date / Duration",
            f"Date not in ISO 8601 format (YYYY-MM-DD). Got: '{raw}'",
            severity="warning",
            line=section["heading_line"],
        ))
        date_val = None
    else:
        date_val = date_match.group(1)

    # Extract prompter time
    time_match = re.search(r'prompter active\s*≈?\s*([\d.]+)\s*hours?', raw, re.IGNORECASE)
    if not time_match:
        errors.append(error(
            "Date / Duration",
            "Missing 'prompter active ≈ N hours'",
            severity="warning",
            line=section["heading_line"],
        ))
        hours_val = None
    else:
        hours_val = float(time_match.group(1))

    return errors, date_val, hours_val


def validate_prompter_time(section):
    """Validate Prompter Time Estimate structure.

    Handles two formats:
      - Bold-list format: Reading: Nm / Thinking: Nm / Writing: Nm / **Total:** Nm
      - Table format: | Metric | Value | Basis | with Prompter active row
    """
    errors = []
    body = "\n".join(section["body_lines"])
    text = section.get("inline_content", "") + "\n" + body

    # Check if it uses table format (has | characters)
    has_table = "| Prompter active" in text or "| Wall clock" in text
    has_bold_total = "**Total**" in text

    if has_table:
        # Table format: extract values from rows
        wall_match = re.search(r'\|\s*Wall clock\s*\|\s*\*{0,2}([\d.]+)\s*m', text, re.IGNORECASE)
        active_match = re.search(r'\|\s*Prompter active\s*\|\s*\*{0,2}([\d.]+)\s*m', text, re.IGNORECASE)
        idle_match = re.search(r'\|\s*Idle.*?waiting\s*\|\s*\*{0,2}([\d.]+)\s*m', text, re.IGNORECASE)

        wall_min = float(wall_match.group(1)) if wall_match else None
        active_min = float(active_match.group(1)) if active_match else None
        idle_min = float(idle_match.group(1)) if idle_match else None

        if wall_match is None:
            errors.append(error("Prompter Time", "Missing 'Wall clock' row in table", line=section["heading_line"]))
        if active_match is None:
            errors.append(error("Prompter Time", "Missing 'Prompter active' row in table", line=section["heading_line"]))

        return errors, active_min
    else:
        # Bold-list / dash-list format
        checks = {
            "reading": r'(?i)read(ing)?\s*(and\s*digesting)?',
            "thinking": r'(?i)think(ing)?',
            "writing": r'(?i)writ(ing|e)',
            "total": r'(?i)\*\*total\**:?\s*\**',
        }
        found = []
        for label, pattern in checks.items():
            if re.search(pattern, text):
                found.append(label)

        for label in ["reading", "thinking", "writing", "total"]:
            if label not in found:
                severity = "error" if label == "total" else "warning"
                errors.append(error(
                    "Prompter Time Estimate",
                    f"Missing '{label.capitalize()}' sub-item",
                    severity=severity,
                    line=section["heading_line"],
                ))

        # Extract total value (bold **Total:** N h, dash-list - **Total:** N h, table | **Total** | **N** |)
        total_match = re.search(r'\**Total\**:?\s*\**\s*([\d.]+)\s*(m|min|h)', text)
        if total_match:
            value = float(total_match.group(1))
            unit = total_match.group(2)
            total_minutes = value if unit == 'm' else value * 60 if unit in ('h', 'min') else value
        else:
            total_minutes = None
            # Only error if the bold **Total:** label is present but value can't be parsed
            if re.search(r'\**Total\**\s*:', text):
                errors.append(error("Prompter Time Estimate", "Cannot parse total value (expected `**Total:** N h`)", line=section["heading_line"]))

        return errors, total_minutes


def validate_sme_time(section):
    """Validate Model-Equivalent SME Time Estimate structure.

    Handles two formats:
      - Bold-list format: - Task: N hours / - **Total:** N hours
      - Table format: | Task | Hours | with | **Total** | **N** | row
    """
    errors = []
    body = "\n".join(section["body_lines"])
    text = section.get("inline_content", "") + "\n" + body
    body_lines = section["body_lines"]

    # Check if table format
    has_table = "| Task" in text and "| Hours" in text

    if has_table:
        # Extract task rows from table
        task_rows = []
        for line in body_lines:
            parts = [p.strip() for p in line.split("|") if p.strip()]
            if len(parts) >= 2 and parts[0].lower() != "task" and parts[0].lower() != "------":
                task_rows.append((parts[0], parts[1] if len(parts) > 1 else ""))

        if not task_rows:
            errors.append(error("SME Time Estimate", "No task rows found in table", line=section["heading_line"]))

        # Find Total row
        total_val = None
        has_total = any("total" in r[0].lower().strip(" *") for r in task_rows)
        for r in task_rows:
            if "total" in r[0].lower():
                tm = re.search(r'([\d.]+)', r[1].strip("* "))
                if tm:
                    total_val = float(tm.group(1))

        if not has_total:
            errors.append(error("SME Time Estimate", "Missing **Total** row in table", line=section["heading_line"]))

    else:
        # Bold-list format
        items = []
        has_total = False
        total_val = None
        for line in body_lines:
            dm = DASH_ITEM_RE.match(line)
            if dm:
                items.append(dm.group(1))
            if BOLD_TOTAL_RE.match(line):
                has_total = True
                tm = re.search(r'([\d.]+)\s*h', line)
                if tm:
                    total_val = float(tm.group(1))

        if not items:
            errors.append(error("SME Time Estimate", "No task items found", line=section["heading_line"]))
        if not has_total:
            errors.append(error("SME Time Estimate", "Missing bold **Total:** line", line=section["heading_line"]))

        # Verify task items have hours
        for item in items:
            if not re.search(r'[\d.]+(\s*hours?|\s*h\b)', item, re.IGNORECASE):
                errors.append(error("SME Time Estimate", f"Task item missing hour estimate: '{item[:60]}'", severity="warning"))

    return errors, total_val


def validate_tags(section):
    """Validate Aggregation Tags format."""
    errors = []
    raw = section.get("inline_content", "")
    for line in section["body_lines"]:
        if line.strip():
            raw += " " + line.strip()
    raw = raw.strip()

    tags = [t.strip() for t in raw.split(",") if t.strip()]
    if len(tags) < 3:
        errors.append(error(
            "Aggregation Tags",
            f"Only {len(tags)} tags found (expected ≥3)",
            severity="warning",
            line=section["heading_line"],
        ))
    # Check for trailing comma
    if raw.rstrip().endswith(","):
        errors.append(error(
            "Aggregation Tags",
            "Trailing comma after last tag",
            line=section["heading_line"],
        ))

    return errors, tags


def cross_validate(eval_data, stats_path):
    """Cross-validate evaluation content against .stats.json."""
    errors = []
    if not stats_path or not os.path.exists(stats_path):
        return errors

    with open(stats_path) as f:
        stats = json.load(f)

    pa = stats.get("prompter_active", {})

    # Wall clock
    eval_wall = eval_data.get("wall_clock_min")
    stats_wall = pa.get("minutes_wall")
    if eval_wall and stats_wall and abs(eval_wall - stats_wall) > 1:
        errors.append(error(
            "Wall clock",
            f"Evaluation value ({eval_wall:.1f}m) differs from stats ({stats_wall:.1f}m)",
            severity="warning",
            md_value=round(eval_wall, 1),
            stats_value=stats_wall,
        ))

    # Prompter active
    eval_active = eval_data.get("prompter_active_min")
    stats_active = pa.get("minutes_active")
    if eval_active is not None and stats_active and abs(eval_active - stats_active) > 1:
        errors.append(error(
            "Prompter active",
            f"Evaluation value ({eval_active:.1f}m) differs from stats ({stats_active:.1f}m)",
            severity="warning",
            md_value=round(eval_active, 1),
            stats_value=stats_active,
        ))

    # Cost
    cost = stats.get("cost")
    if cost is not None:
        errors.append(error(
            "Cost",
            f"Stats report cost=${cost:.6f} — verify evaluation matches",
            severity="info",
            stats_value=cost,
        ))

    return errors


def validate_file(md_path, stats_path=None):
    """Main validation function. Returns a report dict."""
    with open(md_path) as f:
        text = f.read()

    report = {
        "file": os.path.basename(md_path),
        "valid": True,
        "sections_found": 0,
        "sections_expected": len(SECTION_ORDER),
        "errors": [],
        "warnings": [],
    }

    all_errors = []

    # Parse sections
    sections, frontmatter = parse_sections(text)

    if not sections:
        all_errors.append(error("file", "No sections found in file. File may be empty or malformed."))

    # Validate section order
    order_errors, eval_sections = validate_section_order(sections)
    all_errors.extend(order_errors)

    report["sections_found"] = len(eval_sections)

    # Validate each section
    eval_data = {}
    for sec in sections:
        name = sec["name"]

        if name == "session id":
            errs, val = validate_inline_field(sec, "Session ID")
            all_errors.extend(errs)
            eval_data["session_id"] = val

        elif name == "date / duration":
            errs, date_val, hours_val = validate_date_duration(sec)
            all_errors.extend(errs)
            if date_val:
                eval_data["date"] = date_val
            if hours_val:
                eval_data["prompter_active_hours_raw"] = hours_val

        elif name == "project / context":
            errs, val = validate_prose_body(sec, "Project / Context")
            all_errors.extend(errs)

        elif name == "top-level component":
            errs, val = validate_prose_body(sec, "Top-Level Component")
            all_errors.extend(errs)

        elif name in ("second-level modules", "prompter contributions",
                       "model contributions", "required sme expertise"):
            field = sec["raw_name"]
            errs, items = validate_dash_list_body(sec, field)
            all_errors.extend(errs)

        elif name == "prompter time estimate":
            errs, total_min = validate_prompter_time(sec)
            all_errors.extend(errs)
            eval_data["prompter_active_min_total"] = total_min

        elif name == "model-equivalent sme time estimate":
            errs, total_val = validate_sme_time(sec)
            all_errors.extend(errs)
            eval_data["sme_total_hours"] = total_val

        elif name == "aggregation tags":
            errs, tags = validate_tags(sec)
            all_errors.extend(errs)
            eval_data["tags"] = tags

    # Extracted Session Stats section is optional (added by session_stats.py after validation)

    # Cross-validate with stats if available
    cross_errors = cross_validate(eval_data, stats_path)
    all_errors.extend(cross_errors)

    # Separate errors/warnings/info
    report["errors"] = [e for e in all_errors if e.get("severity") == "error"]
    report["warnings"] = [e for e in all_errors if e.get("severity") == "warning"]
    info_list = [e for e in all_errors if e.get("severity") == "info"]

    report["valid"] = len(report["errors"]) == 0

    # Generate regeneration instructions
    if not report["valid"] or report["warnings"]:
        report["regeneration_instructions"] = build_instructions(report)

    return report


def build_instructions(report):
    """Generate human-readable instructions for fixing errors."""
    parts = []
    if report.get("errors"):
        parts.append("Fix the following errors and re-run `session-evaluation generate`:")
        for e in report["errors"]:
            parts.append(f"- {e['field']}: {e['message']}")

    missing_sections = [e["field"] for e in report.get("errors", [])
                        if "Missing required section" in e.get("message", "")]
    if missing_sections:
        parts.append(f"\nMissing sections to add: {', '.join(missing_sections)}")

    if report.get("warnings"):
        parts.append("\nAddress these warnings:")
        for w in report["warnings"]:
            parts.append(f"- {w['field']}: {w['message']}")

    if any("Extracted Session Stats" in e.get("message", "") for e in
           report.get("errors", []) + report.get("warnings", []) + [{"message": ""}]):
        parts.append(
            "\nRun `session_stats.py` to regenerate the stats section:\n"
            "  python3 sessions/session_stats.py sessions/<archive>.json.bz2"
        )

    return "\n".join(parts) if parts else "No corrections needed."


def main():
    if len(sys.argv) < 2:
        print("Usage: session_md_validator.py <file.md> [--stats <file.stats.json>]", file=sys.stderr)
        sys.exit(1)

    md_path = sys.argv[1]
    stats_path = None
    if "--stats" in sys.argv:
        idx = sys.argv.index("--stats")
        if idx + 1 < len(sys.argv):
            stats_path = sys.argv[idx + 1]

    if not os.path.exists(md_path):
        print(json.dumps({"file": md_path, "valid": False, "errors": [{"field": "file", "message": "File not found"}]}, indent=2))
        sys.exit(1)

    report = validate_file(md_path, stats_path)
    print(json.dumps(report, indent=2))

    if not report["valid"]:
        sys.exit(2)


if __name__ == "__main__":
    main()
