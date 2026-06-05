# Review: Concurrency Model Specification

**File:** `specs/concurrency-model.md`  
**Reviewer:** system-design-review (7-expert panel)  
**Date:** 2026-06-05

---

## Part 1 – Consolidated Revisions

### Revision 1

**Section affected**: §7.5 IPC Framing Integrity, §5 (all data flows), §3.3 Stream Reader Thread

**Original text**:
> The JSON-line protocol (`src/ipc_protocol.cpp`) uses `\n` as a message delimiter. `recvMessage()` reads one byte at a time until a newline is found. This guarantees message boundaries even with partial reads, at the cost of performance (one `read()` syscall per byte). No message corruption can occur from fragmented TCP segment delivery.

**Proposed change**: Replace one-byte-at-a-time `read()` with buffered reads. Add a `receiveBuffer` to each socket connection that accumulates bytes and scans for `\n` delimiters in memory. The `poll()` loop should read up to 4096 bytes per invocation into the buffer, then scan for message boundaries. Only after at least one complete message has been buffered should the message be dispatched.

```cpp
// Replacement approach for recvMessage():
struct ConnectionBuffer {
    std::string buffer;
    int fd;
    // Returns 0 and populates msg if a complete message is available
    int tryRecv(ipc::Message& msg) {
        char tmp[4096];
        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n <= 0) return -1;
        buffer.append(tmp, n);
        auto nl = buffer.find('\n');
        if (nl == std::string::npos) return 1; // need more data
        std::string line = buffer.substr(0, nl);
        buffer.erase(0, nl + 1);
        return deserialize(line, msg) ? 0 : -1;
    }
};
```

**Reason**:

- **DistributedSystemsExpert**: The current design creates a back-pressure inversion. If a sender emits a 10 KB message, the receiver calls `read()` ~10,000 times, each with a syscall cost, while the sender blocks on socket send buffers. A malicious or malfunctioning b1 can monopolise the c2 listener thread by keeping it trapped in single-byte reads, starving other connections. This is a potential DoS vector.
- **EnergyAnalysisExpert**: ~10,000 syscalls per message is an energy hotspot. Each context switch into the kernel for a single byte consumes ~1µs on modern hardware — a 10 KB message costs ~10ms of kernel overhead. Buffered reads reduce this to ~3 syscalls per message regardless of size.
- **SoftwareEngineeringExpert**: The `recvMessage()` signature returns `int` with no way to indicate "partial data received, try again later." Adding a buffered connection state would enable non-blocking dispatch across multiple peers in the same `poll()` iteration, which the current design already supports via the `peerFds` vector in c2 and `m_agents` map in b1.

**Severity**: Major

---

### Revision 2

**Section affected**: §9.3 `g_timeoutFired` Read Without Volatile, §7.4 Signal Safety table

**Original text**:
```cpp
if (g_timeoutFired) {  // read site — NOT volatile-qualified
    result.timedOut = true;
    ...
}
```

**Proposed change**: Replace the C-style cast fix with `std::atomic_signal_fence` to enforce visibility between the signal handler write and the main-thread read, and change the declaration from `volatile sig_atomic_t` to `std::atomic<int>`.

```cpp
// Declaration (command_runner.cpp:15):
static std::atomic<int> g_timeoutFired{0};

// Signal handler (command_runner.cpp:21):
void alarmHandler(int) {
    g_timeoutFired.store(1, std::memory_order_relaxed);
}

// Read site (command_runner.cpp:348):
std::atomic_signal_fence(std::memory_order_acquire);
if (g_timeoutFired.load(std::memory_order_relaxed)) {
    result.timedOut = true;
    // ...
}
```

**Reason**:

- **SoftwareEngineeringExpert**: The spec correctly identifies the issue, but the proposed fix (`*(volatile sig_atomic_t*)&g_timeoutFired`) uses a C-style cast that bypasses type safety. `std::atomic<int>` with `std::atomic_signal_fence` provides formally correct signal-to-thread ordering semantics. The `volatile sig_atomic_t` pattern is a legacy C approach; C++11 provides a well-defined alternative via `<atomic>`.
- **CryptographyExpert**: While this particular flag is not a cryptographic secret, the pattern of relying on `volatile` for visibility is a common source of hard-to-debug correctness bugs (compiler hoisting the read before `alarm()`). Using `std::atomic` with proper fences eliminates the class of error.

