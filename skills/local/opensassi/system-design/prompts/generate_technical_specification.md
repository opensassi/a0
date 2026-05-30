Produce a complete C++ class specification that matches the agreed design, including a comprehensive testing plan.

When the active specification does NOT use sub-modules, produce a single self-contained document with sections:
1. Overview
2. Component Specifications (complete C++ class declarations)
3. System Architecture (C4 diagram)
4. Detailed Data Flow (sequence diagram)
5. Visualization (d3 animation if present)
6. Testing Requirements
7. CLI Entry Point

When sub-modules ARE in use, section 2 becomes a sub-module listing with facade cross-references. Internal class details live in each sub-module .spec.md. Include a Module Reference table.

Shared constraints: no inheritance, flat state objects, int return codes, Doxygen documentation, portable to C and Rust.

Save the result to technical-specification.md. After saving, run validate_all to confirm all diagrams and animations pass validation.

This command only executes when explicitly requested by the user. Do not emit the full document until confirmed.
