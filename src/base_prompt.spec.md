# BasePrompt Spec

## 1. Overview

Builds the system prompt that describes the environment and available tools to the LLM. Includes OS info, CWD, binary build hash, tool definitions, and git usage restrictions.

**Source files:** `src/base_prompt.h/.cpp`

## 2. Component Specifications

```cpp
namespace a0 {

/// Returns a system prompt string such as:
/// "You are a0 build <sha>.
///  You run in a Linux ... x86_64 environment.
///  Your current working directory is ...
///  ...
///  For git operations, do NOT use bash. Use run_skill with a git skill path.
///  Browse skills with show_skills('/') and tools with show_skill_tools('/').
///
///  The following system tools are always available:
///  - bash: ... (git commands rejected)
///  - read: ...
///  - glob: ...
///  - grep: ...
///  - edit: ...
///  - write: ...
///  - run_skill: Execute a skill template.
///  - show_skills: Browse available skills.
///  - show_skill_tools: Browse available tools.
///  - tools_for_prompt: Analyze and recommend."
std::string buildBasePrompt(const skills::SkillManager* skillMgr);

} // namespace a0
```

## 3. Tool Descriptions in Prompt

| Tool | Description | Params |
|------|-------------|--------|
| bash | Executes bash (git commands rejected) | command, description, timeout, workdir |
| read | Read files/directories | filePath, offset, limit |
| glob | File pattern matching | pattern, path |
| grep | Content search with regex | pattern, path, include |
| edit | String replacements | filePath, oldString, newString, replaceAll |
| write | Write files | filePath, content |
| run_skill | Execute skill template eagerly | path |
| show_skills | Browse skill tree | path |
| show_skill_tools | Browse tool tree | path |
| tools_for_prompt | Analyze intent and recommend | prompt |

## 4. Git Restriction

The prompt includes:
- `"For git operations, do NOT use bash. Use run_skill with a git skill path."`
- `"Browse skills with show_skills('/') and tools with show_skill_tools('/')"`
- The bash tool description notes: `"IMPORTANT: git commands are rejected"`