**Severity**: Major

---

### Revision 3

**Section affected**: §3.3 Stream Reader Thread (C3), §6.1 Issue Detail: `close(stdinPipe1)` Without Lock

**Original text**:
```cpp
{
    std::lock_guard<std::mutex> lock(state->mutex);
    ::close(stdinPipe1);
    state->stdinFd = -1;  // invalidate fd
}
```

**Proposed change**: Extend the fix to also set `state->stdinFd = -1` under the lock, AND add a check in the write path to detect the closed state and return early instead of writing to fd -1 (which would succeed with undefined behaviour since fd 0/1/2 are stdin/stdout/stderr and may be open):

```cpp
// In sendInput() (command_runner.cpp:147):
void StreamHandle::sendInput(const std::string& data) {
    auto s = m_state.lock();
    if (!s) return;
    std::lock_guard<std::mutex> lock(s->mutex);
    if (s->stdinFd >= 0) {
        write(s->stdinFd, data.data(), data.size());
    }
    // If stdinFd == -1, the reader thread already closed it.
    // Silently drop the input — the child has exited.
}
```

**Reason**:

- **SoftwareEngineeringExpert**: The fix in the spec only addresses the close side. The write side must be robust against the fd having been set to -1 after invalidation. Without the guard on the write side, `sendInput()` after close would attempt to write to fd -1, which is a valid file descriptor (stderr) — the write would silently succeed and corrupt stderr output. The proposed `if (s->stdinFd >= 0)` guard in the existing code already handles this, but only if `stdinFd` is properly reset to -1 under the same lock, which the fix adds.
- **EnergyAnalysisExpert**: Adding the guard avoids unnecessary syscall overhead when writing to a closed stream — the write would otherwise consume kernel time for an I/O that will fail or corrupt data.

**Severity**: Minor

---

### Revision 4

**Section affected**: §6.2 Issue Detail: `DeepSeekProvider` from Background Thread

**Original text**:
> **Short-term:** Add a `std::mutex` to `DeepSeekProvider` and lock in both `complete()` and `setMockUrl()`

**Proposed change**: Add a note that `curl_easy_*` functions are themselves not thread-safe even beyond `m_baseUrl`. A mutex around `complete()` must guard the entire curl easy handle lifecycle (init, setopt, perform, cleanup). Additionally, consider replacing the background thread entirely with a `DrivenProvider`-style `curl_multi` approach.

```diff
+ **Note:** `curl_easy_perform()` is not thread-safe. Even with a mutex
+ protecting `m_baseUrl`, the curl easy handle itself must not be accessed
+ concurrently. The mutex must guard the entire `complete()` call, not
+ just the `m_baseUrl` read. A cleaner long-term solution is to migrate
+ `executeStreaming()` to use `DrivenProvider`'s `curl_multi` approach,
+ eliminating the background thread entirely.
```

**Reason**:

- **SoftwareEngineeringExpert**: The spec correctly identifies the race on `m_baseUrl` but understates the scope. `curl_easy_perform()` is explicitly documented by libcurl as not thread-safe for a single easy handle. Mutex-guarding `complete()` is necessary but locks the calling thread for the entire HTTP round-trip. This defeats the purpose of having a background thread.
- **EnergyAnalysisExpert**: Blocking a thread on `curl_easy_perform()` for the full duration of an LLM API call (potentially 30-60s) is an energy waste — the thread consumes scheduler resources while blocked on I/O. An event-driven `curl_multi` approach (as `DrivenProvider` already implements) would eliminate the dedicated thread entirely.

**Severity**: Minor

---

### Revision 5

**Section affected**: §3.7 c2 Two-Thread Architecture (C8, C9)

**Original text**:
> **Main thread:** uWebSockets HTTP/SSE server. Epoll-based internally. Calls `B1Registry`, `SseManager`, and `C2Listener::sendToB1()` under their respective mutexes.

**Proposed change**: Add a paragraph documenting the TLS/HTTPS capabilities of the c2 dashboard, referencing the `--ssl-key` / `--ssl-cert` flags from the c2 sub-module spec. Note that the default configuration binds to localhost (loopback only) and no authentication is required for localhost access.

