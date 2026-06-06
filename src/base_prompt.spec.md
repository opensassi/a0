# BasePrompt Spec

## 1. Overview

Loads the selected persona's `prompt.md` file with `{{BUILD_HASH}}`, `{{OS_INFO}}`, and `{{CWD}}` template substitution. Replaces the old `prompts/base.md` system — the persona manifest determines the prompt text.

**Source files:** `src/base_prompt.h/.cpp`, `src/personas.h/.cpp`

## 2. Component Specifications

```cpp
namespace a0 {

/// Loads the persona prompt and substitutes {{BUILD_HASH}}, {{OS_INFO}}, {{CWD}}.
/// \param skillMgr     Loaded SkillManager (reserved for future use)
/// \param personaName  Persona name (default: "software-engineer")
/// \returns            Substituted prompt text, or "ERROR: ..." on failure
std::string buildBasePrompt(const skills::SkillManager* skillMgr,
                             const std::string& personaName = "software-engineer");

} // namespace a0
```

## 3. Template Variables

| Variable | Source | Example |
|----------|--------|---------|
| `{{BUILD_HASH}}` | `BuildIdentity::binarySha1()` | `abc123def456` |
| `{{OS_INFO}}` | `uname()` sysname + release + machine | `Linux 6.2.0 x86_64` |
| `{{CWD}}` | `getcwd()` | `/home/user/project` |

## 4. Resolution

Creates a `PersonaLoader` with root `./personas`, calls `loadAll()`, then `getPersona(name)`. Returns the persona's prompt text with `{{VAR}}` substitution. A new `PersonaLoader` is created on each invocation (no caching).
