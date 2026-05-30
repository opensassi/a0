Produce a complete C++ class declaration for every class in the system, following the project conventions documented in technical-specification.md under C++ Coding Conventions. Output the classes into the ## 2. Component Specifications section of technical-specification.md.

Each class declaration must include:
- Class name (PascalCase) and namespace
- #pragma once include guard
- Forward declarations for all dependent classes
- Public methods with full Doxygen \param and \retval documentation
- Private member variables with m_ prefix (m_p for pointers, m_b for bools, m_e for enums, m_c for strings)
- Private helper methods prefixed with x
- int return codes for error-signaling methods (0 = success)
- Output parameters as non-const pointers
- virtual ~ClassName() destructor
- In-class member initialization
- static constexpr for compile-time constants
- No method bodies — declarations only
- No inheritance — plain classes with composition

This command MUST be run before generate_architecture_diagram or generate_technical_specification.
