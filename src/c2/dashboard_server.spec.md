# DashboardServer Spec

## 1. Overview

HTTP dashboard server using uWebSockets. Serves JSON REST API endpoints, an SSE event stream, and static Web UI files from disk. Runs on the main thread alongside `C2Listener` on a separate thread.

**Dependencies:** uWebSockets, `B1Registry`, `SseManager`, `EventStore`, `C2Listener`, SQLite3, POSIX

**Lifecycle:** Created at c2 startup, blocks on `run()` until shutdown.

## 2. Component Specifications

```cpp
namespace a0::c2 {

class DashboardServer {
public:
    DashboardServer(int port, B1Registry* registry, SseManager* sse,
                    EventStore* events, C2Listener* listener,
                    const std::string& webRoot,
                    const std::string& sslKey = "",
                    const std::string& sslCert = "");
    ~DashboardServer();

    int run();
    void shutdown();

private:
    int m_port;
    B1Registry* m_registry;
    SseManager* m_sse;
    EventStore* m_events;
    C2Listener* m_listener;
    std::string m_webRoot;
    std::string m_sslKey;
    std::string m_sslCert;
    bool m_running = false;
    bool m_shutdownRequested = false;
    struct us_listen_socket_t* m_listenToken = nullptr;

    // Track terminals launched directly (no b1) for status polling
    std::unordered_map<std::string, std::string> m_directTerminals;

    template<typename App> void xSetupRoutes(App* app);
    template<typename Res> void xServeStatic(Res* res, const std::string& urlPath);
    std::string xBuildStatusJson();
    std::string xBuildStatsJson();
    std::string xBuildPendingJson();
    static std::string xMimeType(const std::string& path);
    static std::string xReadFile(const std::string& path);
};

} // namespace a0::c2
```

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph DashboardServer
        RUN[run]
        SETUP[xSetupRoutes]
        STATIC[xServeStatic]
    end

    subgraph Routes
        STATUS[GET /api/status]
        STATS[GET /api/stats]
        SSE[GET /api/events]
        PENDING[GET /api/events/pending]
        B1[GET /api/b1/:pid]
        B1AGENTS[GET /api/b1/:pid/agents]
        AGENT[GET /api/agent/:uuid]
        MSGS_GET[GET /api/agent/:uuid/messages]
        MSGS_POST[POST /api/agent/:uuid/messages]
        DISMISS[DELETE /api/agent/:uuid/prompt/:toolCallId]
        PING[POST /api/ping]
        TERM_OPEN[POST /api/terminal/open]
        TERM_STATUS[GET /api/terminal/status/:terminalId]
        STREAM_INPUT[POST /api/stream/:id/input]
        STREAM_CHUNKS[GET /api/stream/:id/chunks]
        SESSION_STREAMS[GET /api/session/:uuid/streams]
        STATIC_FILES[GET /*]
    end

    subgraph Dependencies
        REG[B1Registry]
        SSE_MGR[SseManager]
        EVENTS[EventStore]
        LISTENER[C2Listener]
        FS[web_root/ files]
    end

    BROWSER[Web Browser] --> STATUS
    BROWSER --> STATS
    BROWSER --> SSE
    BROWSER --> PENDING
    BROWSER --> B1
    BROWSER --> B1AGENTS
    BROWSER --> AGENT
    BROWSER --> MSGS_GET
    BROWSER --> MSGS_POST
    BROWSER --> DISMISS
    BROWSER --> PING
    BROWSER --> TERM_OPEN
    BROWSER --> TERM_STATUS
    BROWSER --> STREAM_INPUT
    BROWSER --> STREAM_CHUNKS
    BROWSER --> SESSION_STREAMS
    BROWSER --> STATIC_FILES

    STATUS --> REG
    STATS --> REG
    B1 --> REG
    B1AGENTS --> REG
    AGENT --> REG
    SSE --> SSE_MGR
    PENDING --> EVENTS
    MSGS_GET --> REG
    MSGS_POST --> EVENTS
    MSGS_POST --> LISTENER
    DISMISS --> EVENTS
    STATIC_FILES --> FS
    PING --> SSE_MGR
    TERM_OPEN --> REG
    TERM_OPEN --> LISTENER
    TERM_STATUS --> FS
    STREAM_INPUT --> LISTENER
    STREAM_CHUNKS --> FS
    SESSION_STREAMS --> REG
```

## 4. Data Flow

### 4.1 SSE Connection

```mermaid
sequenceDiagram
    participant Browser
    participant DS as DashboardServer
    participant SSE as SseManager
    participant REG as B1Registry

    Browser->>DS: GET /api/events
    DS->>DS: writeHeader text/event-stream
    DS->>SSE: addClient(sendFn)
    SSE-->>DS: clientId
    DS-->>Browser: write(id:1\nevent:connected\ndata:{}\n\n)

    Note over Browser,REG: (live events streamed)

    REG->>SSE: broadcast("b1_connected", {...})
    SSE->>Browser: write(id:2\nevent:b1_connected\ndata:{...}\n\n)
```

### 4.2 Static File Serving

```mermaid
sequenceDiagram
    participant Browser
    participant DS as DashboardServer
    participant FS as Filesystem

    Browser->>DS: GET /js/app.js
    DS->>DS: xMimeType("app.js") → text/javascript
    DS->>FS: read web_root/js/app.js
    FS-->>DS: file contents
    DS-->>Browser: 200, Content-Type: text/javascript
```

### 4.3 User Prompt Resolution

```mermaid
sequenceDiagram
    participant Browser
    participant DS as DashboardServer
    participant EV as EventStore
    participant REG as B1Registry
    participant L as C2Listener

    Browser->>DS: POST /api/agent/:uuid/messages {role:"tool",tool_call_id:"call_abc",content:"yes"}
    DS->>EV: resolvePrompt(uuid, "call_abc")
    EV-->>DS: 0
    DS->>REG: listB1s()
    REG-->>DS: [{pid:5678, agents:[{sessionUuid:uuid}]}]
    DS->>L: sendToB1(5678, PROMPT_REPLY)
    DS-->>Browser: 200 {"status":"ok"}
```

### 4.4 Terminal Launch

```mermaid
sequenceDiagram
    participant Browser
    participant DS as DashboardServer
    participant REG as B1Registry
    participant L as C2Listener
    participant FS as Filesystem

    Browser->>DS: POST /api/terminal/open {cwd:"/p",contextType:"host"}
    DS->>REG: listB1s()
    REG-->>DS: [{pid:5678, workdir:"/p"}, ...]
    alt b1 found
        DS->>L: sendToB1(5678, TERMINAL_OPEN)
        L-->>DS: 0
    else no b1
        DS->>FS: readlink(/proc/self/exe) → a0 path
        DS->>DS: fork/exec a0 terminal --terminal-id ...
        DS->>DS: store m_directTerminals[id]=cwd
    end
    DS-->>Browser: 200 {terminalId:"term_123"}
```

### 4.5 Stream Chunks API

```mermaid
sequenceDiagram
    participant Browser
    participant DS as DashboardServer
    participant DB as SQLite

    Browser->>DS: GET /api/stream/:id/chunks
    DS->>DB: SELECT * FROM stream_chunk WHERE stream_id=? ORDER BY seq LIMIT 1000
    DB-->>DS: [{seq, direction, data, timestamp}]
    DS-->>Browser: 200 JSON array
```

## 5. Route Table

| Method | Path | Handler | Description |
|--------|------|---------|-------------|
| GET | `/api/status` | `xBuildStatusJson()` | All b1s + agents |
| GET | `/api/stats` | `xBuildStatsJson()` | Aggregate counts |
| GET | `/api/events` | SSE stream | Live event push; calls `m_sse->addClient()` with uWS write callback |
| GET | `/api/events/pending` | `xBuildPendingJson()` | Unresolved user_prompts from EventStore |
| GET | `/api/b1/:pid` | Registry lookup | Single b1 details |
| GET | `/api/b1/:pid/agents` | Registry lookup | Agents under b1 |
| GET | `/api/agent/:uuid` | Registry scan | Agent session info |
| GET | `/api/agent/:uuid/messages` | SQLite read | Load messages for an agent session (supports `?limit=N&before=M` cursor pagination) |
| POST | `/api/agent/:uuid/messages` | Append message; resolves prompt if tool role | User input or tool response; sends prompt_reply via C2Listener |
| DELETE | `/api/agent/:uuid/prompt/:toolCallId` | Dismiss prompt | Cancel without answer; broadcasts `prompt_dismissed` SSE |
| POST | `/api/ping` | Sends pong via SSE | Client keepalive |
| POST | `/api/terminal/open` | Terminal launch | Opens PTY terminal via b1 or direct a0 fork |
| GET | `/api/terminal/status/:terminalId` | Terminal status | Poll SQLite for stream readiness |
| POST | `/api/stream/:id/input` | Terminal stdin | Forward STREAM_INPUT via b1 IPC |
| GET | `/api/stream/:id/chunks` | Read chunks | Stream output from SQLite ordered by seq (limit 1000) |
| GET | `/api/session/:uuid/streams` | List streams | All streams for a session |
| GET | `/*` | `xServeStatic()` | Static files, SPA fallthrough |

## 6. Static File Serving

Files are served from the path specified by `--web-root` (default: `<cwd>/.a0/git/opensassi/a0/c2/web`). MIME types are determined by file extension. For paths without a recognized extension or when the file doesn't exist, `index.html` is served (SPA fallthrough).

All static responses include `Cache-Control: no-cache, no-store, must-revalidate` to prevent stale ES module caching during development.

MIME mapping: `.html` → `text/html`, `.js` → `text/javascript`, `.css` → `text/css`, `.svg` → `image/svg+xml`, `.png` → `image/png`, `.ico` → `image/x-icon`.

## 7. Error Handling

| Scenario | Behaviour |
|----------|-----------|
| Port already in use | `run` returns -1 |
| File not found in web_root | Serves `index.html` (SPA fallthrough) |
| Registry empty | `/api/status` returns `[]`, `/api/stats` returns all zeros |
| uWS internal error | uWS handles internally; server continues |
| SSE client disconnected | `onAborted` removes client from `SseManager` |
| `sendToB1` to disconnected b1 | Returns -1, logged silently |
| Invalid JSON in POST body | Returns 400 `{"error":"invalid json"}` |
| Missing `role` in POST messages | Returns 400 `{"error":"missing role"}` |
| terminal/open cwd not found | Returns 400 `{"error":"directory not found"}` |
| Agent not found | Returns 404 `{"error":"agent not found"}` |
| B1 not found | Returns 404 `{"error":"b1 not found"}` |
| Database open failure | `/api/agent/:uuid/messages` returns `[]` |

## 8. Testing Requirements

| Method | Test Case | Input | Expected |
|--------|-----------|-------|----------|
| `xBuildStatusJson` | Two b1s registered | — | JSON array with 2 entries |
| `xBuildStatusJson` | Empty registry | — | `[]` |
| `xBuildStatsJson` | Mixed | 2 b1s, 1 crashed | `{"totalB1s":2,"totalAgents":3,"crashedCount":1}` |
| `xBuildPendingJson` | One pending prompt | — | JSON array with 1 entry containing session/toolCallId/prompt |
| `xBuildPendingJson` | No pending | — | `[]` |
| `xServeStatic` | Existing file | `/js/app.js` | 200, correct MIME type |
| `xServeStatic` | Non-existent path | `/nope` | 200, serves index.html (SPA) |
| `xServeStatic` | web_root missing | No web_root dir | 404 "Not Found" |
| `xMimeType` | .js file | — | `text/javascript` |
| `xMimeType` | .unknown file | — | `application/octet-stream` |
| `xReadFile` | Existing file | — | File contents as string |
| `xReadFile` | Non-existent file | — | Empty string |
| `run` | SSL enabled | sslKey + sslCert set | Creates uWS::SSLApp, listens on port |
| `run` | No SSL | Default | Creates uWS::App, listens on port |
| `shutdown` | While running | Called from signal handler | `m_running=false`, `us_listen_socket_close` called |

## 9. Terminal Launch Flow

When `POST /api/terminal/open` is received with `{cwd, contextType}`:

1. **B1 lookup**: Iterate registered b1s by workdir. If a b1 matches:
   - Send `TERMINAL_OPEN` IPC message to b1
   - b1 forks `a0 terminal` with `--cwd`, `--terminal-id`, (derived `--log-file`)
2. **Direct fork** (no matching b1):
   - Record `m_directTerminals[terminalId] = cwd`
   - Fork/exec `a0 --a0-dir <cwd>/.a0 --log-file <derivedPath> terminal --terminal-id <id> --cwd <cwd>`
   - a0 auto-launches b1 if needed, which registers with c2

### Log File Derivation

```cpp
// dashboard_server.cpp
std::string a0Log;
if (!g_c2LogFile.empty()) {
    auto dot = g_c2LogFile.rfind('.');
    a0Log = (dot != std::string::npos)
        ? g_c2LogFile.substr(0, dot) + "-a0" + g_c2LogFile.substr(dot)
        : g_c2LogFile + "-a0";
}
```

When `--log-file` is set on c2, forked a0 terminal processes receive `--log-file /tmp/c2-e2e-a0.log`. The a0 terminal further propagates to b1.

## 10. Integration

`DashboardServer` is constructed in `c2_main.cpp` with pointers to `B1Registry`, `SseManager`, `EventStore`, and `C2Listener`. It runs on the main thread. On SIGINT/SIGTERM, `c2_main.cpp` calls `shutdown()` which sets `m_running = false`.
