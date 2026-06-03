# Create Issue with Full Project Board Setup

## Purpose

Create a new GitHub issue and configure all project board fields: milestone, phase, status, and size. Every issue MUST have all four fields set.

## Prerequisites

Run `verify_setup` first to confirm authentication and discover the project board. Run `lookup_phase_fields` to get the current Phase option IDs.

## Parameters

- `title` — Issue title
- `labels` — Comma-separated labels
- `body` — Issue body with user stories and acceptance criteria
- `phase` — One of: "Open Source", "Cloud Beta", "Enterprise"
- `milestone` — Maps to phase: "Open Source", "Cloud Beta", or "Enterprise"
- `size` — Effort estimate: XS, S, M, L, XL

## Workflow

### Step 1: Create the issue

Call `{{tool:issue_create repo="opensassi/a0" title="<title>" label="<labels>" body="<body>"}}`

Capture the output URL and extract the issue number from it.

### Step 2: Set the milestone

Call `{{tool:issue_edit _="<N>" repo="opensassi/a0" milestone="<milestone>"}}`

### Step 3: Get the issue's GraphQL node ID

Call `{{tool:graphql field="query=query { repository(owner:\"opensassi\", name:\"a0\") { issue(number:<N>) { id } } }" jq=".data.repository.issue.id"}}`

### Step 4: Add the issue to the project board

Call `{{tool:graphql field="query=mutation { addProjectV2ItemById(input: { projectId: \"<PROJECT_ID>\" contentId: \"<NODE_ID>\" }) { item { id } } }" jq=".data.addProjectV2ItemById.item.id"}}`

Capture the item ID.

### Step 5: Set the Phase custom field

Look up the Phase option ID for the target phase (from `lookup_phase_fields`). Then:

Call `{{tool:graphql field="query=mutation { updateProjectV2ItemFieldValue(input: { projectId: \"<PROJECT_ID>\" itemId: \"<ITEM_ID>\" fieldId: \"PVTSSF_lADOEN7XcM4BZXJfzhUXmvU\" value: { singleSelectOptionId: \"<PHASE_OPTION_ID>\" } }) { projectV2Item { id } } }"}}`

### Step 6: Set Status to Backlog

Call `{{tool:graphql field="query=mutation { updateProjectV2ItemFieldValue(input: { projectId: \"<PROJECT_ID>\" itemId: \"<ITEM_ID>\" fieldId: \"PVTSSF_lADOEN7XcM4BZXJfzhUWTUo\" value: { singleSelectOptionId: \"f75ad846\" } }) { projectV2Item { id } } }"}}`

### Step 7: Set Size

Look up the Size option ID for the target size:
- XS = `6c6483d2`
- S = `f784b110`
- M = `7515a9f1`
- L = `817d0097`
- XL = `db339eb2`

Call `{{tool:graphql field="query=mutation { updateProjectV2ItemFieldValue(input: { projectId: \"<PROJECT_ID>\" itemId: \"<ITEM_ID>\" fieldId: \"PVTSSF_lADOEN7XcM4BZXJfzhUWThI\" value: { singleSelectOptionId: \"<SIZE_OPTION_ID>\" } }) { projectV2Item { id } } }"}}`

### Step 8: Report

Output the result:
> "Created issue #<N>: <title> (Phase: <phase>, Status: Backlog, Size: <size>, Milestone: <milestone>)"
