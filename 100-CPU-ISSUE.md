# 100% CPU Spin / TUI Lock Investigation

## Initial Issue

The TUI intermittently locks up with 100% CPU on one core. No keyboard input is accepted, not even `Ctrl+C` or `:q`. The only recovery is killing the process from another terminal.

Reported as occurring intermittently with the prompt "list files in the current directory" across multiple builds.

---

## Reproduction

A stress test was built to reproduce the issue systematically. The test infrastructure uses Python PTY-based drivers (`test/e2e/conftest.py`) that fork `a0 tui` with a mock DeepSeek API server.

### Stress test approach

1. **Single-goal sessions**: Run `a0 tui` in Fullscreen mode, submit one goal, wait for completion, measure CPU. Repeated 100+ times. Result: 60-67% CPU during goal processing — normal FTXUI behavior, not the bug.

2. **Multi-goal sessions**: Submit 5 goals in a single TUI session, measuring CPU across all goals. Repeated 50+ times. Result: **100% reproduction of the 84% CPU ramp** — CPU climbed from 24% to 84% over ~20 seconds on every run.

3. **Scrolling tests**: Same issue in FixedSize test mode — CPU climb consistent.

### Key clue from strace

A 5-second `strace -c` on the spinning process revealed:

```
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
100.00    0.552180           2    194325           rt_sigreturn
------ ----------- ----------- --------- --------- ----------------
100.00    0.552180           2    194325           total
```

**194,325 `rt_sigreturn` calls in 5 seconds** — 38,865 signals per second. The `rt_sigreturn` is the return from a signal handler. Stracing with `-e trace=rt_sigreturn,kill,tkill` showed:

```
--- SIGSEGV {si_signo=SIGSEGV, si_code=SEGV_MAPERR, si_addr=0x4e00000008} ---
rt_sigreturn({mask=[]})
--- SIGSEGV {si_signo=SIGSEGV, si_code=SEGV_MAPERR, si_addr=0x4e00000008} ---
rt_sigreturn({mask=[]})
...
```

The main thread was hitting **38,865 SIGSEGV faults per second** at address `0x4e00000008`.

### Identifying the crash site

A GDB backtrace during the spin showed the main thread in:

```
#0  std::_Sp_counted_base::_M_release(this=0x4e00000000)
#1  std::__shared_count::~__shared_count()
#2  std::__shared_ptr<ftxui::Node>::~__shared_ptr()
#3  std::shared_ptr<ftxui::Node>::~shared_ptr()
#4  void std::_Destroy<std::shared_ptr<ftxui::Node>>()
#5  std::vector<...>::~vector()
#6  ftxui::Node::~Node()
#7  ftxui::VBox::~VBox()
#8  std::_Sp_counted_ptr_inplace<ftxui::VBox>::_M_dispose()
#9  std::_Sp_counted_base::_M_release()
#10 ... (recursive destruction pattern repeats with Reflect, Focus, FgColor nodes)
```

The address `0x4e00000000` was a freed `_Sp_counted_base` control block — a **use-after-free** in FTXUI's shared_ptr reference counting.

### The `ftxui::reflect` mechanism

The address `0x0000004E00000008` matched a `Box` struct (4 `int` fields):

| Byte offset | Field | Value | Hex |
|------------|-------|-------|-----|
| 0-3 | `x_min` | 0 | `0x00000000` |
| 4-7 | `x_max` | 78 (0x4E) | `0x0000004E` |
| 8-11 | `y_min` | 8 | `0x00000008` |

When read as a 64-bit pointer by `_M_release(this)`: `0x0000004E00000008`. The `_M_release` function tries to decrement the refcount at `this->_M_use_count`, which is at offset 0 from `this` — i.e., address `0x0000004E00000008`, which is unmapped. SIGSEGV.

The `Box` values (x_min=0, x_max=78, y_min=8) are typical terminal coordinates being written **through a dangling `Box&` reference** held by a `ftxui::Reflect` node.

### Root cause in `message_panel.cpp`

```cpp
// src/tui/message_panel.cpp, xRenderAssistant(), lines 339-340
m_impl->toolHits.push_back({entryIdx, ci, ftxui::Box{}});
elems.push_back(xRenderToolBlock(child)
    | ftxui::reflect(m_impl->toolHits.back().box));
```

When an assistant entry has **2 or more tool children**, the vector reallocation sequence is:

