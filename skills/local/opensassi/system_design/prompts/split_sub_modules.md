Break the current monolithic specification into sub-modules. Only available when the specification does NOT already use sub-modules.

1. Propose a directory layout: each sub-module gets a directory under src/ and exports a single facade class.
2. Define each facade class interface and internal components.
3. Map monolithic spec classes to sub-module internal files.
4. Output a structured plan covering:
   - New directories to create
   - New .spec.md files to write
   - Content to extract from technical-specification.md into each .spec.md
   - The revised technical-specification.md structure
5. End by asking whether to apply with generate_technical_specification or iterate.
6. After applying, run validate_all to confirm all sub-module artifacts pass.

Conventions: sub-module dirs are lowercase plural (storage/, shard/), facade spec file is src/<module>/<Facade>.spec.md, internal files use CamelCase.
