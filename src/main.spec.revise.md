# Main Spec — Revision Proposals

## Wiring Changes

### Revision 6.1 — Remove setWakeupFn from cmdTui

**Section affected**: §4 Data Flow, tui path sequence diagram
**Original text**:
```
    TUI->>TUI: create AgentTui(cmdSender, evtReceiver)
    TUI->>ACT: setWakeupFn
    TUI->>TUI_APP: run()
```
**Proposed change**:
```
    TUI->>TUI: create AgentTui(cmdSender, evtReceiver)
    TUI->>TUI_APP: run()
```
**Reason**: `setWakeupFn` was removed from `AppCoreThread`. The TUI is self-timed at 60fps via its own `sleep_for(16ms)` loop. The spec must match the current code.

### Revision 6.2 — Remove wakeupFn parameter from AppCoreThread::start

**Section affected**: §2, `cmdTui` and `cmdRun` flow descriptions
**Original text**: (descriptions reference `AppCoreThread::start(cmdRcvr, evtSender, wakeupFn)` with three parameters)
**Proposed change**: Update to two-parameter form: `AppCoreThread::start(cmdRcvr, evtSender)`.
**Reason**: The `wakeupFn` parameter was removed from the `start()` signature.

### Revision 6.3 — Update architecture diagram

**Section affected**: §3 Architecture Diagram
**Proposed change**: Remove the `setWakeupFn` arrow from the TUI path subgraph.
**Reason**: No longer wired.

### Revision 6.4 — Update Testing Requirements

**Section affected**: §5 Testing Requirements
**Proposed change**: Remove any references to wakeupFn.
**Reason**: Removed feature.
