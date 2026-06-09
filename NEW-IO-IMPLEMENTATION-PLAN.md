# Persistence-First I/O Architecture — Implementation Plan

## Phase 0: Foundation — ResourceProvider Interface

**New file**: `src/resource_provider.h`

Abstract interface injected into all sub-modules at construction time. Sub-modules never see `PersistenceStore` or SQLite.

```cpp
namespace a0 {

using StreamId = int64_t;
using InvocationId = int64_t;

enum class ResourceType { LlmStream, ToolOutput, TerminalStream, ToolInvocation };

class ResourceHandle {
public:
    virtual ~ResourceHandle() = default;
    virtual int64_t id() const = 0;
    virtual bool hasMore() const = 0;
    virtual std::string readNext() = 0;
    virtual std::string read(int64_t offset, int64_t limit) = 0;
    virtual int64_t size() const = 0;
};

class ResourceWriter {
public:
    virtual ~ResourceWriter() = default;
    virtual int64_t id() const = 0;       // valid after first append
    virtual void append(const std::string& data) = 0;
    virtual void close() = 0;
    virtual bool closed() const = 0;
};

class ResourceProvider {
public:
    virtual ~ResourceProvider() = default;
    virtual std::unique_ptr<ResourceWriter> create(ResourceType type) = 0;
    virtual std::unique_ptr<ResourceHandle> open(ResourceType type, int64_t id) = 0;
};

} // namespace a0
```

- Inject `ResourceProvider*` into `DrivenCore`, `SkillManager`, `AppCoreThread`, `AgentTui` at construction time
- `SqliteResourceProvider` in `persistence/` with dedicated writer thread, WAL mode, LRU cache
- `NullResourceProvider` for tests

## Phase 1: Stream-Based LLM Output

- `ResponseDecoder` buffers tokens to `tokenFlushSize` (default 256 B), flushes to `ResourceWriter`
- First token: `ResourceProvider::create(LlmStream)` → emit `LlmStart{streamId, roundSeq}`
- Each flush: `writer.append(chunk)` → emit `LlmChunk{streamId, seq, text, isFinal:false}`
- Response complete: `writer.close()` → emit `LlmComplete{streamId, finishReason}`
- Full reconstruction: `SELECT data FROM stream_chunks WHERE stream_id=? ORDER BY seq`
- `stream_chunk.direction` gains `"llm_token"`
- Config: `--token-flush-size` / `A0_TOKEN_FLUSH_SIZE` (default 256)

## Phase 2: New AppCoreEvent Variants (Single Atomic Cut)

Replace the 6 core event types. No migration path.

```cpp
struct LlmStart      { int64_t streamId; int roundSeq; };
struct LlmChunk      { int64_t streamId; int seq; std::string text; bool isFinal; };
struct LlmComplete   { int64_t streamId; std::string finishReason; };
struct ToolStart     { int64_t invocationId; std::string toolCallId; std::string toolName; std::string arguments; };
struct ToolChunk     { int64_t invocationId; int seq; std::string text; std::string streamType; };
struct ToolEnd       { int64_t invocationId; int exitCode; int64_t totalBytes; std::string outputPreview; };
struct Complete      { int64_t sessionId; std::string summary; };
struct Error         { std::string source; int64_t contextId; std::string message; };
struct SessionReady  { int64_t dbId; std::string uuid; };
struct SessionList   { std::vector<Entry> entries; };
struct SessionHistory { int64_t dbId; std::string uuid; bool found; std::vector<SessionMessage> messages; };
struct LoadResource  { ResourceType type; int64_t id; int64_t offset; int64_t limit; };
struct LoadResourceResult { int64_t id; std::string data; };
struct FollowAgent   { std::string sessionUuid; };
struct UnfollowAgent { std::string sessionUuid; };

using Command = std::variant<SubmitGoal, Cancel, Shutdown, SetSession,
                             ListSessions, ResumeSession, LoadResource>;
using AppCoreEvent = std::variant<LlmStart, LlmChunk, LlmComplete,
                                  ToolStart, ToolChunk, ToolEnd,
                                  Complete, Error,
                                  SessionReady, SessionList, SessionHistory,
                                  LoadResourceResult>;
using B1Control = std::variant<FollowAgent, UnfollowAgent>;
```

