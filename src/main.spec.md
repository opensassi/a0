# Main Spec

## 1. Overview

Entry-point module (`src/main.cpp`). Parses CLI flags via CLI11, loads `.env` files, resolves the DeepSeek API key through a priority chain instantiates all concrete components (SkillManager, SqliteStore, SubprocessToolRunner, DeepSeekProvider, Docker managers, PersonaLoader), and dispatches to either headless (`run` subcommand), TUI (`tui` subcommand, default), terminal (`terminal` subcommand), or session management (`session` subcommand).

Both `run` and `tui` use `AppCoreThread` wrapping `DrivenCore` on a background thread with MPSC communication. The `cmdRun` path polls MPSC events synchronously; `cmdTui` forwards MPSC events to the FTXUI render loop via `AgentTui`.

**Dependencies:** All sub-modules (shared, bootstrap, persistence, skills, llm, core, ipc, docker, executor, tui, b1)

## 2. Component Specifications

No classes defined — the file contains static helper functions and a `struct AgentStack`:

```cpp
struct AgentStack {
    a0::persistence::SqliteStore persistence;
    a0::persistence::SqliteResourceProvider resourceProvider;
    a0::skills::SkillManager skillMgr;
    SubprocessToolRunner toolRunner;
    a0::DeepSeekProvider llmProvider;

    a0::docker::DockerContainerManager* containerMgr = nullptr;
    a0::docker::DockerComposeManager* composeMgr = nullptr;
    a0::docker::DockerToolRunnerImpl* dockerRunner = nullptr;
    a0::DockerSecurityFilter dockerFilter;

    AgentStack(const std::string& a0Dir, const std::string& skillsDir,
               const std::string& apiKey, const std::string& mockUrl,
               bool noDocker, bool noContainerPool,
               const std::string& idleTimeoutStr, const std::string& maxIdleStr,
               const std::string& defaultImage, int maxParallel = 4,
               const std::string& externalRepo = "https://github.com/opensassi/a0");
    ~AgentStack();
};
```

**Static functions:**
- `loadEnvFile(path)` — sources .env file, no-op if missing
- `killByPidFile(path)` — SIGTERM then SIGKILL
- `killByProcessName(name)` — pgrep + SIGTERM
- `xSelfDir()` — readlink /proc/self/exe
- `xChildLog(parentLog, suffix)` — derive child log path from parent
- `xMakeLogPath(sessionId, pid, suffix)` — construct per-session log path
- `xRedirectStderr(sessionId, pid)` — redirect stderr to log file
- `xRegisterSystemHandlers(mgr)` — register all C++ handlers
- `xRegisterAgent(store)` — register agent fingerprint
- `cmdKillAll(a0Dir)` — stop daemon processes
- `cmdSessionExport(a0Dir, sessionId, outputPath, outputJson)` — export session as JSONL
- `cmdSessionList(a0Dir, offset, limit, outputJson)` — list sessions
- `cmdRun(a0Dir, skillsDir, ...)` — headless execution
- `cmdTui(a0Dir, skillsDir, ...)` — interactive TUI
- `cmdTerminal(a0Dir, terminalId, cwd)` — PTY terminal session

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph Entry
        M[main]
        KA[cmdKillAll]
        SE[cmdSessionExport / cmdSessionList]
        TRM[cmdTerminal]
    end

    subgraph Run_Path
        RUN[cmdRun]
        CMD_CH[mpsc Channel<Command>]
        EVT_CH[mpsc Channel<AppCoreEvent>]
        ACT[AppCoreThread]
    end

    subgraph TUI_Path
        TUI[cmdTui]
        TUI_APP[AgentTui]
    end

    subgraph Shared_Components
        STACK[AgentStack]
        SM[SkillManager]
        PS[SqliteStore]
        RP[SqliteResourceProvider]
        TR[SubprocessToolRunner]
        LM[DeepSeekProvider]
    end

    subgraph Docker_Components
        DCM[DockerContainerManager]
        DCoM[DockerComposeManager]
        DTR[DockerToolRunnerImpl]
    end

    subgraph Persistence
        DB[(.a0/db/sessions.db)]
    end

    M -->|kill-all| KA
    M -->|session| SE
    M -->|terminal| TRM
    M -->|run| RUN
    M -->|tui or default| TUI
    RUN --> STACK
    TUI --> STACK
    STACK --> SM
    STACK --> PS
    STACK --> RP
    STACK --> TR
    STACK --> LM
    STACK -->|if !noDocker| DCM
    STACK -->|if !noDocker| DCoM
    STACK -->|if !noDocker| DTR
    SM --> TR
    SM --> DTR
    PS --> DB
    RUN --> CMD_CH
    RUN --> EVT_CH
    CMD_CH --> ACT
    EVT_CH --> RUN
    TUI_APP --> CMD_CH
    EVT_CH --> TUI_APP
    ACT --> LM
```

## 4. Data Flow

```mermaid
sequenceDiagram
    participant M as main
    participant ST as AgentStack
    participant RUN as cmdRun
    participant TUI as cmdTui
    participant CMD as Command Channel
    participant EVT as Event Channel
    participant ACT as AppCoreThread
    participant B1 as b1 daemon

    M->>M: parse CLI flags
    M->>ST: AgentStack constructor
    ST->>ST: create SkillManager, SqliteStore, runners, providers
    ST->>ST: xRegisterSystemHandlers
    ST->>ST: setToolRunner, setResourceProvider, setDockerRunner

    alt run subcommand
        M->>RUN: cmdRun
        RUN->>RUN: loadAll, registerAgent, createSession
        RUN->>CMD: create channel pair
        RUN->>ACT: start(recv, send)
        RUN->>CMD: send SetSession + SubmitGoal
        loop poll
            RUN->>EVT: drain
            EVT-->>RUN: Complete or Error
        end
        RUN->>CMD: send Shutdown
        RUN->>ACT: stop
        RUN-->>M: result JSON
    else tui subcommand
        M->>TUI: cmdTui
        TUI->>TUI: loadAll, registerAgent, createSession
        TUI->>B1: fork b1, connect, register
        TUI->>CMD: create channel pair
        TUI->>ACT: start(recv, send)
        TUI->>CMD: send SetSession
        TUI->>TUI: create AgentTui(cmdSender, evtReceiver)
        TUI->>ACT: setWakeupFn
        TUI->>TUI_APP: run()
        TUI_APP->>EVT: drain in FTXUI loop
        TUI_APP->>CMD: send SubmitGoal etc.
        TUI_APP-->>TUI: return code
        TUI->>CMD: send Shutdown
        TUI->>ACT: stop
    end
```

## 5. Testing Requirements

| Test | Verification |
|------|-------------|
| CLI11 parse --help | Prints help, exits 0 |
| CLI11 parse unknown flag | Prints error, exits non-zero |
| run with prompt | AppCoreThread started, SubmitGoal sent, Complete received |
| tui default | AgentTui constructed with cmdSender + evtReceiver |
| session export | Exports JSONL to stdout or file |
| session list | Lists recent sessions |
| kill-all | Sends SIGTERM to b1/c2 processes |
| terminal subcommand | Creates PTY, connects to b1, sends TERMINAL_READY |