```markdown
**Network access control:**
The c2 dashboard binds to `127.0.0.1:<port>` by default (loopback only).
This means only processes on the local machine can reach the HTTP API.
For remote access, c2 supports TLS via `--ssl-key <file> --ssl-cert <file>`
(see `src/c2/technical-specification.md §5.1` for details). There is no
HTTP-to-HTTPs redirect; TLS must be enabled explicitly. No authentication
is implemented on the REST API — all local processes with network access
to the loopback interface can read and write agent state. This is by design
for a development tool; production deployments should firewall the port
or enable TLS + reverse proxy authentication.
```

**Reason**:

- **DigitalPhysicalSecurityExpert**: The concurrency spec describes inter-thread communication flows inside c2 but does not mention the network-visible surface area. The c2 REST API (GET/POST endpoints listed in the c2 sub-module spec) is exposed on a TCP port. A reader of the concurrency model needs to understand that the HTTP server is accessible without authentication on the loopback interface. The TLS capability exists in the implementation but is undocumented in this spec, creating a gap between the two specifications.

**Severity**: Minor

---

### Revision 6

**Section affected**: §7.4 Signal Safety table — c2 SIGINT/SIGTERM handler

**Original text**:
> | `SIGINT`/`SIGTERM` (c2) | Calls `shutdown()`, `unlink()`, `_exit()` | **Acceptable** — signal handler exits immediately via `_exit()`, no stdlib cleanup needed |

**Proposed change**: Clarify that `shutdown()` and `unlink()` are called BEFORE `_exit()`, and that `_exit()` is the correct choice (not `exit()`) because it skips global destructors. Document that `dashboard.shutdown()` and `listener.shutdown()` are NOT async-signal-safe (they use mutexes and socket operations) — this creates a risk if the signal arrives while the dashboard or listener is in the middle of a mutex-protected operation.

```markdown
| `SIGINT`/`SIGTERM` (c2) | Calls `dashboard.shutdown()`, `listener.shutdown()`, `unlink()`, `_exit()` | **Constrained** — `shutdown()` calls are NOT async-signal-safe. If a signal arrives while the dashboard or listener holds a mutex (`m_b1Mutex`, `B1Registry::m_mutex`, `SseManager::m_mutex`), the shutdown call may deadlock. In practice, signal delivery during mutex acquisition is rare, and `_exit(0)` guarantees process termination even if shutdown hangs. A self-pipe trick or signalfd would provide signal-safe shutdown. |
```

**Reason**:

- **DigitalPhysicalSecurityExpert**: The signal safety claim is incomplete. Section 7.4 labels the c2 handler as "Acceptable" and notes `_exit()` is used, but the handler calls `shutdown()` (which acquires mutexes) and `unlink()` (which is async-signal-safe) BEFORE `_exit()`. If the signal interrupts the dashboard thread while it holds a mutex, `shutdown()` will spin waiting for a lock held by the interrupted thread — a deadlock. The `_exit()` call at the end becomes unreachable. This is a correctness concern in the signal safety domain.

**Severity**: Minor

---

### Revision 7

**Section affected**: §4.2 Synchronization Primitive Inventory — `hex_session_id` RNG line

**Original text**:
> | `hex_session_id` RNG | `thread_local` | Per-thread random device | `src/hex_session_id.h:9-11` |

**Proposed change**: Note whether the RNG uses a CSPRNG (e.g., `std::random_device{}/std::mt19937` vs `/dev/urandom` wrapping). If session IDs are used as security tokens (e.g., session UUID in IPC REGISTER messages), they must be unpredictable.

```diff
- | `hex_session_id` RNG | `thread_local` | Per-thread random device | `src/hex_session_id.h:9-11` |
+ | `hex_session_id` RNG | `thread_local` | Per-thread random device (std::mt19937 seeded from std::random_device) | `src/hex_session_id.h:9-11` |
+ 
+ **CSPRNG note:** `std::mt19937` is NOT a CSPRNG. It is a Mersenne Twister
+ PRNG — sufficient for session UUIDs used as identifiers, but NOT suitable
+ for security tokens, API keys, or nonce generation. If session IDs are
+ later used for authentication, replace with `std::random_device{}` or a
+ cryptographic RNG wrapper.
```

