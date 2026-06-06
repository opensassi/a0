# Personas Spec

## 1. Overview

Manages persona profiles — bundles of system prompts with associated skill/tool filters. Personas are loaded from a three-tier namespace directory (`personas/system/`, `personas/local/`, `personas/github_<user>/`). Each persona has a `persona.json` manifest and a `prompt.md` text file. The selected persona's skills and tools control which tool schemas are exposed to the LLM.

**Source files:** `src/personas.h`, `src/personas.cpp`

**Dependencies:** nlohmann/json

## 2. Component Specifications

```cpp
namespace a0::personas {

enum class PersonaNamespace { SYSTEM, LOCAL, GITHUB };

struct PersonaManifest {
    std::string name;
    std::string description;
    std::string promptFile;
    std::vector<std::string> skills;   // e.g. ["system_task-manager"]
    std::vector<std::string> tools;    // e.g. ["system_fs_read", "system_fs_glob"]
    PersonaNamespace ns;
    std::string dir;
};

struct Persona {
    PersonaManifest manifest;
    std::string prompt;   // resolved content of prompt.md
};

class PersonaLoader {
public:
    explicit PersonaLoader(const std::string& root = "./personas");

    /// Walk system/, local/, github_<user>/ directories.
    /// Returns -1 if root does not exist.
    int loadAll();

    /// Case-insensitive lookup.
    std::optional<Persona> getPersona(const std::string& name) const;

    /// All loaded personas across all namespaces.
    std::vector<Persona> listPersonas() const;

private:
    std::string m_root;
    std::vector<Persona> m_personas;

    int xLoadNamespace(const std::string& dirPath, PersonaNamespace ns);
    std::optional<PersonaManifest> xParseManifest(const std::string& dir) const;
    std::string xReadPrompt(const std::string& dir, const std::string& promptFile) const;
};

} // namespace a0::personas
```

## 3. Persona File Format

```json
{
  "name": "software-engineer",
  "description": "A skilled software engineer persona",
  "promptFile": "prompt.md",
  "skills": ["system_task-manager"],
  "tools": ["system_fs_read", "system_fs_glob", "system_fs_grep", "system_bash_bash"]
}
```

Required fields: `name`, `description`, `promptFile`. Optional: `skills` (array of `ns_comp` references), `tools` (array of qualified tool names).

## 4. Integration

`buildBasePrompt()` creates a `PersonaLoader` to load the persona prompt. `DrivenCore::xBuildToolSchemas()` uses the persona's `skills` and `tools` to filter which tool schemas are sent to the LLM. `main.cpp` reads the `--persona` CLI flag (default `"software-engineer"`), loads the manifest, and passes the skills/tools through `AppCoreThread` to `DrivenCore`.

## 5. Testing Requirements

| Test | Verification |
|------|-------------|
| loadAll with valid personas | Returns 0, personas indexed |
| loadAll with missing root | Returns -1 |
| loadAll with invalid persona.json | Skipped with warning |
| loadAll with missing prompt file | Loads with empty prompt |
| getPersona by exact name | Returns populated Persona |
| getPersona case-insensitive | Matches regardless of case |
| getPersona not found | Returns std::nullopt |
| listPersonas across namespaces | All namespaces represented |
| Skills/tools parsed from manifest | Arrays populated correctly |
| Namespace loading (system/local/github) | All three tiers scanned |
