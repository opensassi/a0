Generate a complete project-wide specification tree by scanning the current directory's source and test files. Multi-phase bottom-up process.

Phase 1 — Source file specs: For every source file across detected languages, generate a full .spec.md with component specs, Mermaid architecture diagram, Mermaid sequence diagram, D3 animation, and testing requirements. Never skip diagrams or animations.

Save file-level specs as source/<relative-path>/<FileName>.spec.md.

Language detection uses extension mapping (.cpp/.hpp -> C++, .js -> JavaScript, etc.). Skip node_modules/, .git/, .artifacts/, thirdparty/, sessions/, build/, dist/, vendor/.

Phase 1.5 — Source-test cross-references: each source spec lists its test specs; each test spec lists source specs.

Phase 2 — Sub-module organization: group related file specs into sub-modules based on dependency analysis. Create facade classes and sub-module .spec.md files. Cross-cutting concerns remain free-standing.

Phase 3 — Top-level specification: generate technical-specification.md with overview, sub-module listing, C4 diagram, sequence diagram, D3 animation, integration test plan, and CLI entry point. Include Module Reference and Free-Standing Components tables.

Validation: after each phase, run extract_artifacts and test_artifacts on each spec file. Do not proceed until validation passes.
