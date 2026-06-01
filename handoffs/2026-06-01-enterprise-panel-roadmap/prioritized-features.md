# Prioritized Product Features — Enterprise Stakeholder Review Panel

## Methodology

Rankings synthesized from 50 evaluation methods across 5 expert personas (SeniorSoftwareEngineer, SeniorSoftwareEngineerUser, EngineeringManager, ProductManager, CorporateExecutive), applied to both the a0 codebase review and the comparative evaluation against Claude Code, Aider, Devin, and Cursor.

**Weighting by stage**: a0 is alpha (day 4). Priority favors features that (1) unblock downstream capabilities, (2) reinforce unique competitive advantages, and (3) remove the highest-impact friction for enterprise adoption.

---

## Top 5 Features to Implement Next

### 1. SQLite Schema Versioning & Migration System

**Evidence**: The only remaining **Critical** issue in the expert review.

**Gap**: Schema tables are created via `CREATE TABLE IF NOT EXISTS` with zero version tracking. Any future schema change silently breaks all existing `.a0/db/sessions.db` files with no upgrade or rollback path.

**Why #1**:
- Every other feature depends on data stability — sessions, skills, invocations, streams all live in SQLite
- Without migrations, development velocity on persistence-adjacent features is capped (fear of breaking existing data)
- This is the foundation for data retention policies (Major finding), archival, and GDPR compliance
- Small implementation: add a `schema_version` table, version constant, and sequential migration scripts

**Implementation sketch**:
- Add `schema_version` table with `version INTEGER, applied_at INTEGER`
- Define version constant in `sqlite_store.cpp`
- On connect: query current version, apply unapplied migrations in order, update version
- First migration: add version tracking (matching current schema)

**Expert consensus**: SeniorSoftwareEngineer only flagged this, but it blocks progress across all domains.

---

### 2. Conversational Correction / Mid-Task Redirect

**Evidence**: SeniorSoftwareEngineerUser rated a0 a **—** on correction iteration loop vs. **+** for Claude Code and Aider. The #1 user-facing gap.

**Gap**: Currently, if the agent starts executing a tool chain with the wrong assumptions, the user must restart the entire skill or resend the goal. There is no natural "no, use staging" or "skip that step" redirect mid-execution.

**Why #2**:
- Directly addresses the biggest UX gap vs. every competitor
- Changes the interaction model from "submit and wait" to "collaborate and iterate"
- Relatively contained implementation: inject a bypass into the forked tool-calling loop that checks for redirect messages before each tool execution
- High visibility impact — this is the first thing users compare against Claude Code

**Implementation sketch**:
- Add a `redirect` message type in the REPL/stdin loop
- Before each tool call in `xRunForkedLoop`, check for pending user redirect
- On redirect: modify params, skip tool, or inject context correction
- Stream redirect acknowledgement via IPC to provide visible feedback

**Expert consensus**: Unanimous across SSEUser, ProductManager (iteration governance), EngineeringManager (adoption cost).

---

### 3. Skill Install from GitHub + MCP Auto-Packaging Pipeline

**Evidence**: `xInstallFromGit()` is a stub returning 0 (Major finding). The self-expanding skill ecosystem is a0's unique differentiator — no competitor can discover and package capabilities autonomously.

**Gap**: The skill distribution model (GitHub install, namespace isolation, version validation) is fully designed in the spec but not implemented. Users cannot install skills from external sources. The MCP server auto-conversion pipeline (described as a core feature) has no code path.

**Why #3**:
- This is a0's most defensible competitive advantage — other tools have fixed capabilities, a0 grows its own
- The architecture is already designed: `SkillManager::install()`, `VersionManager`, `ValidationEngine`
- MCP auto-packaging turns any MCP server into an a0 skill — massive leverage
- Unlocks ecosystem network effects: the more skills available, the more valuable a0 becomes

**Implementation sketch**:
- Phase 1: Implement `xInstallFromGit` — clone repo, parse `skill.json`, copy to `skills/github_<user>/`, archive current version via `VersionManager`
- Phase 2: Implement MCP server discovery — scan well-known registries, fetch server definitions, generate `skill.json` wrappers with tool schemas
- Phase 3: Add `a0 skill install/search/list` CLI commands

**Expert consensus**: EngineeringManager (ecosystem dependency), ProductManager (capability surface), CorporateExecutive (strategic differentiation).

---

### 4. Multi-Provider LLM Factory with Runtime Selection

