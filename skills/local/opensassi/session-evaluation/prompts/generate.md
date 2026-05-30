Analyze the entire conversation history from the current session and produce a structured Session Evaluation Summary.

## Process

1. Read the full conversation from context (all user messages, assistant responses, tool calls, and outputs).
2. Extract metadata:
   - Session timing (estimate duration from message volume and complexity)
   - User message count and complexity
   - Tool call frequency and types
   - Key decision points and course corrections
3. Apply the Session Evaluation Prompt template below to produce the structured report.
4. Fill in every section:
   - Session ID: Generate a unique ID based on today's date and a short topic slug (e.g. 2026-05-11-my-project-setup)
   - Date / Duration: Today's date; estimate prompter active time
   - Project / Context: One-paragraph description of the overall task and domain
   - Top-Level Component: The primary deliverable or highest-level output
   - Second-Level Modules: Bullet list of distinct sub-components created or advanced
   - Prompter Contributions: Decision-making, direction, and substantive corrections
   - Model Contributions: Drafting, analysis, structuring, diagnosis, and implementation
   - Prompter Time Estimate: Reading (~250 wpm), thinking, and writing breakdown
   - Model-Equivalent SME Time Estimate: Hours with task breakdown
   - Required SME Expertise: Granular bullet-point expertise areas
   - Aggregation Tags: 5-12 comma-separated keyword tags
5. Output the complete markdown inline for the user to review.
6. Extract the title slug (from the Session ID field) and hold it for potential export use.

## Estimation Guidelines

- Prompter reading: count words in all assistant responses, divide by 250 wpm, add 20% for technical comprehension overhead.
- Prompter thinking: estimate 30-50% of reading time depending on session complexity.
- Prompter writing: estimate from user message word count at ~100-150 wpm.
- SME time: break down into specific tasks (project setup, implementation, testing, debugging, documentation) at 2-8 hours each.
- SME expertise: list 6-12 granular domains (e.g. "C++17 CMake build system configuration", "Subprocess fork/exec/waitpid debugging"), not generic categories.

## Session Evaluation Prompt Template

Apply the following template exactly:

**Session ID:** [Generate a unique ID based on today's date and a short topic slug]

**Date / Duration:** [Date]; prompter active = [estimate total hours]

**Project / Context:**
[One paragraph describing the overall task and domain.]

**Top-Level Component:**
[The primary deliverable or highest-level output produced during this session.]

**Second-Level Modules:**
[Bullet list of distinct sub-components created or materially advanced.]

**Prompter Contributions:**
[Summarize the human's input: decisions, direction, corrections, strategy.]

**Model Contributions:**
[Summarize the AI's output: drafts, analysis, structure, diagnosis, implementation.]

**Prompter Time Estimate:**
- Reading and digesting model responses: ~[X] hours
- Thinking, strategizing, and weighing options: ~[Y] hours
- Writing messages and directives: ~[Z] hours
- **Total: [sum] hours**

**Model-Equivalent SME Time Estimate:**
[Total SME hours with task breakdown.]

**Required SME Expertise:**
[Bullet list of specific expertise domains required to replicate the model's contributions.]

**Aggregation Tags:**
[5-12 comma-separated keyword tags.]
