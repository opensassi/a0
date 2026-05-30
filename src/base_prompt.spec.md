# BasePrompt Spec

## 1. Overview

Builds the base system prompt for all LLM sessions. Includes agent identity (binary SHA1), environment info (Linux kernel version, CWD), and parameterized descriptions of all available system tools. Called once at startup — result is cached and immutable for process lifetime.

**Source files:** `src/base_prompt.h/.cpp`

**Dependencies:** `SkillManager`, `uname(2)`

## 2. Component Specifications

```cpp
namespace a0 {

/// Build the base system prompt for all LLM sessions.
/// \param skillMgr  Loaded SkillManager for tool definition lookups
/// \returns A multi-line string with agent identity,
///          environment info, and tool schemas.
std::string buildBasePrompt(const skills::SkillManager* skillMgr);

} // namespace a0
```

## 3. Architecture

```mermaid
graph TB
    BP[buildBasePrompt]
    UNAME[uname]

    AGENT_CORE[AgentCore] --> BP
    BP -->|binary SHA| PROCFS[/proc/self/exe]
    BP --> UNAME
    BP -->|tool schemas| SKILL_MGR[SkillManager]
```

## 4. Testing Requirements

| Test | Verification |
|------|-------------|
| Prompt contains binary SHA | String includes 40-char hex hash |
| Prompt contains tool names | All registered tools listed in output |
| Prompt contains kernel version | `uname -r` output present |
