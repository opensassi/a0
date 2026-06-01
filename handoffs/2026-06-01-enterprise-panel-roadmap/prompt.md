```
/handoff-execute

You are continuing a session from the Enterprise Stakeholder Review Panel. Load the handoff context and execute the handoff workflow.

First, load these files into context:
- handoffs/2026-06-01-enterprise-panel-roadmap/handoff.md
- handoffs/2026-06-01-enterprise-panel-roadmap/A0-REVIEW-PANEL.md
- handoffs/2026-06-01-enterprise-panel-roadmap/review.md
- handoffs/2026-06-01-enterprise-panel-roadmap/roadmap.md
- handoffs/2026-06-01-enterprise-panel-roadmap/phase2-audit-reconciliation.md
- technical-specification.md
- src/*/technical-specification.md (all sub-module specs)

Then load all .cpp and .h files in src/ into context (use task agents in parallel to read them all).

Once all context is loaded, execute the workflow:

1. Enumerate all open issues via `gh issue list --repo opensassi/a0 --state open --limit 80 --json number,title,milestone,labels`
2. Cluster them by system context: [spec tree, source tree, test tree, UI, infrastructure]
3. For each cluster, run the Enterprise Stakeholder Review Panel (A0-REVIEW-PANEL.md) to identify feature gaps between issue descriptions and the actual codebase
4. Use the SeniorSoftwareEngineer expert to decompose each issue into atomic, parallelizable implementation units — code-first decomposition, one isolatable source change per unit
5. For each atomic unit, create a GitHub issue with the COMPLETE project board setup:

   a. `gh issue create` with title, labels, body
   b. `gh issue edit --milestone "<Phase>"` — milestone matches the parent's phase
   c. Add to project board via GraphQL mutation (addProjectV2ItemById)
   d. Set the Phase custom field via mutation (updateProjectV2ItemFieldValue)
   e. Set Status to "Backlog" via mutation
   f. Set Size via mutation (XS/S/M/L/XL based on effort estimate)

6. Link sub-issues to parent epic: update the parent's body with a tasklist entry `- [ ] #<num> <title>`

7. Run the audit reconciliation workflow from phase2-audit-reconciliation.md as Phase 2

8. Report: which issues were decomposed, how many atomic units were created, what can be implemented in parallel, phase distribution

Key constraints:
- Decomposition is code-first, not story-first — each atomic unit maps to one isolatable .h/.cpp change
- Independent units must be identified for parallel implementation
- Each new sub-issue MUST have: milestone set, Phase field set, Status set, Size set
- Phase option IDs for the project board: lookup via `gh project field-list 1 --owner opensassi --format json | jq '.fields[] | select(.name=="Phase") | .options[] | {name, id}'`
- Status option IDs: Backlog = f75ad846
- Size option IDs: XS = 6c6483d2, S = f784b110, M = 7515a9f1, L = 817d0097, XL = db339eb2
- Phase field ID: PVTSSF_lADOEN7XcM4BZXJfzhUXmvU
- Status field ID: PVTSSF_lADOEN7XcM4BZXJfzhUWTUo
- Size field ID: PVTSSF_lADOEN7XcM4BZXJfzhUWThI
- Project ID: PVT_kwDOEN7XcM4BZXJf
```
