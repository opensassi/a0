# Look Up Phase Option IDs

## Purpose

Fetch the current Phase field option IDs from the project board. These IDs change if the field is ever recreated, so they must be verified before use.

## Prerequisites

Run the `verify_setup` prompt first to discover the project number. Use the project number from that step.

## Workflow

### Step 1: List project fields

Call `{{tool:project_field_list _="<project_number>" owner="opensassi" format="json"}}`

### Step 2: Extract Phase options

Apply the jq filter to the output:

```
.fields[] | select(.name=="Phase") | {id, options: [.options[] | {name, id}]}
```

### Step 3: Report

Output the Phase field info:
```
Phase field ID: <id>
Open Source option ID: <id>
Cloud Beta option ID: <id>
Enterprise option ID: <id>
```

Also output the Status and Size option IDs for reference:
- Status field ID: `PVTSSF_lADOEN7XcM4BZXJfzhUWTUo`
  - Backlog: `f75ad846`
  - In progress: `47fc9ee4`
  - Done: `98236657`
- Size field ID: `PVTSSF_lADOEN7XcM4BZXJfzhUWThI`
  - XS: `6c6483d2`, S: `f784b110`, M: `7515a9f1`, L: `817d0097`, XL: `db339eb2`

## Validation

Verify the Phase field has exactly 3 options (Open Source, Cloud Beta, Enterprise). If the structure differs, report the actual options and warn that the board configuration may have changed.
