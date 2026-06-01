## Phase 2: Audit Requirements Reconciliation

After the Enterprise Stakeholder Review Panel has created feature issues in Phase 1, run this reconciliation step to ensure every feature issue addresses applicable audit requirements from the Security & Compliance Audit epic.

### Step 1: Load Audit Context

Load the Security & Compliance Audit epic and all its sub-issues:

```bash
gh issue view 55 --repo opensassi/a0 --json body,title
gh issue list --repo opensassi/a0 --label "control-plane,persistence,llm-skills" --state open --limit 80
```

### Step 2: For Each New Feature Issue, Run the System-Design-Review Panel

The system-design-review panel (from `npx @opensassi/opencode system-design-review`) has 7 experts:

1. **CryptographyExpert** — crypto primitives, randomness, side channels
2. **DigitalPhysicalSecurityExpert** — network threats, access control, incident response
3. **DistributedSystemsExpert** — consistency, fault tolerance, back-pressure
4. **SoftwareEngineeringExpert** — correctness, testability, portability
5. **UserExperienceExpert** — API clarity, error messages, documentation
6. **LegalComplianceExpert** — GDPR, data protection, licensing
7. **EnergyAnalysisExpert** — hotspots, I/O patterns, algorithms

For each new feature issue, the panel evaluates:

- Does this feature touch any of the 5 security boundaries? (bash, docker, LLM API, file write, network)
- Does this feature involve user data? → LegalComplianceExpert reviews
- Does this feature expose a new API? → UserExperienceExpert reviews
- Does this feature change the persistence layer? → SoftwareEngineeringExpert reviews
- Does this feature involve distributed communication? → DistributedSystemsExpert reviews
- Does this feature involve cryptography or secrets? → CryptographyExpert reviews
- Does this feature have performance implications? → EnergyAnalysisExpert reviews

### Step 3: Identify Gaps

For each gap identified, the panel recommends one of:

**Option A: Add user stories to the existing feature issue**
If the feature partially addresses the audit requirement but lacks explicit acceptance criteria:
```bash
CURRENT=$(gh issue view <N> --repo opensassi/a0 --json body --jq '.body')
gh issue edit <N> --repo opensassi/a0 --body "${CURRENT}

---

### Audit Reconciliation

**Audit requirement**: <reference to specific audit finding>
**User story added**: <new user story with acceptance criteria>
**Originating expert**: <expert name>
**Audit issue reference**: #<audit-issue-number>"
```

**Option B: Create a new sub-issue linked to the same epic**
If the gap requires independent work:
```bash
# Create the issue
gh issue create --repo opensassi/a0 \
  --title "<title>" \
  --label "<labels>" \
  --body "<body>"

# Set milestone to match the parent epic's phase
gh issue edit <N> --repo opensassi/a0 --milestone "<Open Source|Cloud Beta|Enterprise>"

# Add to project board
NODE_ID=$(gh api graphql -f query="query { repository(owner:\"opensassi\", name:\"a0\") { issue(number:<N>) { id } } }" --jq '.data.repository.issue.id')
ITEM_ID=$(gh api graphql -f query="mutation { addProjectV2ItemById(input: { projectId: \"PVT_kwDOEN7XcM4BZXJf\" contentId: \"$NODE_ID\" }) { item { id } } }" --jq '.data.addProjectV2ItemById.item.id')

# Set Phase field (must match the parent's phase)
gh api graphql -f query="mutation { updateProjectV2ItemFieldValue(input: { projectId: \"PVT_kwDOEN7XcM4BZXJf\" itemId: \"$ITEM_ID\" fieldId: \"PVTSSF_lADOEN7XcM4BZXJfzhUXmvU\" value: { singleSelectOptionId: \"<OS/CB/ENT_OPTION_ID>\" } }) { projectV2Item { id } } }"

# Look up Phase option IDs:
# gh project field-list 1 --owner opensassi --format json | jq '.fields[] | select(.name=="Phase") | .options[] | {name, id}'

# Set Status to Backlog
gh api graphql -f query="mutation { updateProjectV2ItemFieldValue(input: { projectId: \"PVT_kwDOEN7XcM4BZXJf\" itemId: \"$ITEM_ID\" fieldId: \"PVTSSF_lADOEN7XcM4BZXJfzhUWTUo\" value: { singleSelectOptionId: \"f75ad846\" } }) { projectV2Item { id } } }"

# Set Size based on effort
gh api graphql -f query="mutation { updateProjectV2ItemFieldValue(input: { projectId: \"PVT_kwDOEN7XcM4BZXJf\" itemId: \"$ITEM_ID\" fieldId: \"PVTSSF_lADOEN7XcM4BZXJfzhUWThI\" value: { singleSelectOptionId: \"<XS/S/M/L/XL_OPTION>\" } }) { projectV2Item { id } } }"
```

**Option C: No action needed**
If the feature does not intersect with any audit requirement, record that finding:
```
#<feature-issue-number>: No audit intersection — no changes needed.
```

### Step 4: Update Tasklists

After modifying or creating issues, update the parent epic's body to include the new sub-issue in its tasklist:

```bash
# Via file to handle large bodies
gh issue view <EPIC> --repo opensassi/a0 --json body --jq '.body' > /tmp/body.md
echo "- [ ] #<new-issue> <title>" >> /tmp/body.md
gh issue edit <EPIC> --repo opensassi/a0 --body-file /tmp/body.md
```

### Step 5: Report

Output a summary table with phase context:

| Feature Issue | Phase | Audit Intersection | Action Taken | New/Modified Issues |
|---------------|-------|-------------------|--------------|---------------------|
| #<N> <title> | OS/CB/ENT | (yes/no) | (none/stories added/new issue) | #<M> |
| ... | ... | ... | ... | ... |

### Key Audit Requirements Reference

**Open Source blockers** (must reconcile: #56-#67, #77, #79):
- Every bash/docker/LLM/file-write operation must have an auth boundary
- AuthorizationStore: SQLite-backed, in-memory cache, Once/Session/Project/Global levels
- c2 auth prompt pop-up with non-dismissable grant selection
- Agent starts with zero capability (no implied grants)
- Zeroing of secrets on destruction
- Runtime log levels (not compile-time)
- Consistent error handling
- Schema validation for skill.json
- Process group cleanup in CommandRunner
- C++17 filesystem in VersionManager
- CLI --help and --version for all binaries
- No global mutable state

**Cloud Beta blockers** (must reconcile: #68-#76, #78, #80, #81):
- TLS verification required for all external connections
- Unix socket SO_PEERCRED verification
- Sensitive data filter before LLM API calls
- Data retention policy with automatic purging
- DPIA documentation for all data flows
- GDPR erasure (session delete with cascade)
- Docker --network=none by default
- Dependency license documentation
- Per-boundary rate limiting
- Auth audit log (append-only SQLite)
- Input validation on all c2 REST endpoints
- JWT authentication chain (cloud → d3 → c2 → b1 → a0)

### Project Board Setup Reminder

Every new issue MUST have all four fields set:
1. **Milestone** — maps to phase: "Open Source", "Cloud Beta", or "Enterprise"
2. **Phase** — custom field on the project board (same as milestone)
3. **Status** — default to "Backlog" for new issues
4. **Size** — XS/S/M/L/XL based on effort estimate

Without these fields, the issue will not appear in the correct phase-filtered board view.
