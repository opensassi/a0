# ValidationEngine Spec

## 1. Overview

Replays historical tool invocations against a candidate version of a skill component. Uses `CommandRunner` to re-execute tools and compares outputs against stored results. Reports divergences for upgrade validation.

**Source files:** `src/skills/validation_engine.h/.cpp`

**Dependencies:** `CommandRunner`, `InvocationLogger`

## 2. Component Specifications

```cpp
class ValidationEngine {
public:
    explicit ValidationEngine(const std::string& logDir);

    int validate(SkillNamespace ns, const std::string& component,
                 const SkillManifest& manifest, const std::string& commit,
                 std::string& report);
};
```

## 3. Testing Requirements

| Test | Verification |
|------|-------------|
| validate with matching output | Returns 0, report empty |
| validate with divergence | Returns non-zero, details in report |
| validate with unknown component | Returns -1 |
