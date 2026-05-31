# BasePrompt Spec

## 1. Overview

Loads a minimal identity prompt from `prompts/base.md` with `{{BUILD_HASH}}`, `{{OS_INFO}}`, and `{{CWD}}` template substitution. Skill discovery is handled by `tools_for_prompt` auto-injection in the forked loop — no tool descriptions are embedded in the base prompt.

**Source files:** `src/base_prompt.h/.cpp`

## 2. Component Specifications

```cpp
namespace a0 {

/// Loads `prompts/base.md` and substitutes {{BUILD_HASH}}, {{OS_INFO}}, {{CWD}}.
/// The template file contains only agent identity info — no tool listings.
std::string buildBasePrompt(const skills::SkillManager* skillMgr);

} // namespace a0
```

## 3. Template Variables

| Variable | Source | Example |
|----------|--------|---------|
| `{{BUILD_HASH}}` | `BuildIdentity::binarySha1()` | `abc123def456` |
| `{{OS_INFO}}` | `uname()` sysname + release + machine | `Linux 6.2.0 x86_64` |
| `{{CWD}}` | `getcwd()` | `/home/user/project` |

## 4. Template File

Located at `prompts/base.md` relative to the project root (or `--a0-dir`). Loaded at startup and cached for the process lifetime.
