Generate a Mermaid graph TB C4 container or component diagram showing the system's building blocks and their static relationships.

Include external actors, the main container, and internal components with directed edges indicating usage, delegation, and data flow. Label each node with its component name and optionally its type. The diagram must reference only the class names, properties, and relationships defined in the class specification.

When the design includes a user-facing visualisation, embed a Visualization sub-module as a nested container within the main system container. Name internal components after the metric or check they represent.

1. Embed the diagram in the ## 3. System Architecture section of technical-specification.md, replacing any existing content.
2. After embedding, run validate_all to confirm the diagram compiles.
3. This command MUST be run after generate_class_specification, never before.
