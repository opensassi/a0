# HexSessionId Spec

## 1. Overview

Generates a 32-character hex session identifier using a thread-local Mersenne Twister PRNG seeded from `std::random_device`. The function is header-only and produces 16 random bytes formatted as lowercase hex, yielding 128 bits of entropy per identifier.

**Source files:** `src/hex_session_id.h` (header-only)

**Dependencies:** `string`, `sstream`, `iomanip`, `random`

## 2. Component Specifications

```cpp
#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <random>

/// Returns a 32-character hex string (e.g. "a1b2c3d4e5f6789012345678abcdef01").
/// Thread-safe via thread_local RNG.
/// 128 bits of entropy per call.
inline std::string generateHexSessionId();
```

### Implementation Detail

```cpp
inline std::string generateHexSessionId() {
    thread_local std::random_device rd;
    thread_local std::mt19937 rng(rd());
    thread_local std::uniform_int_distribution<int> dist(0, 255);
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; i++) {
        ss << std::setw(2) << dist(rng);
    }
    return ss.str();
}
```

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph Per_Thread
        RD[random_device]
        RNG[mt19937]
        DIST[uniform_int_distribution 0-255]
    end

    subgraph Call
        CALL[generateHexSessionId]
        LOOP[16 iterations]
        SS[ostringstream << hex << setfill('0')]
    end

    subgraph Output
        RESULT[32-char hex string]
    end

    RD -->|seed| RNG
    RNG --> DIST
    CALL --> DIST
    DIST -->|16 bytes| LOOP
    LOOP -->|format| SS
    SS --> RESULT
```

## 4. Testing Requirements

| Test | Verification |
|------|-------------|
| Length | Returns exactly 32 characters |
| Characters | All chars in `[0-9a-f]` |
| Uniqueness | Consecutive calls produce different values |
| Thread safety | Concurrent calls return unique values per thread |
| Determinism not required | Two seedings produce different sequences |
