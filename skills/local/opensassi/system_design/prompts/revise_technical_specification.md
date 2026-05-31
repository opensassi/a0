Review the file-based technical specification (technical-specification.md) against all subsequent design decisions, corrections, and feedback. If the file does not exist, use the original user message as the reference.

This command is also the implicit default for any free-form user revision request. When the user says "change X to Y", silently perform the review-and-propose workflow.

Propose a structured list of revisions:

### Revision N

**Section affected**: <line or paragraph reference>
**Original text**: <verbatim quote>
**Proposed change**: <deletion / replacement / addition with the new text>
**Reason**: <brief explanation>

Do not rewrite the whole document — only propose specific, minimal changes. End by asking whether to apply with generate_technical_specification.
