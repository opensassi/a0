# Session Handoff

**From**: Enterprise Stakeholder Review Panel + Product Roadmap (2026-06-01)
**To**: Next implementation session

---

## Purpose

This document enables the next session to pick up seamlessly. It describes the current state, the workflow for continuing, and the decomposition methodology for breaking issues into parallelizable implementation units.

---

## Current State

**Project board**: https://github.com/orgs/opensassi/projects/1 (81 issues)

**Phases on board** (each issue tagged by Phase field — Open Source / Cloud Beta / Enterprise):
- **Open Source** (34 issues) — due Jun 15
- **Cloud Beta** (40 issues) — due Jul 1
- **Enterprise** (5 issues) — due Aug 15

**Stack**:
```
Cloud Service ──→ d3 ──→ c2 ──→ b1 ──→ a0
  (zero-opex)    (C++17)   (C++17)   (C++17)   (C++17)
```

**Project views**:
| View | Filter |
|------|--------|
| All Issues | No filter (default) |
| Open Source | Phase = Open Source |
| Cloud Beta | Phase = Cloud Beta |
| Enterprise | Phase = Enterprise |
| Team Items | (manual assignment — populate via Assignees) |
| My Items | (manual assignment — populate via Assignees) |

---

## Next Session Workflow

### Step 1: Load Context

Load the following into context:
- `AGENTS.md` — agent conventions
- All technical-specification.md files (root + sub-modules)
- All `.spec.md` files in `src/`
- All `.cpp` and `.h` files in `src/`
- All `skills/` and `prompts/` files
- `A0-REVIEW-PANEL.md` — the expert panel prompt (for running reviews)
- `.artifacts/review.md` — the existing codebase review
- `handoffs/2026-06-01-enterprise-panel-roadmap/handoff.md` — this document
- `handoffs/2026-06-01-enterprise-panel-roadmap/phase2-audit-reconciliation.md` — audit reconciliation

### Step 2: Load Open Issues

Use `gh issue list --repo opensassi/a0 --state open --limit 80 --json number,title,milestone,labels` to enumerate all open issues with their milestone and label data.

### Step 3: Cluster Issues by System Context

Group issues by which system context they primarily concern:

| Context | Issues |
|---------|--------|
| **Spec tree** | #1, #42-#54, #27-#28, #39 |
| **Source tree** | #7-#15, #19-#21, #25-#26, #56-#81 |
| **Test tree** | #37-#38, #42-#54 |
| **UI (c2/d3)** | #16-#18, #29-#36, #38 |
| **Infrastructure** | #22-#24, #29-#34 |

### Step 4: Run Expert Panel Per Cluster

For each cluster, run the Enterprise Stakeholder Review Panel (A0-REVIEW-PANEL.md) to:
- Identify feature gaps between the issue description and the actual codebase
- Evaluate the decomposition of issues into atomic work units
- Recommend sub-issues where further decomposition is needed

### Step 5: Decompose into Atomic Implementation Units

Using the SeniorSoftwareEngineer expert's engineering judgment, break each issue into the smallest possible implementation units. The decomposition must be:

1. **Code-first**: Each unit maps to one isolatable source file or class set
2. **Parallelizable**: Units with no dependency on each other can be developed simultaneously
3. **Testable**: Each unit has a clear pass/fail criterion
4. **Incremental**: Each unit produces a working build increment

**Atomic unit template**:
```
- Source files: [list of .h/.cpp files to create/modify]
- Interface: [public API of the new/modified class]
- Test: [test method and what it verifies]
- Dependency: [which other units this must wait for]
- Effort: [XS/S/M/L/XL]
```

### Step 6: Create Sub-Issues on GitHub

For each atomic unit, create a sub-issue on GitHub using this complete workflow:

**6a. Determine parent epic and phase**: The sub-issue inherits its phase from the parent issue's phase (Open Source / Cloud Beta / Enterprise).

**6b. Create the issue**:
```bash
gh issue create --repo opensassi/a0 \
  --title "<title>" \
  --label "<labels>" \
  --body "<body with user stories>"
```
Capture the output URL — it contains the issue number.

