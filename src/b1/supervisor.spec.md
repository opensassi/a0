# Supervisor Spec

## 1. Overview

Central class for the b1 supervisor lifecycle. Manages the accept loop over `.a0/b1.sock`, tracks connected a0 instances via PID and socket disconnect, monitors for crashes via `waitpid(WNOHANG)`, pushes periodic snapshots to c2, and forwards `user_prompt` IPC messages upstream and `prompt_reply` IPC messages downstream.

**Dependencies:** `UnixSocket`, `Message` (from `ipc`), `CommandRunner`, POSIX (`poll`, `waitpid`, `kill`, `unlink`)

**Lifecycle:** Short-lived setup (`init`), long-running event loop (`run`), then shutdown.

## 2. Component Specifications

```cpp
namespace a0::b1 {

enum class AgentState { RUNNING, CRASHED, STOPPED };

struct AgentRecord {
    int pid = 0;
    int fd = -1;
    std::string sessionUuid;
    AgentState state = AgentState::RUNNING;
    std::chrono::steady_clock::time_point connectedAt;
    std::chrono::steady_clock::time_point lastHeartbeat;
};

class Supervisor {
public:
    Supervisor(const std::string& socketPath,
               const std::string& pidPath,
               const std::string& c2SocketPath,
               const std::string& workdir);
    ~Supervisor();

    int init();
    int run();
    void shutdown();
    size_t agentCount() const;

private:
    std::string m_socketPath, m_pidPath, m_c2SocketPath, m_workdir;
    ipc::UnixSocket m_listenSocket;
    bool m_running = false;
    std::unordered_map<int, AgentRecord> m_agents;
    int m_c2Fd = -1;
    std::chrono::steady_clock::time_point m_lastC2Push;
    int m_listenFd = -1;

    int xHandleRegister(const ipc::Message& msg, int peerFd);
    int xHandleHeartbeat(const ipc::Message& msg, int peerPid);
    int xHandleUserPrompt(const ipc::Message& msg, int peerFd);
    int xHandlePromptReply(const ipc::Message& msg);
    int xDetectCrashes();
    int xPushSnapshotToC2();
    int xLaunchC2IfNeeded();
    int xSendToC2(const ipc::Message& msg);
    int xSendToAgent(int agentFd, const ipc::Message& msg);
    int xFindAgentFdBySession(const std::string& sessionUuid) const;
    void xCleanupStaleSocket();
    int xWritePidFile();
};

} // namespace a0::b1
```

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph Supervisor
        INIT[init]
        LOOP[run]
        REG[xHandleRegister]
        HB[xHandleHeartbeat]
        UP[xHandleUserPrompt]
        REPLY[xHandlePromptReply]
        CRASH[xDetectCrashes]
        C2[xPushSnapshotToC2]
        LAUNCH[xLaunchC2IfNeeded]
    end

    A0[a0 instances] -->|connect + register| REG
    A0 -->|user_prompt| UP
    A0 -->|process exit| CRASH
    C2DAEMON[c2 daemon] -->|prompt_reply| REPLY
    C2 -->|push snapshot| C2DAEMON
    LAUNCH -->|fork + setsid| C2DAEMON
    UP -->|forward to| C2DAEMON
    LOOP --> REG
    LOOP --> HB
    LOOP --> UP
    LOOP --> CRASH
    LOOP --> C2
    LOOP --> REPLY
```

## 4. Data Flow

```mermaid
sequenceDiagram
    participant A0 as a0
    participant Sup as Supervisor
    participant C2 as c2

    A0->>Sup: {"type":"register","pid":1234,"session":"uuid"}
    Sup->>Sup: xHandleRegister → track AgentRecord

    A0->>Sup: {"type":"user_prompt","session":"ses_x","toolCallId":"c1","prompt":"?"}
    Sup->>Sup: xHandleUserPrompt
    Sup->>C2: forward user_prompt

    C2->>Sup: {"type":"prompt_reply","session":"ses_x","toolCallId":"c1"}
    Sup->>Sup: xHandlePromptReply → find agent by session
    Sup->>A0: forward prompt_reply

    loop Event loop iteration
        Sup->>Sup: xDetectCrashes() → waitpid(WNOHANG)
    end

    Sup->>C2: {"type":"update","agents":[...]}
```

## 5. Error Handling

| Scenario | Behaviour |
|----------|-----------|
| Socket path already in use | `xCleanupStaleSocket` unlinks before bind |
| PID file write fails | `init` returns -2 |
| poll() returns error | `run` continues (logs error) |
| c2 socket unreachable | `xLaunchC2IfNeeded` fork/execs new c2 via setsid() |
| user_prompt from unknown agent | Forwarded to c2 with pid=0 |
| prompt_reply for unknown session | Logged to stderr, dropped |
| c2 connection lost mid-operation | Next send returns -1 → fd closed, c2 auto-relaunch |

## 6. Testing Requirements

| Method | Test Case | Input | Expected |
|--------|-----------|-------|----------|
| `init` | Writes PID file | Valid path | File exists, matches getpid() |
| `init` | Binds socket | Valid path | Socket file exists |
| `xHandleRegister` | Valid register JSON | `{"type":"register","pid":99}` | AgentRecord created, m_agents size=1 |
| `xHandleUserPrompt` | Valid prompt | `{"type":"user_prompt","session":"s","toolCallId":"c"}` | Forwarded to c2 |
| `xHandlePromptReply` | Known session | `{"type":"prompt_reply","session":"s"}` | Forwarded to agent fd |
| `xHandlePromptReply` | Unknown session | `{"type":"prompt_reply","session":"?"}` | Returns -1 |
| `xFindAgentFdBySession` | Known session | UUID of registered agent | Returns agent fd |
| `xFindAgentFdBySession` | Unknown session | Random UUID | Returns -1 |
| `xDetectCrashes` | No children | — | Returns 0 |
| `agentCount` | One registered | 1 register | Returns 1 |
