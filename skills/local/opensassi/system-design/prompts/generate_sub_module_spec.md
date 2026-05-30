Generate a complete .spec.md file for a named sub-module, scoped to that single module. Available only when a sub-module-aware top-level specification is active.

The generated file must follow this 7-section structure:
1. Overview — role, dependencies, lifecycle stages
2. Component Specifications — facade class C++ declaration, internal components
3. System Architecture — Mermaid C4 diagram
4. Detailed Data Flow — Mermaid sequence diagram of internal orchestration
5. Visualization — D3 animation concept
6. Testing Requirements — unit test table for every public method
7. CLI Entry Point — how this module is wired in

After writing, run extract_artifacts and test_artifacts to validate mermaid diagrams render correctly.
