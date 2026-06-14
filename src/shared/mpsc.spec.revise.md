# MPSC Spec — Revision Proposals

## Event Struct Enrichment (Explicit Parent References)

### Revision 1.1 — ToolStart gains parentStreamId

**Section affected**: §2 Component Specifications, `ToolStart` struct
**Original text**:
```cpp
struct ToolStart  { int64_t invocationId; std::string toolCallId; std::string toolName; std::string arguments; };
```
**Proposed change**:
```cpp
struct ToolStart  { int64_t invocationId; int64_t parentStreamId; std::string toolCallId; std::string toolName; std::string arguments; };
```
**Reason**: `parentStreamId` links a tool call to the `LlmStart` event that produced it, making the parent-child relationship explicit in the event itself. Eliminates the need for a consumer-side cursor.

### Revision 1.2 — Complete gains turnSeq

**Section affected**: §2 Component Specifications, `Complete` struct
**Original text**:
```cpp
struct Complete   { int64_t sessionId; std::string summary; };
```
**Proposed change**:
```cpp
struct Complete   { int64_t sessionId; int64_t turnSeq; std::string summary; };
```
**Reason**: `turnSeq` identifies which logical turn this Complete event closes. Allows the consumer to map completion events to turns without maintaining a cursor or depending on event ordering.

### Revision 1.3 — Update Testing Requirements

**Section affected**: §5 Testing Requirements
**Proposed change**: Add test cases:
| Test | Verification |
|------|-------------|
| ToolStart.parentStreamId constructed | Field round-trips correctly |
| Complete.turnSeq constructed | Field round-trips correctly |
| AppCoreEvent variant dispatch with enriched types | All variants construct with new fields |
