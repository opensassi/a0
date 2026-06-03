# GitHub Setup Verification

## Purpose

Verify the GitHub CLI is authenticated and discover the open project board for the repository. This must succeed before any board operations.

## Workflow

### Step 1: Check authentication

Call `{{tool:auth_check}}` to verify `gh` is logged in.

If the tool returns an error, report failure and stop:
> "GitHub CLI is not authenticated. Run `gh auth login` first."

### Step 2: Discover the project board

Call `{{tool:graphql field="query=query { repository(owner:\"opensassi\", name:\"a0\") { projectsV2(first: 5, orderBy: {field: CREATED_AT, direction: DESC}) { nodes { id title number closed } } } }" jq=".data.repository.projectsV2.nodes[] | select(.closed == false) | {number, title, id}"}}`

Parse the result to extract:
- `number` — project number (integer)
- `title` — project name
- `id` — project node ID (starts with `PVT_`)

If the result is empty (no open project found), report failure and stop:
> "No open GitHub Project found for the repository opensassi/a0."

### Step 3: Report

Output a summary:
> "GitHub CLI authenticated. Project discovered: `{title}` (#{number}, ID: `{id}`)."

Store the project number and project ID for use in subsequent operations.
