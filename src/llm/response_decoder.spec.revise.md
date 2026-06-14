# ResponseDecoder Spec — Revision Proposals

## parentStreamId Emission

### Revision 2.1 — ToolStart emission includes parentStreamId

**Section affected**: §4 Data Flow, SSE tool call sequence diagram
**Original text**: (diagram shows)
```
RD->>RD: push ToolStart / ToolChunk / ToolEnd
```
**Proposed change**: Update the sequence diagram to show ToolStart emitted with `parentStreamId` derived from the decoder's active `m_streamId`. Add a note: "The decoder tracks the active stream ID from `LlmStart`/`xEmitStart()` and injects it as `parentStreamId` into every subsequent `ToolStart` until the next `LlmStart`."
**Reason**: The decoder is the natural point to inject `parentStreamId` because it already tracks `m_streamId` (set during `xEmitStart()`) and has access to the current LLM stream context.

### Revision 2.2 — No structural change needed for m_streamId

**Section affected**: §2 Component Specifications, private members
**Original text**:
```cpp
    int64_t m_streamId = 0;
```
**Proposed change**: No change needed. `m_streamId` is already set in `xEmitStart()` before any tool-call processing in `xProcessJsonChunk`. The existing member is sufficient to serve as `parentStreamId`.
**Reason**: The stream ID is already available at the point where ToolStart events are constructed. Only the emission code needs updating to populate the new field.

### Revision 2.3 — Update Testing Requirements

**Section affected**: §5 Testing Requirements
**Proposed change**: Update the SSE tool call delta test to assert `parentStreamId` matches the stream's `m_streamId`.
| Test | Verification |
|------|-------------|
| SSE tool call delta with parentStreamId | ToolStart.parentStreamId == decoder.streamId() |
