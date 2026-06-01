# Handoff Execution Process

When `prompt.md` is run in opencode with `/opensassi` loaded in the current directory (`/home/pc/projects/opensassi/a0/`), the following 10-step process executes.

---

## Step 0: Setup

```
/opensassi
```
Loads the opensassi root skill, which loads all sub-skills (system-design, git, issue, todo, etc.) and calls `load-spec --depth 2`.

## Step 1: User invokes handoff

```
/handoff-execute
```
The agent reads `prompt.md` and begins executing.

## Step 2: Load context

The agent reads these files into context (in parallel via task agents):

- `handoffs/.../handoff.md` — the workflow instructions
- `handoffs/.../A0-REVIEW-PANEL.md` — the 5-expert Enterprise panel methodology
- `handoffs/.../review.md` — the existing codebase review
- `handoffs/.../roadmap.md` — the full roadmap
- `handoffs/.../phase2-audit-reconciliation.md` — audit reconciliation workflow
- `technical-specification.md` — root spec
- `src/*/technical-specification.md` — all sub-module specs
- All `.cpp` and `.h` files in `src/` (via parallel task agents)

## Step 3: Enumerate open issues

```bash
gh issue list --repo opensassi/a0 --state open --limit 80 \
  --json number,title,milestone,labels
```
Returns all open issues with current milestone and label state.

## Step 4: Cluster by system context

Issues grouped into clusters:
- **Spec tree**: #1, #42-#54, #27-#28, #39
- **Source tree**: #7-#15, #19-#21, #25-#26, #56-#81
- **Test tree**: #37-#38, #42-#54
- **UI (c2/d3)**: #16-#18, #29-#36, #38
- **Infrastructure**: #22-#24, #29-#34

## Step 5: Run Enterprise Panel per cluster

For each cluster, the agent loads the A0-REVIEW-PANEL.md prompt and runs the 5-expert panel against that cluster's issues + the loaded source code. The panel identifies:
- Gaps between issue descriptions and actual codebase state
- Issues that need further decomposition
- Missing acceptance criteria

## Step 6: Decompose into atomic units

Using the SeniorSoftwareEngineer expert, the agent breaks each issue into atomic, code-first implementation units. Each unit specifies:

- **Source files**: list of `.h/.cpp` files to create/modify
- **Interface**: public API of the new/modified class
- **Test**: test method and what it verifies
- **Dependencies**: which other units this must wait for
- **Effort**: XS / S / M / L / XL

Effort estimates are used for work assignment:
| Size | Typical scope | Example |
|------|---------------|---------|
| XS | Single method, < 50 lines | Adding a utility function |
| S | Single class, < 200 lines | New thin wrapper class |
| M | Multiple related classes | New subsystem module |
| L | Cross-cutting change | Refactoring a core interface |
| XL | Multi-file, multi-module | New daemon (d3 core) |

## Step 7: Create GitHub issues with full board setup

For each atomic unit, the agent executes the 6-step creation workflow:

### 7a. Create the issue
```bash
gh issue create --repo opensassi/a0 \
  --title "<title>" \
  --label "<labels>" \
  --body "<body>"
```

### 7b. Set milestone (maps to phase)
```bash
gh issue edit <N> --repo opensassi/a0 --milestone "<Phase>"
```
Inherits the parent issue's phase: "Open Source", "Cloud Beta", or "Enterprise".

### 7c. Add to project board
```bash
NODE_ID=$(gh api graphql -f query="query { repository(owner:\"opensassi\", name:\"a0\") { issue(number:<N>) { id } } }" --jq '.data.repository.issue.id')
ITEM_ID=$(gh api graphql -f query="mutation { addProjectV2ItemById(input: { projectId: \"PVT_kwDOEN7XcM4BZXJf\" contentId: \"$NODE_ID\" }) { item { id } } }" --jq '.data.addProjectV2ItemById.item.id')
```

