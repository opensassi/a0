# Self-Hoisting the Product: How We Used Expert Panels to Define a0's Roadmap

**Date:** 2026-06-01
**Session:** `2026-06-01-enterprise-panel-roadmap-17f17158effe3l0X6pyZ5qxxTv`
**Tag:** `enterprise-panel`, `product-roadmap`, `security-audit`, `agentic-development`

---

In one eight-hour session, we went from zero to a complete product roadmap with 81 tracked issues, a zero-trust security model, a self-hoisting compiler test, and a public project board — all driven by a structured expert panel methodology running inside an AI agent.

This post is the post-session retrospective, written after `git finish-session` pushed the atomic commit to main.

## What We Built

The session was framed around a question: **can we use the same spec-driven development pipeline a0 provides to design a0 itself?**

The answer was yes, and the vehicle was the **Enterprise Stakeholder Review Panel** — a five-expert panel (SeniorSoftwareEngineer, SeniorSoftwareEngineerUser, EngineeringManager, ProductManager, CorporateExecutive) that evaluates the codebase, identifies feature gaps, and decomposes work into atomic, parallelizable implementation units.

The panel's output is structured issues on GitHub, organized by release phase, with acceptance criteria, implementation plans, and test requirements — the same format a0's spec pipeline uses to generate code. The system is self-hosting by design: a0 reads its own roadmap to generate its own implementation.

## The Numbers

| Metric | Value |
|--------|-------|
| Session duration | 8 hours prompter time |
| Equivalent SME time | 120-160 hours |
| AI multiplier | 15-20x |
| Issues created | 81 |
| Release phases | 3 (Open Source, Cloud Beta, Enterprise) |
| Expert panels | 2 (Enterprise Stakeholder, System Design Review) |
| Audit findings | 43 mapped to 21 blocker issues |
| Tests passed | 27/27 (100%) |

## The Architecture That Emerged

The session produced a four-tier daemon architecture that didn't exist when we started:

```
Cloud Service ──→ d3 ──→ c2 ──→ b1 ──→ a0
  (auth only)    (team)   (host)  (proj)  (agent)
```

The cloud service is deliberately zero-opex — it handles only authentication and routing. All agent execution, telemetry, and data storage run on customer infrastructure. Every operation is cryptographically signed through a JWT chain ([issue #81](https://github.com/opensassi/a0/issues/81)) that passes user identity from SAML/SSO all the way down to individual a0 tool calls.

The agent starts with **zero autonomous capability** ([issue #77](https://github.com/opensassi/a0/issues/77)). Every bash command, Docker execution, file write, and external API call requires explicit user authorization via a c2 dashboard pop-up. This is the opposite of most AI coding tools, which assume full filesystem access by default.

## The Self-Hoisting Compiler

The most interesting development was the **ontogeny test** ([issue #42](https://github.com/opensassi/a0/issues/42)) — a self-hoisting specification compiler. The agent reads its own technical specification files, generates C++ implementation code, compiles it, runs the E2E test suite against the generated binary, and iterates until the generated binary passes all tests.

This is the software equivalent of a self-hosting compiler (GCC compiling itself). If the spec tree is complete enough that a0 can generate itself from it, we have definitive proof that the spec-driven pipeline is working. The test was decomposed into 12 atomic sub-issues ([#43-#54](https://github.com/opensassi/a0/issues/43)) that can be implemented in parallel.

## The Session Flow

The session used a two-phase execution process:

**Phase 1 — Feature Review** (Enterprise Stakeholder Panel):
1. Load 80+ source files and all spec documents into context
2. Cluster open issues by system context (spec tree, source tree, test tree, UI, infrastructure)
3. Run the 5-expert panel against each cluster
4. Decompose issues into atomic, code-first implementation units
5. Create issues on GitHub with milestones, phase assignments, and size estimates

**Phase 2 — Audit Reconciliation** (System Design Review Panel):
1. Run the 7-expert technical panel over each new feature issue
2. Identify gaps between features and audit requirements
3. Add user stories or create new blocker issues

Both panels produce structured GitHub issues, not code. The actual implementation happens in follow-up sessions using the handoff process we defined.

## The Human Multiplier

The most striking metric from the session evaluation:

> **Prompter time:** 8 hours (reading, thinking, writing)
> **Equivalent SME time:** 120-160 hours
> **Multiplier:** 15-20x

The AI multiplier was highest in precisely the areas where traditional coding tools provide the least value: product strategy, security architecture, compliance planning, and multi-stakeholder tradeoff analysis. The panel methodology turns a single product manager's intuition into structured, auditable, multi-perspective analysis.

This is the opposite of the usual AI coding narrative. The focus wasn't on generating code faster — it was on **compressing the strategic decision-making cycle** that precedes and directs all code.

## What's Next

The handoff documents in `handoffs/2026-06-01-enterprise-panel-roadmap/` define the execution process for the next session: load context, enumerate issues, run panels, decompose, create sub-issues, implement. The process is self-similar — it applies at every level of granularity, from product roadmap down to individual method implementation.

The delivery timeline:
- **Open Source:** June 15 (2 weeks)
- **Cloud Beta:** July 1 (4 weeks)
- **Enterprise candidate:** August 15 (10 weeks)

All 81 issues are tracked publicly at [github.com/orgs/opensassi/projects/1](https://github.com/orgs/opensassi/projects/1).

---

*Post-session commentary written after `git finish-session` on commit `62e674e`, with 27/27 tests passing and the single atomic commit pushed to `opensassi/a0 main`.*
