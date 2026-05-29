# Trace Spec

## 1. Overview
Compile-time diagnostic logging macro. When `TRACE` is defined, `TRACE_LOG(msg)` writes `[TRACE] file:line msg` to `std::cerr`. When `TRACE` is undefined, the macro expands to nothing — zero runtime overhead.

## 2. Component Specifications

```cpp
#pragma once
#include <iostream>

#ifdef TRACE
/**
 * @param msg Expression to stream via operator<<
 * Logs: [TRACE] <__FILE__>:<__LINE__> <msg>
 * No-op when TRACE is not defined at compile time
 */
#define TRACE_LOG(msg) \
    std::cerr << "[TRACE] " << __FILE__ << ":" << __LINE__ << " " << msg << std::endl
#else
#define TRACE_LOG(msg)
#endif
```

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph Translation_Unit
        TU[Source File]
        MACRO[TRACE_LOG(msg) invocation]
    end

    subgraph Preprocessor
        DEF{TRACE defined?}
        EXPAND[Expand to cerr << ...]
        REMOVE[Expand to nothing]
    end

    subgraph Output
        STDOUT[std::cout]
        STDERR[std::cerr]
    end

    TU --> MACRO
    MACRO --> DEF
    DEF -->|Yes| EXPAND
    DEF -->|No| REMOVE
    EXPAND --> STDERR
```

## 4. Data Flow

```mermaid
sequenceDiagram
    participant Caller as Call Site
    participant PP as Preprocessor
    participant Stderr as std::cerr

    Note over Caller: #define TRACE

    Caller->>PP: TRACE_LOG("value = " << x)
    PP->>PP: Check if TRACE defined
    PP->>Stderr: "[TRACE] foo.cpp:42 value = 5\n"

    Note over Caller: TRACE not defined

    Caller->>PP: TRACE_LOG("value = " << x)
    PP->>PP: Expand to nothing
    Note over PP: Zero instructions emitted
```

## 5. Error Handling

| Error Condition | Signal | Notes |
|---|---|---|
| `msg` expression throws | Exception propagates | Macro does not catch |
| `std::cerr` unavailable | No output | Silently fails (stream badbit) |

## 6. Edge Cases

| Edge Case | Behavior |
|---|---|
| `TRACE` not defined | Zero codegen, zero overhead |
| `msg` contains commas | Parenthesized by macro, OK |
| `msg` is a stream manipulator | Passed through to `operator<<` |
| `msg` has side effects | Executed only when `TRACE` is defined |
| Multiple translation units | Each unit independently controlled by its own `#define TRACE` |

## 7. Testing Requirements

| Scenario | Test |
|---|---|
| `TRACE` defined, simple string | Verify output to stderr contains `[TRACE]`, file, line, and message |
| `TRACE` defined, multiple args | Verify streaming works with `operator<<` |
| `TRACE` undefined | Verify no output and no codegen (compile-time check) |
| Side effects in msg | Verify side effects absent when `TRACE` not defined |
| Large msg | Verify no truncation |
