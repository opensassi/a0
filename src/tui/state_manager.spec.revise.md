# StateManager Spec — New File Proposal

## §1. Overview

**Role:** Manages UI preferences with two-tier resolution: global defaults and per-item overrides. Every component reads its display state (collapsed/expanded) through StateManager, which resolves the effective value by checking per-item overrides first, then falling back to the global default.

**Pattern:** Cascading defaults (similar to `git config --system → --global → --local`). Only per-item overrides that DIFFER from the global default are stored. "Collapse All" sets the global default and clears all overrides — O(1).

**Source files:** `src/tui/state_manager.h`, `src/tui/state_manager.cpp`

**Dependencies:** None (standard library only)

**Lifecycle:**
1. Constructed at AgentTui creation time with default values
2. Components call `is_collapsed(id)` each render pass
3. User click toggles: `toggle_collapsed(id)` creates a per-item override
4. "Collapse All" / "Expand All": sets global, clears overrides

## §2. Component Specifications

```cpp
namespace a0::tui {

class StateManager {
public:
    StateManager();

    // Effective value resolution (tiered)
    bool is_collapsed(const std::string& data_id) const;

    // Per-item interaction
    void toggle_collapsed(const std::string& data_id);

    // Global controls — reset all overrides
    void collapse_all();
    void expand_all();

private:
    bool m_globalCollapsed = true;

    struct PerItem {
        std::optional<bool> collapsed;
    };
    std::unordered_map<std::string, PerItem> m_overrides;
};

} // namespace a0::tui
```

## §3-7

(Standard structure. Tests cover: global default applied correctly, per-item override diverges from global, collapse_all clears overrides, expand_all clears overrides, toggle multiple items independently.)
