# Blog Post Generation Prompt

After `git finish-session` completes successfully (tests passing, commit pushed to main), generate a blog post documenting the session.

## Naming Convention

```
blog/<ISO-8601-date>-<session-title-slug>-<session-id-noprefix>.md
```

Example: `blog/2026-06-01-enterprise-panel-roadmap-17f17158effe3l0X6pyZ5qxxTv.md`

The title slug and session ID come from the git commit message (which matches the session evaluation sidecar filename).

## Front Matter

Each post starts with:

```markdown
# <Title>

**Date:** <ISO-8601-date>
**Session:** <commit-message>
**Tag:** <comma-separated tags, 3-6>
```

## Content Structure

The post is a post-session retrospective written after `git finish-session`. Cover:

1. **What We Built** — One-paragraph summary of the session's primary deliverable. Frame it in the context of a0's overall product vision (spec-driven development, self-hosting, agent-driven iteration).

2. **The Numbers** — Key metrics table from the session evaluation:
   | Metric | Value |
   |--------|-------|
   | Session duration | prompter hours |
   | Equivalent SME time | hours |
   | AI multiplier | ratio |
   | Issues created | count |
   | Release phases | count |
   | Key architectural decisions | summary |

3. **The Architecture That Emerged** — What was designed, built, or decided. Focus on architectural decisions that didn't exist before the session. Reference specific issue numbers with links.

4. **The Session Flow** — Describe the process used: what context was loaded, what panels were run, how issues were decomposed. This documents the methodology for future sessions.

5. **The Human Multiplier** — Reflect on the session evaluation's prompter vs. SME time estimates. Highlight where the AI multiplier was highest. This is the key insight for the blog's audience.

6. **What's Next** — Link to the handoff documents, mention the next delivery milestone, reference the project board.

7. **Post-script** — End with:

```markdown
*Post-session commentary written after `git finish-session` on commit `<hash>`, 
with <N>/<M> tests passing and the single atomic commit pushed to `opensassi/a0 main`.*
```

## Key References

- Session evaluation: `sessions/<commit-message>.md`
- Session archive: `sessions/<commit-message>.json.bz2`
- Handoff: `handoffs/<date>-<slug>/`
- Project board: https://github.com/orgs/opensassi/projects/1

## Style Guidelines

- Write for an audience of technical leaders (CTOs, engineering managers, senior architects)
- Focus on process and methodology, not implementation details
- Use concrete numbers from the session evaluation
- Reference specific issue numbers with GitHub links
- Keep it to 400-800 words
- Avoid marketing language — let the results speak
- The tone should be reflective and analytical, not promotional