**Reason**:

- **CryptographyExpert**: The spec documents the `thread_local` RNG but does not specify the algorithm. `std::mt19937` seeded from `std::random_device` is common but is a predictable PRNG — given enough outputs, the internal state can be reconstructed. Session UUIDs used in IPC REGISTER messages identify agents to the supervisor — if an attacker on the same machine can predict session IDs, they could impersonate agents. Currently this is low risk because Unix socket permissions control access, but the spec should document the assumption.

**Severity**: Minor

---

### Revision 8

**Section affected**: §8.3 Integration Tests — INT-CONC-05

**Original text**:
> | INT-CONC-05 | DrvenCore submit + tick chain | Submit goal, tick 50 times | LLM response appears, no crash |

**Proposed change**: Fix typo: `DrvenCore` → `DrivenCore`.

```diff
- | INT-CONC-05 | DrvenCore submit + tick chain | Submit goal, tick 50 times | LLM response appears, no crash |
+ | INT-CONC-05 | DrivenCore submit + tick chain | Submit goal, tick 50 times | LLM response appears, no crash |
```

**Reason**:

- **UserExperienceExpert**: Typographical error in the specification degrades readability and suggests imprecision. "DrvenCore" is not a defined component name anywhere in the spec or implementation. The correct name is "DrivenCore" (defined in §3.2, §5.1).
- **SoftwareEngineeringExpert**: A typo in a test name creates confusion when searching the codebase — a reader might search for "DrvenCore" and find no matches, assuming the test refers to an unimplemented component.

**Severity**: Minor

---

## Part 2 – Debug Output

### Expert Panel Consensus

| Expert | Findings Raised | Severity Breakdown |
|--------|----------------|--------------------|
| **CryptographyExpert** | 1 (Minor) | — Critical: 0, Major: 0, Minor: 1 |
| **DigitalPhysicalSecurityExpert** | 2 (Minor) | — Critical: 0, Major: 0, Minor: 2 |
| **DistributedSystemsExpert** | 1 (Major) | — Critical: 0, Major: 1, Minor: 0 |
| **SoftwareEngineeringExpert** | 3 (1 Major, 2 Minor) | — Critical: 0, Major: 1, Minor: 2 |
| **UserExperienceExpert** | 1 (Minor) | — Critical: 0, Major: 0, Minor: 1 |
| **LegalComplianceExpert** | 0 | — No findings (scope does not cover data processing) |
| **EnergyAnalysisExpert** | 1 (Major, shared with DistributedSystems) | — Critical: 0, Major: 1, Minor: 0 |

### Residual Conflicts

None. All findings were resolved within single-expert domains. There was no cross-domain disagreement requiring escalation.

### Strengths Identified

1. **Thorough issue catalog** — The spec identifies 4 concrete issues (§9) with specific file locations, code quotes, and fix recommendations. This is the most actionable part of the document.

2. **Clear mutex domain isolation** — §4.1 diagram correctly shows three independent mutex domains in c2 with no cross-domain nesting. The spec correctly asserts deadlock-free design in §7.2.

3. **Comprehensive concurrency inventory** — §2.2 exhaustively lists all 9 concurrency contexts across 3 processes, with source file references, thread type, and status (Active vs Designed). No missing contexts were found.

4. **Sequence diagrams** — §5 contains 4 well-structured Mermaid sequence diagrams covering all major data-flow paths, including the unused AppCoreThread design.

### Coverage Gaps

1. **No CPU/memory profiling data** — The spec makes energy claims (e.g., "16ms sleep yields ~60 FPS") without profiling evidence. The EnergyAnalysisExpert would prefer benchmark data showing actual frame latency and CPU usage.

2. **No thread stack size documentation** — The spec does not document default stack sizes for background threads (C2–C5). On Linux, `std::thread` defaults to 8 MB — for a short-lived stream reader thread, this is excessive. Recommend adding a note or using `pthread_attr_setstacksize`.

3. **No pipe buffer sizing** — The spec documents the use of `pipe()` for subprocess I/O but does not mention pipe buffer sizes (default 64 KB on Linux). If a child process writes more than 64 KB before the reader thread reads, it will block. A note about `fcntl(fd, F_SETPIPE_SZ)` would improve robustness.

---

*End of review.*