Tool execution path:
1. `ToolStart`: `ResourceProvider::create(ToolInvocation)` → `invocationId` → emit
2. Streaming: `ResourceWriter::append()` → emit `ToolChunk` at `toolFlushSize` intervals
3. `ToolEnd`: writer.close() → build `outputPreview` → emit

Config knobs:
- `--tool-flush-size` / `A0_TOOL_FLUSH_SIZE` (default 4 KB)
- `--output-preview-size` / `A0_OUTPUT_PREVIEW_SIZE` (default 4 KB)

## Phase 3: Cap'n Proto IPC Protocol

- Add `capnproto` dependency (FetchContent or system package)
- `src/ipc/app_core_event.capnp` — schema for all event/command/control types
- `ipc_lib` includes generated code + existing `unix_socket.*`
- Single wire message: `AppCoreEvent` (no more `stream_data`, `user_prompt`, etc.)
- b1 decodes union tag for aggregates, conditionally forwards raw bytes
- c2 pushes to SSE, serves REST endpoints that proxy SQLite by handle
- Control: `FollowAgent`/`UnfollowAgent` (c2→b1)

Eliminated:
- c2 `EventStore` (separate SQLite DB)
- c2 `stream_data`/`stream_end` IPC handlers
- b1 `xHandleUserPrompt`, `xHandleStreamData`, `xHandleStreamEnd`, `xHandlePromptReply`
- b1 `m_streamOwners` map
- IPC JSON types `USER_PROMPT`, `PROMPT_REPLY`

## Phase 4: Delete Dead Code

| Files | Status |
|---|---|
| `src/agent_core.cpp/.h`, `.spec.md` | Not compiled, safe to delete |
| `src/skill_runner.cpp/.h`, `.spec.md` | Not compiled, safe to delete |
| `src/context_manager.cpp/.h`, `.spec.md` | Compiled but unreferenced, safe to delete |
| `src/dependency_resolver.cpp/.h`, `.spec.md` | Compiled but unreferenced, safe to delete |

Also remove `LIB_SOURCES` entries and test/CMake references.

## Phase 5: TUI Handle Integration

- Events carry inline previews for zero-DB live rendering
- Collapsed tool output shows `outputPreview` up to `outputPreviewSize`
- Expand sends `LoadResource{type, id, offset, limit}` via MPSC
- `AppCoreThread` handles → `ResourceProvider::open()` → `LoadResourceResult`
- TUI caches expanded resources in bounded LRU (`resourceCacheSize`)
- TUI boundary rules preserved (no `PersistenceStore*`)

## Phase 6: Sub-Module Restructuring

Reorganize monolithic `a0_lib` into sub-modules:

| New sub-module | Files |
|---|---|
| **`src/llm/`** | `llm_provider.h`, `driven_provider.*`, `deepseek_provider.*`, `response_decoder.*` |
| **`src/executor/`** | `command_runner.*`, `tool_runner.*`, `tool_state.*`, `dependency_graph.*`, `system_handlers.*`, `docker_security_filter.*`, `stream_registry.*`, `handler_results.h` |
| **`src/core/`** | `driven_core.*`, `app_core_thread.*` |
| **`src/bootstrap/`** | `a0_dir.*`, `base_prompt.*`, `personas.*`, `session_context.*`, `hex_session_id.h`, `daemonize.h` |
| **`src/ipc/`** | `unix_socket.*`, `ipc_protocol.*`, `app_core_event.capnp`, codegen |
| **`src/shared/`** | `agent_interfaces.h`, `mpsc.h`, `trace.h`, `resource_provider.h`, `handler_results.h` |

---

## Verification

After each phase:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

E2E tests use mock DeepSeek + Cap'n Proto IPC a0→b1→c2; browser reads from c2 REST (SQLite proxy).