### 7d. Set Phase custom field
```bash
gh api graphql -f query="mutation { updateProjectV2ItemFieldValue(input: { projectId: \"PVT_kwDOEN7XcM4BZXJf\" itemId: \"$ITEM_ID\" fieldId: \"PVTSSF_lADOEN7XcM4BZXJfzhUXmvU\" value: { singleSelectOptionId: \"<OS/CB/ENT_OPTION_ID>\" } }) { projectV2Item { id } } }"
```
Look up option IDs if needed:
```bash
gh project field-list 1 --owner opensassi --format json | \
  jq '.fields[] | select(.name=="Phase") | .options[] | {name, id}'
```

### 7e. Set Status to Backlog
```bash
gh api graphql -f query="mutation { updateProjectV2ItemFieldValue(input: { projectId: \"PVT_kwDOEN7XcM4BZXJf\" itemId: \"$ITEM_ID\" fieldId: \"PVTSSF_lADOEN7XcM4BZXJfzhUWTUo\" value: { singleSelectOptionId: \"f75ad846\" } }) { projectV2Item { id } } }"
```

### 7f. Set Size from effort estimate
```bash
gh api graphql -f query="mutation { updateProjectV2ItemFieldValue(input: { projectId: \"PVT_kwDOEN7XcM4BZXJf\" itemId: \"$ITEM_ID\" fieldId: \"PVTSSF_lADOEN7XcM4BZXJfzhUWThI\" value: { singleSelectOptionId: \"<XS/S/M/L/XL_ID>\" } }) { projectV2Item { id } } }"
```
Size option IDs: XS = `6c6483d2`, S = `f784b110`, M = `7515a9f1`, L = `817d0097`, XL = `db339eb2`

## Step 8: Link sub-issues to parent epic

```bash
# Via file to handle large bodies
gh issue view <EPIC> --repo opensassi/a0 --json body --jq '.body' > /tmp/body.md
echo "- [ ] #<new-issue> <title>" >> /tmp/body.md
gh issue edit <EPIC> --repo opensassi/a0 --body-file /tmp/body.md
```

The board's "Sub-issues progress" field auto-updates from tasklist checkboxes.

## Step 9: Run Phase 2 — Audit Reconciliation

Load `phase2-audit-reconciliation.md` and run the 7-expert system-design-review panel over every new feature issue. For each gap found:
- **Option A**: Add user stories to the existing issue
- **Option B**: Create a new audit sub-issue (with full board setup: milestone, Phase, Status, Size)
- **Option C**: Document no-action

## Step 10: Report

Output a summary table:

| Feature Issue | Phase | Audit Intersection | Action Taken | New/Modified |
|---------------|-------|-------------------|--------------|-------------|
| #N title | OS/CB/ENT | yes/no | stories/new/none | #M |

Plus: how many atomic units created, what can be implemented in parallel, phase distribution of new issues.

---

## Key Constraint

The Phase option IDs on the project board may change if the field is ever recreated. **The agent must verify the current Phase option IDs** before setting them, otherwise the GraphQL mutation silently fails and the issue won't appear in the phase-filtered board view:

```bash
gh project field-list 1 --owner opensassi --format json | \
  jq '.fields[] | select(.name=="Phase") | .options[] | {name, id}'
```

---

## Atomic Decomposition for Agentic Development

Since development is fully agent-driven, very large work chunks can be one-shotted. The atomic decomposition serves two purposes:

1. **Workspace parallelization**: Each atomic unit maps to a separate workspace or branch, enabling parallel agent instances to work simultaneously without conflicts.

2. **Single-commit bundling**: After parallel development, all atomic units are bundled into a single commit. This reduces test and validation time since the entire test suite runs once against the complete change set, rather than repeatedly for each incremental commit.

This is the opposite of traditional CI workflow (many small commits, each tested independently). For agentic development, the optimal pattern is:

```
Decompose → Parallel workspaces → Implement independently →
Bundle into single commit → Run full test suite once → Commit
```
