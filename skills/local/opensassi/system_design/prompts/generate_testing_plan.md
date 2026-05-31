Produce a structured testing plan into the ## 6. Testing Requirements section of technical-specification.md, replacing existing content:

- Regression baseline: read the C++ Coding Conventions > Regression Test Baseline table. Those files are immutable.
- New unit tests: for each new class and public method, generate a test suite in a new file under test/. Use the project's comparison helpers or TEST/TESTT macros.
- Calling-order validation: tests for lifecycle method sequences (init -> encode -> uninit).
- Parameter range tests: all config fields, valid values accepted, invalid rejected.
- Integration tests: real program instances with real test data from test/data/. Bit-exact output comparison.
- Post-test cleanup via CTest FIXTURES_CLEANUP cleanup pattern.