1. First tool child: `push_back` allocates slot 0. `ftxui::reflect(m_impl->toolHits.back().box)` stores a `Box&` reference to `toolHits[0].box`.

2. Second tool child: `push_back` **reallocates** the vector. All existing `Box&` references dangle — they point to the old freed memory.

3. Subsequent `make_shared` allocations (e.g., `Reflect` node for the second child) reuse the freed memory for a `_Sp_counted_base` control block.

4. FTXUI's rendering pipeline calls `Reflect::SetBox()`, which writes through the dangling `Box&` reference, **corrupting the control block** at the reused memory.

5. When the render tree is destroyed (at end of `Draw()`), `_M_release` accesses the corrupted control block — SIGSEGV.

The `entryBoxes` vector in the same file uses `resize()` at the top (no reallocation during the loop) and does not have this bug.

---

## Fixes

### Fix 1: Pre-allocate `toolHits` capacity (`message_panel.cpp`)

Pre-count the tool children and `reserve()` capacity before the loop:

```cpp
// Pre-allocate toolHits capacity so push_back doesn't reallocate
size_t toolChildCount = 0;
for (const auto& c : entry.children)
    if (c.role == MessageRole::Tool) toolChildCount++;
m_impl->toolHits.reserve(m_impl->toolHits.size() + toolChildCount);
```

This prevents vector reallocation during the loop, keeping all `Box&` references valid.

### Fix 2: Remove `wakeupFn` cross-thread signaling (`app_core_thread.h/.cpp`, `main.cpp`)

The AppCoreThread previously called `wakeupFn()` after sending MPSC events, which posted an empty task to FTXUI's queue. This was intended to wake the UI thread for immediate rendering. Removing it makes the TUI self-timed at 60fps via its own `sleep_for(16ms)` frame loop, eliminating a source of unnecessary render cycles and cross-thread coupling.

### Fix 3: Swap loop order (`agent_tui.cpp`)

Changed from:
```cpp
loop.RunOnce();     // render first
drainEvents();      // then process events
```
To:
```cpp
drainEvents();      // process events first
loop.RunOnce();     // render with fresh state
```

This eliminates a wasteful render of stale state.

### Fix 4: SIGSEGV safety net (`agent_tui.cpp`)

FTXUI installs an empty `SIGSEGV` handler. Returning from `SIGSEGV` re-executes the faulting instruction, so a permanent fault (like the use-after-free) creates an infinite SIGSEGV loop at 100% CPU.

Replaced with a handler that:
1. Restores the terminal: show cursor, exit alternate screen, disable bracketed paste, restore `termios` (ICANON, ECHO, ISIG)
2. Re-raises with `SIG_DFL` for a core dump with a clean terminal

---

## Files Changed

```
src/tui/message_panel.cpp       | 15 ++++++--
src/tui/agent_tui.cpp           | 55 ++++++++++++++++++++-----
src/core/app_core_thread.cpp    |  8 ----
src/core/app_core_thread.h      | 12 ------
src/main.cpp                    |  7 +---
CMakeLists.txt                  |  4 +-
test/e2e/conftest.py            | 10 ++---
test/e2e/mock_deepseek_server.py| 21 +++++++++-
```

---

## Lessons Learned

1. **Vector reallocation during `ftxui::reflect()` is dangerous.** Any `reserve()` call that changes capacity invalidates the `Box&` references stored inside `Reflect` nodes. Pre-allocate or use a container with stable references (e.g., `deque`, list, or a fixed-size array).

2. **FTXUI catches SIGSEGV with an empty handler.** This design turns an otherwise-fatal crash into either a silent infinite spin (the use-after-free) or a catastrophic corruption (if the faulting memory happens to be mapped). Always override with `SIG_DFL` or a proper cleanup handler.

3. **`strace -c` is the fastest way to identify signal storms.** 194k `rt_sigreturn` in 5s was the giveaway. Followed by `strace -e trace=rt_sigreturn` to capture the signal type.

4. **`perf record -g` for user-space, `strace -c` for system calls.** Perf captures CPU samples but may miss kernel-mode time without root. Strace catches syscall patterns but adds overhead. Use both.

5. **TRACE_LOG instrumentation + steady_clock timing** is invaluable for distinguishing between "thread is actually busy" vs. "thread is stuck in a signal loop." The former shows high utime; the latter shows high stime.
