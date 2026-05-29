# DefaultDependencyResolver Spec

## 1. Overview

DefaultDependencyResolver implements DependencyResolver. It validates that all dependencies declared by a SkillPrompt are present in the SkillManager. Dependencies use qualified names (`ns:skill:name`) with no fallback or overrides. Resolution follows the transitive closure across skills. A `visited` set prevents infinite loops from circular references.

**Dependencies:** SkillManager (lookup-only)
**Lifecycle:** Instantiated with a SkillManager pointer; must outlive the resolver.

## 2. Component Specifications

```cpp
class DefaultDependencyResolver : public DependencyResolver {
public:
    /// \param skillManager  Non-owning pointer to the skill manager; must remain valid for the lifetime of this resolver.
    explicit DefaultDependencyResolver(SkillManager* skillManager);

    /// \param tool  The tool to check (ignored – tools always pass).
    /// \retval true  Always, because tools have no dependency semantics.
    bool checkToolDependencies(const Tool& tool) const override;

    /// \param skill  The skill whose dependency set to check.
    /// \retval true  When missingDependencies(skill) returns an empty vector.
    bool checkSkillDependencies(const Skill& skill) const override;

    /// \param skill  The skill to audit.
    /// \returns  Names of every transitive dependency that is neither a registered tool nor a registered skill.
    std::vector<std::string> missingDependencies(const Skill& skill) const override;

    /// Overload: resolve dependencies against SkillManager using qualified names.
    /// \param dependencies  Map of qualified dependency names → bare name alias.
    /// \returns  Missing qualified names.
    std::vector<std::string> missingDependencies(
        const std::unordered_map<std::string, std::string>& dependencies) const;

private:
    /// Recursive helper. Inserts skill.name into visited before iterating its deps.
    /// \param skill    Current skill node to expand.
    /// \param visited  Mutable set of already-visited skill names (cycle guard).
    /// \returns        Accumulated missing dependencies for this subtree.
    std::vector<std::string> missingDependenciesRecursive(
        const Skill& skill, std::set<std::string>& visited) const;

    /// Recursive helper for qualified-name resolution.
    std::vector<std::string> missingDepsQualified(
        const std::string& ns,
        const std::string& component,
        std::set<std::string>& visited) const;

    SkillManager* m_skillManager;
};
```

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph Interfaces
        DR[DependencyResolver]
        SM[SkillManager]
    end
    subgraph Implementation
        DDR[DefaultDependencyResolver]
    end
    DDR --|> DR
    DDR --> SM
    Skill[SkillPrompt] -- declares deps --> DDR
```

## 4. Data Flow

```mermaid
sequenceDiagram
    participant Client
    participant DDR as DefaultDependencyResolver
    participant SM as SkillManager

    Client->>DDR: missingDependencies(qualifiedDeps)
    activate DDR
    DDR->>DDR: missingDepsQualified(ns, component, visited)
    loop for each qualified dep
        DDR->>SM: getTool(dep)
        alt tool exists
            SM-->>DDR: SkillTool
            DDR->>DDR: skip (not missing)
        else tool not found
            SM-->>DDR: nullopt
            DDR->>SM: getPrompt(dep)
            alt prompt exists
                SM-->>DDR: SkillPrompt
                DDR->>DDR: recurse into dependency's manifest
            else prompt not found
                DDR->>DDR: add dep to missing list
            end
        end
    end
    DDR-->>Client: vector<string> missing
    deactivate DDR
```

## 5. Error Handling

| Scenario | Behaviour |
|----------|-----------|
| Dependency not found in SkillManager | Included in the returned missing list |
| Circular skill dependency (A→B→A) | Cut by `visited` set; not reported as missing |
| Duplicate dependency declared twice | Deduplicated by `visited` set |
| Transitive chain where an intermediate skill is missing | Intermediate skill name appears in missing list, its children are not visited |
| SkillManager pointer is null | Undefined behaviour (caller must ensure valid pointer) |

## 6. Edge Cases

| Case | Expected Result |
|------|----------------|
| Skill with empty `dependencies` vector | Empty result from `missingDependencies` |
| Skill depending only on registered tools | Empty result (tools always pass) |
| Deep chain (A→B→C→D where D is missing) | `[D]` is returned after full traversal |
| SkillManager loaded with zero skills | Every dependency reported as missing |
| Skill depending on itself | Visited set prevents re-entry; self-dependency not reported as missing |

## 7. Testing Requirements

| Method | Test Case | Input | Expected Output |
|--------|-----------|-------|----------------|
| `checkToolDependencies` | Any tool | `Tool{name="ls", ...}` | `true` |
| `checkSkillDependencies` | All deps satisfied | Skill with dep pointing to registered tool | `true` |
| `checkSkillDependencies` | Missing dep | Skill with dep absent from registry | `false` |
| `missingDependencies` | Fully satisfied | Skill with only tool deps | `[]` |
| `missingDependencies` | Single missing | Skill with one dep missing | `["missing_dep"]` |
| `missingDependencies` | Transitive missing | A→B→toolX, toolX missing | `["toolX"]` |
| `missingDependencies` | Circular | A→B→A, both registered | `[]` |
| `missingDependencies` | No deps | Skill with empty deps | `[]` |