**6c. Set the milestone** (matches the parent's phase):
```bash
# Map phase to milestone
Open Source → "Open Source"
Cloud Beta → "Cloud Beta"  
Enterprise → "Enterprise"

gh issue edit <N> --repo opensassi/a0 --milestone "<milestone-name>"
```

**6d. Add to the project board**:
```bash
# Get the issue's GraphQL node ID
NODE_ID=$(gh api graphql -f query="query { repository(owner:\"opensassi\", name:\"a0\") { issue(number:<N>) { id } } }" --jq '.data.repository.issue.id')

# Add to the project
gh api graphql -f query="mutation { addProjectV2ItemById(input: { projectId: \"PVT_kwDOEN7XcM4BZXJf\" contentId: \"$NODE_ID\" }) { item { id } } }"
```

**6e. Set the Phase custom field**:
```bash
# Get the project item ID (returned by the previous mutation)
# Phase field option IDs (look up fresh if they change):
# Open Source → lookup via: gh project field-list 1 --owner opensassi --jq '.fields[] | select(.name=="Phase") | .options[] | select(.name=="Open Source") | .id'
# Cloud Beta → same, with "Cloud Beta"
# Enterprise → same, with "Enterprise"

# Phase field ID
PHASE_FIELD_ID="PVTSSF_lADOEN7XcM4BZXJfzhUXmvU"

gh api graphql -f query="mutation { updateProjectV2ItemFieldValue(input: { projectId: \"PVT_kwDOEN7XcM4BZXJf\" itemId: \"$ITEM_ID\" fieldId: \"$PHASE_FIELD_ID\" value: { singleSelectOptionId: \"$PHASE_OPTION_ID\" } }) { projectV2Item { id } } }"
```

**6f. Set the Status custom field** (default: "Backlog"):
```bash
# Status field ID and option IDs:
STATUS_FIELD_ID="PVTSSF_lADOEN7XcM4BZXJfzhUWTUo"
# Backlog = f75ad846
# In progress = 47fc9ee4
# Done = 98236657

gh api graphql -f query="mutation { updateProjectV2ItemFieldValue(input: { projectId: \"PVT_kwDOEN7XcM4BZXJf\" itemId: \"$ITEM_ID\" fieldId: \"$STATUS_FIELD_ID\" value: { singleSelectOptionId: \"f75ad846\" } }) { projectV2Item { id } } }"
```

**6g. Set the Size field** (XS/S/M/L/XL):
```bash
SIZE_FIELD_ID="PVTSSF_lADOEN7XcM4BZXJfzhUWThI"
# XS = 6c6483d2, S = f784b110, M = 7515a9f1, L = 817d0097, XL = db339eb2

gh api graphql -f query="mutation { updateProjectV2ItemFieldValue(input: { projectId: \"PVT_kwDOEN7XcM4BZXJf\" itemId: \"$ITEM_ID\" fieldId: \"$SIZE_FIELD_ID\" value: { singleSelectOptionId: \"$SIZE_OPTION_ID\" } }) { projectV2Item { id } } }"
```

### Step 7: Link Sub-Issues to Parent Epic

Update the parent epic's body with a tasklist referencing the new sub-issue:

```bash
CURRENT=$(gh issue view <EPIC> --repo opensassi/a0 --json body --jq '.body')
gh issue edit <EPIC> --repo opensassi/a0 --body "${CURRENT}
- [ ] #<new-issue> <title>"
```

### Step 8: Begin Implementation

Start with:
1. Issues with no dependencies (Group 1 in each decomposition)
2. Highest priority items (Open Source phase first, then Cloud Beta)
3. Shortest cycle time items (XS/S effort) for quick wins

---

## Decomposition Methodology

### Code-First Isolation Boundaries

When decomposing an issue, look for these natural isolation boundaries in the codebase:

| Boundary | Example | Parallelizable |
|----------|---------|---------------|
| New `.h/.cpp` pair | Adding a new class | Yes — no existing code change |
| New method on existing class | Adding a feature to SkillManager | Depends on class header |
| Interface change | Modifying an abstract base class | No — must coordinate with all implementations |
| IPC protocol change | Adding message types | Yes — if backward compatible |
| CMakeLists.txt change | Adding new library target | Yes — at the end |
| Test file change | Adding test cases | Yes — in parallel with implementation |
| Web UI component | New JS file | Yes — independent of backend |

### Decomposition Priority

1. **Extract interfaces first** — define the contract before implementing
2. **Implement leaf classes first** — no internal dependencies
3. **Implement aggregators last** — depend on leaf classes
4. **Test in parallel** — write tests at the same time as implementation
5. **Integrate in sequence** — merge and verify after each atomic unit

---

## Project Board Field Reference

### Custom Fields

| Field | ID | Options | Used by views |
|-------|----|---------|---------------|
| Phase | `PVTSSF_lADOEN7XcM4BZXJfzhUXmvU` | Open Source, Cloud Beta, Enterprise | Phase-filtered views |
| Status | `PVTSSF_lADOEN7XcM4BZXJfzhUWTUo` | Backlog, Ready, In progress, In review, Done | Default |
| Size | `PVTSSF_lADOEN7XcM4BZXJfzhUWThI` | XS, S, M, L, XL | Planning |

### Milestones

| Milestone | Due date | Maps to Phase |
|-----------|----------|---------------|
| Open Source | 2026-06-15 | Open Source |
| Cloud Beta | 2026-07-01 | Cloud Beta |
| Enterprise | 2026-08-15 | Enterprise |

### Label Conventions

| Label | When to use |
|-------|-------------|
| `epic` | Container issue that groups sub-issues via tasklist |
| `control-plane` | Agent control and authorization features |
| `telemetry` | Observability, metrics, logging |
| `ui` | Web dashboard components |
| `persistence` | SQLite, data storage, schema |
| `llm-skills` | LLM providers, skill ecosystem |
| `monitoring` | collectd, host metrics |
| `testing` | Playwright, test infrastructure, ontogeny |
| `ipc` | Protocol and communication |

### Quick Reference: Phase Option IDs

Re-fetch these if they change (they are stable but unique per project):

```bash
gh project field-list 1 --owner opensassi --format json | \
  jq '.fields[] | select(.name=="Phase") | {id, options: [.options[] | {name, id}]}'
```

---

## Key References

| Resource | Path |
|----------|------|
| Expert panel prompt | `A0-REVIEW-PANEL.md` |
| Codebase review | `.artifacts/review.md` |
| Comparative eval | `.artifacts/comparative-evaluation.md` |
| Roadmap | `README.md` (also on project board) |
| Audit reconciliation | `handoffs/2026-06-01-enterprise-panel-roadmap/phase2-audit-reconciliation.md` |
| Project board | https://github.com/orgs/opensassi/projects/1 |
| Repository | https://github.com/opensassi/a0 |

---

## Quick Commands Reference

```bash
# List open issues with full context
gh issue list --repo opensassi/a0 --state open --limit 80 --json number,title,milestone,labels,assignees

# View issue details
gh issue view <N> --repo opensassi/a0

# Create issue with labels
gh issue create --repo opensassi/a0 --title "..." --label "..." --body "..."

# Set milestone (maps to phase)
gh issue edit <N> --repo opensassi/a0 --milestone "Open Source"

# Add to project board + set custom fields (complete workflow)
NODE_ID=$(gh api graphql -f query="query { repository(owner:\"opensassi\", name:\"a0\") { issue(number:<N>) { id } } }" --jq '.data.repository.issue.id')
ITEM_ID=$(gh api graphql -f query="mutation { addProjectV2ItemById(input: { projectId: \"PVT_kwDOEN7XcM4BZXJf\" contentId: \"$NODE_ID\" }) { item { id } } }" --jq '.data.addProjectV2ItemById.item.id')

# Set Phase field
gh api graphql -f query="mutation { updateProjectV2ItemFieldValue(input: { projectId: \"PVT_kwDOEN7XcM4BZXJf\" itemId: \"$ITEM_ID\" fieldId: \"PVTSSF_lADOEN7XcM4BZXJfzhUXmvU\" value: { singleSelectOptionId: \"<OS/CB/ENT_OPTION_ID>\" } }) { projectV2Item { id } } }"

# Set Status to Backlog (default for new issues)
gh api graphql -f query="mutation { updateProjectV2ItemFieldValue(input: { projectId: \"PVT_kwDOEN7XcM4BZXJf\" itemId: \"$ITEM_ID\" fieldId: \"PVTSSF_lADOEN7XcM4BZXJfzhUWTUo\" value: { singleSelectOptionId: \"f75ad846\" } }) { projectV2Item { id } } }"

# Update tasklist in epic body
CURRENT=$(gh issue view <EPIC> --repo opensassi/a0 --json body --jq '.body')
gh issue edit <EPIC> --repo opensassi/a0 --body "${CURRENT}
- [ ] #<new-issue> <title>"

# Update tasklist in epic body via file (for large bodies)
gh issue view <EPIC> --repo opensassi/a0 --json body --jq '.body' > /tmp/body.md
echo "- [ ] #<new-issue> <title>" >> /tmp/body.md
gh issue edit <EPIC> --repo opensassi/a0 --body-file /tmp/body.md
```