**Evidence**: Flagged by 3 experts (SSE, EM, CE) as a Major concern. The `InferenceProvider` interface already exists — the gap is a factory implementation.

**Gap**: Currently frozen to DeepSeek. The interface supports swapping, but there is no runtime provider selection, no failover, and no fallback chain. If DeepSeek is down, the agent is dead.

**Why #4**:
- Smallest implementation effort vs. impact ratio on this list
- Directly enables cost optimization (route simple tasks to cheaper models)
- Directly addresses the top enterprise procurement concern (vendor lock-in)
- Enables local/offline operation via Ollama-backed provider

**Implementation sketch**:
- Add `ProviderFactory` class that registers providers by name
- `InferenceProvider* ProviderFactory::create(config)` returns the configured provider
- Config via CLI flag `--provider` or env var `A0_LLM_PROVIDER`
- Add simple failover: if primary provider fails, try secondary
- Provider config supports `deepseek`, `openai`, `anthropic`, `ollama` backends

**Expert consensus**: CorporateExecutive (vendor risk), EngineeringManager (cost flexibility), SeniorSoftwareEngineer (architecture cleanliness).

---

### 5. Cost Controls: Per-Session Token Tracking + Budget Caps

**Evidence**: EngineeringManager gave a0 a **—** on cost prediction. CorporateExecutive flagged lack of TCO model as Major. Aider supports local models; Cursor has fixed pricing — a0 has neither.

**Gap**: The agent can call the LLM API 25+ times per tool-calling loop with zero cost governance. A runaway session can generate unbounded API charges before anyone notices.

**Why #5**:
- Enterprise procurement requires cost predictability — without it, adoption stalls
- The data already exists: every invocation is logged in the `message` and `invocation` tables. The gap is aggregation and enforcement.
- Token tracking enables per-user billing, team budgets, and ROI measurement
- Rate limiting prevents runaway costs from buggy tool loops

**Implementation sketch**:
- Add token counters to `DeepSeekProvider` (track prompt + completion tokens per call)
- Store cumulative token usage in session table or new `usage` table
- Add budget cap config: `--max-tokens-per-session`, `--max-cost-per-session`
- Before each LLM call, check budget; if exceeded, return structured error
- Expose usage via `PersistenceStore` queries → c2 dashboard

**Expert consensus**: EngineeringManager (budgeting), CorporateExecutive (ROI), SeniorSoftwareEngineer (resource governance).

---

## Implementation Ordering Rationale

```
Week 1         Week 2          Week 3          Week 4
──────────     ──────────      ──────────      ──────────
Schema Migr    Conversational  GitHub Install  Multi-Provider
(Critical      Correction        + MCP           Factory
 data safety)  (UX gap vs      (Ecosystem      (Vendor risk
               competitors)     differentiator)  + cost flex)
                                      │
                                      ▼
                                  Cost Controls
                                  (Enterprise
                                   procurement
                                   blocker)
```

**Dependency graph**: Schema migrations first because every other feature reads/writes SQLite. Conversational correction second because it's the highest-impact UX fix independent of other work. GitHub install + MCP third because it unlocks the self-expanding ecosystem (the core differentiator). Multi-provider factory fourth (small effort, high procurement impact). Cost controls fifth (depends on token tracking from the provider layer).

---

## Features Deferred (with reasoning)

| Feature | Expert source | Reason for deferral |
|---------|---------------|---------------------|
| Real-time metrics dashboard | SSE, EM, CE | Audit trail exists; aggregation layer can be added after cost controls |
| Command allowlist/blocklist | SSE | Important for security but Docker sandboxing exists as first line of defense |
| Cross-platform (macOS) | SSE | Linux-only is acceptable for alpha; C++ portability can follow later |
| Error handling consistency | SSE | Important for maintainability but not blocking any user-facing capability |
| Data retention / GDPR | CE | Needed for compliance but premature in alpha; document intent first |
| CI configuration | SSE, EM | Test visibility is a procurement concern; document current workflow instead |
| Schema enforcement engine | SSE | Currently blocked by test infrastructure; useful for UX but not foundational |
| Natural-language introspection | SSEU | `show_skills` works; language fallback is polish, not blocking |
| IPC protocol versioning | SSE | Important at scale but not blocking current single-machine use |
| Progress indicators | SSEU | Important UX gap but lower impact than conversational correction |

---

*Generated by the Enterprise Stakeholder Review Panel. Prioritization based on: (1) severity-weighted expert consensus, (2) dependency analysis, (3) competitive positioning, (4) implementation leverage, (5) enterprise adoption blockers.*
