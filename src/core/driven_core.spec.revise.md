# DrivenCore Spec — Revision Proposals

## turnSeq Emission

### Revision 3.1 — New m_turnSeq counter

**Section affected**: §2 Component Specifications, private members
**Original text**:
```cpp
    int m_turnCount = 0;
```
**Proposed change**: Add alongside existing counter:
```cpp
    int m_turnCount = 0;    // existing — max turn limit counter
    int m_turnSeq = 0;      // NEW — monotonically increasing turn sequence number
```
**Reason**: `m_turnSeq` is incremented on each `submitGoal()` call and emitted in `Complete.turnSeq`. It identifies which logical turn ended, regardless of how many LLM sub-requests were made within that turn.

### Revision 3.2 — Complete event carries turnSeq

**Section affected**: §2, `xFinishGoal` / `xFailGoal` emission
**Original text**: (implicit — Complete emitted without turn context)
**Proposed change**: Both `xFinishGoal` and `xFailGoal` emit:
```cpp
m_events.push_back(mpsc::Complete{m_sessionDbId, m_turnSeq, text});
```
`m_turnSeq` is incremented at the start of each `submitGoal()` call.
**Reason**: Every `Complete` event carries the logical turn number that ended, making it self-describing. The consumer can map it to its own turn tracking without a cursor.

### Revision 3.3 — Update Testing Requirements

**Section affected**: §5 Testing Requirements
**Proposed change**: Add test:
| Test | Verification |
|------|-------------|
| Complete.turnSeq after submitGoal | First goal has turnSeq=1, second has turnSeq=2 |
