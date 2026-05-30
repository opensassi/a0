Generate a self-contained Python script for Manim that visualizes the complete state machine. Save it as animation.py in the project root.

Structure:
- Scene 1: Initialization — boxes for each component, arrows from a KeyProvider, flashing to indicate seeded state.
- Scene 2: Processing of the first input block — show keystream generation, masking, XOR, state updates in strict order.
- Scene 3: Second block, faster, highlighting round-robin or chain-specific updates.
- Scene 4: Time-lapse of a full cycle showing the pattern of updates, flashing active elements.

Use colored rectangles, arrows, text labels, and simple grid representations. The script must be immediately runnable with manim -pql.
