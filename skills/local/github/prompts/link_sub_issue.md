# Link Sub-Issue to Parent Epic

## Purpose

Append a tasklist entry referencing a new sub-issue to a parent epic's body so the project board's "Sub-issues progress" field auto-updates.

## Parameters

- `epic_number` — Issue number of the parent epic
- `sub_issue_number` — Issue number of the new sub-issue
- `sub_issue_title` — Title of the new sub-issue

## Workflow

### Step 1: Get the epic's current body

Call `{{tool:issue_view _="<epic_number>" repo="opensassi/a0" json="body" jq=".body"}}` — or if the body is long, dump to a file:

Call `{{tool:issue_view _="<epic_number>" repo="opensassi/a0" json="body" jq=".body > /tmp/body-<epic_number>.md"}}`

### Step 2: Append the tasklist entry

If using inline body (small bodies):
Create the new body string by appending `\n- [ ] #<sub_issue_number> <sub_issue_title>` to the current body.

Call `{{tool:issue_edit _="<epic_number>" repo="opensassi/a0" body="<CURRENT_BODY>\n- [ ] #<sub_issue_number> <sub_issue_title>"}}`

If using body-file (large bodies — preferred):
```bash
echo "- [ ] #<sub_issue_number> <sub_issue_title>" >> /tmp/body-<epic_number>.md
```

Call `{{tool:issue_edit _="<epic_number>" repo="opensassi/a0" bodyFile="/tmp/body-<epic_number>.md"}}`

### Step 3: Report

> "Linked sub-issue #<sub_issue_number> <sub_issue_title> to epic #<epic_number>."
