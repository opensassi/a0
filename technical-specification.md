# Technical Paper: Minimal Component‑Based Agent (C++ Implementation)

## Version 2.1 – Docker Integration and Containerized Execution

---

## 1. Overview

This document provides the complete technical specification and development plan for a **minimal self‑evolving agent** written in **C++17**. The agent connects to the DeepSeek API, maintains a file‑based repository of **tools** (atomic bash commands) and **skills** (LLM prompts with eager tool calls, parameter substitution, optional validators), and runs inside a VM isolation environment (or directly on a host). All logs are stored locally.

**Core features**:

- Tools and skills share JSON Schema I/O (strings, arrays, objects).
- Skills support **eager tool calls** (`{{tool:name ...}}`) that execute before the LLM request.
- Skills support **parameter substitution** (`{{key}}`) where `key` is a field from the invocation parameters (e.g., `{{goal}}`).
- **Validators** (optional chain of tools) process the LLM response; validators are simple tool names with optional transformation bindings.
- **Dependencies** declared per skill (tools and other skills required) – **must be checked before execution**.
- **Timeout enforcement** for all tool executions (30 seconds default).
- **Docker execution mode**: Tools can run inside isolated containers (default Ubuntu 22.04) with configurable trust levels (HIGH/MEDIUM/LOW), shared container pools, custom images, and `apt` dependencies. Skills can bring up multi‑service environments using `docker-compose.yml`.
- Performance monitoring (optional).
- Session replay via local JSON Lines logs.

This specification includes **architectural and procedural guardrails** that would have prevented common implementation omissions, such as missing dependency checks, ignored parameter substitution, absent subprocess timeouts, and incomplete `args` mode handling. The Docker sub‑module is a **required** extension for containerized execution.

---

## 2. Component Specifications (C++ Interfaces)

The following interfaces are defined in `agent_interfaces.h`. They use modern C++17/20 features (`std::variant`, `std::optional`, smart pointers). No inheritance except abstract base classes.

**Key types**:

```cpp
using StructuredValue = std::variant<nullptr_t, bool, double, std::string,
                                     std::vector<StructuredValue>,
                                     std::unordered_map<std::string, StructuredValue>>;
using JSONSchema = std::unordered_map<std::string, StructuredValue>;
```

### 2.0 Abstract Async LLM Provider

```cpp
namespace a0 {

/// Abstract async LLM provider interface.
/// Non-blocking startRequest() / tick() API designed for event-loop integration.
class LlmProvider {
public:
    virtual ~LlmProvider() = default;
    virtual void startRequest(const std::string& systemPrompt,
                              const std::vector<Message>& messages,
                              const std::vector<ToolSchema>& tools) = 0;
    virtual void startRequestStreaming(const std::string& systemPrompt,
                                       const std::vector<Message>& messages,
                                       const std::vector<ToolSchema>& tools) = 0;
    virtual std::vector<mpsc::AppCoreEvent> tick() = 0;
    virtual void cancel() = 0;
    virtual bool active() const = 0;
    virtual int timeoutMs() const = 0;
    virtual void setMockUrl(const std::string& url) = 0;
};

} // namespace a0
```

**Implementation hierarchy:**

```
LlmProvider  (pure virtual, DrivedCore depends on this)
    ↑
DrivenProvider  (universal curl_multi machinery, protected virtual hooks)
    ↑                        ↑
DeepSeekProvider
```

### 2.1 Core Data Structures

```cpp
enum class TrustLevel {
    HIGH,      // shared container across all high-trust tools
    MEDIUM,    // shared container across all medium-trust tools
    LOW        // one container per (image, tool name) combination
};

struct Tool {
    std::string name;
    std::string description;
    std::string command;
    // "stdin": params JSON is serialized and piped to the command's stdin.
    // "args":  params is a JSON object; each top-level key-value pair is passed as a command-line argument
    //          in the form --key=value (if value is string/number) or just value (if key is "_").
    //          For a non-object params, the whole value is passed as a single argument.
    //          Arguments are shell-escaped.
    std::string inputMode = "stdin";

    // Docker execution (optional – if dockerImage empty, run on host)
    std::string dockerImage;                     // e.g., "ubuntu:22.04"
    TrustLevel trustLevel = TrustLevel::MEDIUM;
    bool useContainerPool = true;                // false = ephemeral docker run --rm
    std::vector<std::string> aptDependencies;    // packages to install via apt
    int timeoutSecs = 30;                        // per-tool timeout override
};

struct ToolCall {
    std::string id;
    std::string name;
    json arguments;
};

struct ToolSchema {
    std::string name;
    std::string description;
    json inputSchema;
};

struct ValidatorBinding {
    std::string toolName;
    std::optional<std::string> transform; // JSONPath binding
};

struct Prompt {
    std::string name;
    std::string description;
    std::string prompt;                   // may contain {{tool:...}} and {{key}} placeholders
    std::vector<std::string> dependencies;    // tool and prompt names required
    std::vector<ValidatorBinding> validators; // post-LLM processing chain
    std::string ns;                            // namespace context for qualified-name resolution
    std::string component;                     // component context for qualified-name resolution

    // Docker compose support
    std::string composeFile;                     // path to docker-compose.yml (relative to skill dir)
    std::vector<std::string> aptDependencies;    // rolled up into container(s) used by prompt's tools
};
```

### 2.2 Abstract Interfaces

```cpp
class SkillRegistry {
public:
    virtual ~SkillRegistry() = default;
    virtual bool loadFromDirectory(const std::string& path) = 0;
    virtual std::optional<Tool> getTool(const std::string& name) const = 0;
    virtual std::optional<Skill> getSkill(const std::string& name) const = 0;
    virtual std::vector<std::string> listTools() const = 0;
    virtual std::vector<std::string> listSkills() const = 0;
    virtual bool addTool(const Tool& tool) = 0;
    virtual bool addSkill(const Skill& skill) = 0;
};

class ToolRunner {
public:
    virtual ~ToolRunner() = default;
    virtual json run(const Tool& tool, const json& params) = 0;
    virtual a0::StreamHandle runStreaming(const Tool& tool,
                                           const json& params,
                                           a0::StreamCallback onChunk);
};

/// @deprecated No longer compiled. Kept as reference.
/// InferenceProvider was deleted — the async LlmProvider interface
/// replaced it. This block documents the old contract.

class ContextManager {
public:
    virtual ~ContextManager() = default;
    virtual void push(const ContextFrame& frame) = 0;
    virtual ContextFrame pop() = 0;
    virtual ContextFrame peek() const = 0;
    virtual size_t size() const = 0;
    virtual void clear() = 0;
    virtual std::vector<ContextFrame> snapshot() const = 0;
};

class DependencyResolver {
public:
    virtual ~DependencyResolver() = default;
    virtual bool checkToolDependencies(const Tool& tool) const = 0;
    virtual bool checkPromptDependencies(const Prompt& prompt) const = 0;
    virtual std::vector<std::string> missingDependencies(const Prompt& prompt) const = 0;
};

/// @deprecated No longer compiled. Kept as reference.
class SkillRunner {
public:
    virtual ~SkillRunner() = default;
    virtual std::string expandPrompt(const Prompt& prompt, const json& params) = 0;
    virtual json runValidators(const Prompt& prompt, const json& input) = 0;
    virtual json execute(const Prompt& prompt, const json& params) = 0;
    virtual a0::StreamHandle executeStreaming(const Prompt& prompt,
                                               const json& params,
                                               a0::StreamCallback onChunk) = 0;
    virtual void setGlobalVar(const std::string& key, const std::string& value) = 0;
    virtual void setGlobalVars(const std::unordered_map<std::string, std::string>& vars) = 0;
};

/// @deprecated No longer compiled. Kept as reference.
class AgentCore {
public:
    virtual ~AgentCore() = default;
    virtual bool init(const std::string& componentsDir) = 0;
    virtual json processGoal(const std::string& goal) = 0;
    virtual a0::StreamHandle processGoalStreaming(const std::string& goal,
                                                   a0::StreamCallback onChunk) = 0;
    virtual bool resumeSession(const std::string& sessionId) = 0;
    virtual std::string currentSessionId() const = 0;
    virtual void run() = 0;
};

// === Docker Integration Interfaces ===

class ContainerManager {
public:
    virtual ~ContainerManager() = default;
    virtual std::string acquireContainer(const Tool& tool) = 0;
    virtual std::string execInContainer(const std::string& containerId,
                                        const std::string& command,
                                        const std::string& stdinData = "") = 0;
    virtual void pruneIdleContainers() = 0;
};

class ComposeManager {
public:
    virtual ~ComposeManager() = default;
    virtual std::string startEnvironment(const Prompt& prompt, const std::string& skillDirectory) = 0;
    virtual void stopEnvironment(const Prompt& prompt) = 0;
};

class DockerToolRunner : public ToolRunner {
public:
    virtual ~DockerToolRunner() = default;
};

class ComposeManager {
public:
    virtual ~ComposeManager() = default;
    virtual std::string startEnvironment(const Prompt& prompt, const std::string& skillDirectory) = 0;
    virtual void stopEnvironment(const Prompt& prompt) = 0;
    virtual void markUsed(const Prompt& prompt) = 0;
    virtual void setCurrentPrompt(const Prompt& prompt) = 0;
    virtual std::string getCurrentNetwork() const = 0;
    virtual void clearCurrentPrompt() = 0;
};

class DockerToolRunner : public ToolRunner {
public:
    virtual ~DockerToolRunner() = default;
};

// === Async / Event-Driven Interfaces ===

// mpsc::Channel — header-only MPSC channel with eventfd wakeup (src/mpsc.h)

// ResponseDecoder — stateful SSE/JSON decoder (src/response_decoder.h/.cpp)
//   feed(bytes) → decodes tokens/tool_calls, events() returns vector<AppCoreEvent>

// DrivenProvider — async LLM provider, implements LlmProvider (src/driven_provider.h/.cpp)
//   curl_multi base machinery. Subclasses override xBuildPayload() and xAddAuth().
//   DeepSeekProvider : DrivenProvider (src/deepseek_provider.h/.cpp) — DeepSeek-specific config.
//   xBuildPayload produces OpenAI-compatible JSON.
//   xAddAuth adds "Authorization: Bearer" header.

// DrivenCore — tick-driven state machine core (src/driven_core.h/.cpp)
//   States: Idle → AwaitingLlm → ExecutingTools → Idle
//   submitGoal(goal) → tick() drives provider + tool execution
//   runSync(goal) → synchronous wrapper for CLI headless mode
//   Uses LlmProvider* — works with any provider

// AppCoreThread — thread-safe wrapper (src/app_core_thread.h/.cpp)
//   Owns DrivenCore in dedicated thread with ppoll()-based event loop
```

---

## 3. System Architecture

The architecture includes both host and Docker execution paths, with mandatory dependency check and container pooling. The `SystemToolRegistry` has been eliminated — all tool dispatch (both C++ system handlers and command-based subprocess tools) goes through `SkillManager`'s handler registry. A `DependencyGraph` batch executor parallelizes reader-class tools, and a `ToolState` bag carries per-session state across invocations.

```mermaid
graph TB
    User((User))
    DeepSeek[("DeepSeek API")]

    subgraph "Agent Process (Legacy Core Path)"
        Core[AgentCore]
        Context[ContextManager]
        SkillRunner[SkillRunner]
        DepResolver[DependencyResolver]
        DepGraph[DependencyGraph]
    end

    subgraph "Agent Process (DrivenCore Path)"
        DC[DrivenCore]
        DP[DrivenProvider]
        ACT[AppCoreThread]
        CHAN[mpsc CHANNELS]
        DEC[ResponseDecoder]
    end

    subgraph "Shared Services"
        SkillMgr[SkillManager]
        SkillMgr_Handlers[m_handlers: xBash, xRead, xGitCommand, ...]
        ToolState[m_toolState: ToolState]
        HostRunner[HostToolRunner]
        DockerRunner[DockerToolRunner]
        Persistence[PersistenceStore]
        ContainerMgr[ContainerManager]
        ComposeMgr[ComposeManager]
    end

    subgraph "Persistence"
        DB[(.a0/db/sessions.db)]
    end

    subgraph "Docker Engine"
        Containers[(Containers)]
        ComposeStacks[(Compose Stacks)]
    end

    subgraph "Filesystem"
        SkillsDir[(skills/)]
        SchemaJSON[skills/schema.json]
    end

    User --> Core
    User --> CHAN
    CHAN --> DC
    DC --> DP
    DP --> DEC
    DP --> DeepSeek
    DC --> SkillMgr
    ACT --> DC
    ACT --> DP
    ACT --> CHAN

    Core --> SkillMgr
    Core --> DepResolver
    Core --> HostRunner
    Core --> DockerRunner
    Core --> SkillRunner
    Core --> DepGraph
    Core --> Persistence

    SkillMgr --> SkillMgr_Handlers
    SkillMgr --> HostRunner
    SkillMgr --> DockerRunner
    SkillMgr --> ToolState

    SkillRunner --> DepResolver
    SkillRunner --> DeepSeek
    SkillRunner --> SkillMgr
    SkillRunner --> HostRunner
    SkillRunner --> DockerRunner

    Persistence --> DB
    SkillMgr --> SkillsDir
    SkillMgr --> SchemaJSON

    DockerRunner --> ContainerMgr
    DockerRunner --> ComposeMgr
    ContainerMgr --> Containers
    ComposeMgr --> ComposeStacks
    DepGraph --> SkillMgr
    DepGraph --> HostRunner
```

**Caption**: Two parallel agent core paths exist. The **legacy path** (`AgentCore` → `SkillRunner` → `DeepSeekProvider`) is used by `cmdRun()` and the `run` skill subcommand. The **DrivenCore path** (`DrivenCore` + `DrivenProvider` + `ResponseDecoder`) is a tick-based state machine used by the TUI sub-module. Both paths share `SkillManager`, `PersistenceStore`, and the tool runners. `DrivenCore` replaces both `xRunForkedLoop()` and `processGoalStreaming()` with a single `submitGoal/tick` interface. `AppCoreThread` wraps `DrivenCore` in a dedicated thread with `ppoll()` event loop for thread-safe CLI usage.

---

## 4. Detailed Data Flow

The following sequence diagrams show the execution paths for skills and Docker‑based tools.

### 4.0 DependencyGraph Batched Tool Execution

```mermaid
sequenceDiagram
    participant LLM
    participant Core as AgentCore (xRunForkedLoop)
    participant DG as DependencyGraph
    participant SM as SkillManager
    participant CR as CommandRunner

    LLM->>Core: tool_calls: [read1, read2, write1, read3]
    Core->>Core: collect ToolInvocation vector
    Core->>DG: buildBatches(invocations)
    DG->>DG: classifyTool(read1)=READER, read2=READER, write1=WRITER, read3=READER
    DG-->>Core: batches: [[read1,read2,read3], [write1]]

    Core->>DG: executeBatches(batches, skillMgr, maxParallel=4)

    rect rgb(230, 255, 230)
        Note over DG,CR: Batch 0: 3 READERs (parallel when all subprocess)
        alt all are command tools
            DG->>CR: runAll([cmd1, cmd2, cmd3], 30s, 4)
            CR-->>DG: [out1, out2, out3]
        else mixed system + command
            loop each reader
                DG->>SM: executeToolWithMeta(qn, params)
                SM-->>DG: HandlerResult
            end
        end
    end

    rect rgb(255, 240, 230)
        Note over DG,SM: Batch 1: WRITER (serial)
        DG->>SM: executeToolWithMeta(write1, params)
        SM-->>DG: HandlerResult{output}
    end

    DG-->>Core: [BatchResult{outputs:[out1,out2,out3]}, BatchResult{outputs:[out4]}]
    Core->>Core: flatten into message order
```

### 4.1 Skill Execution with Dependency Check and Eager Tool Calls

```mermaid
sequenceDiagram
    participant User
    participant Core
    participant SkillRunner
    participant DepResolver
    participant ToolRunner
    participant list_files
    participant DeepSeek
    participant extract_json

    User->>Core: "find files in /home related to proposals"
    Core->>SkillRunner: run(skill, {goal:"find files in /home related to proposals"})
    SkillRunner->>DepResolver: checkSkillDependencies(skill)
    alt missing dependencies
        DepResolver-->>SkillRunner: ["missing_tool"]
        SkillRunner-->>Core: error("missing dependencies: missing_tool")
        Core-->>User: error
    else dependencies satisfied
        DepResolver-->>SkillRunner: ok
        SkillRunner->>SkillRunner: expandPrompt: replace {{goal}} with user goal
        SkillRunner->>SkillRunner: parse {{tool:list_files path="/home"}}
        SkillRunner->>ToolRunner: run(list_files, {path:"/home"})
        ToolRunner-->>SkillRunner: ["f1","f2","f3"]
        SkillRunner->>SkillRunner: substitute tool result into prompt
        SkillRunner->>DeepSeek: complete(expanded prompt)
        DeepSeek-->>SkillRunner: "[\"f1\",\"f3\"]"
        loop each validator (extract_json)
            SkillRunner->>ToolRunner: run(validator, currentValue)
            alt validator returns error
                ToolRunner-->>SkillRunner: "ERROR: invalid JSON"
                SkillRunner->>SkillRunner: prefix "VALIDATOR_ERROR:"
            else
                ToolRunner-->>SkillRunner: transformedValue
            end
        end
        SkillRunner-->>Core: ["f1"]
        Core-->>User: result
    end
```

### 4.1a Streaming Tool Execution

```mermaid
sequenceDiagram
    participant Client as AgentCore/SkillRunner
    participant SM as SkillManager
    participant TR as ToolRunner
    participant CR as CommandRunner

    Client->>SM: executeToolStreaming(qn, params, onChunk)

    alt system tool handler
        SM->>SM: exact match in m_handlers
        SM->>SM: run handler synchronously
        SM->>SM: call onChunk(output, "stdout")
        SM-->>Client: completed StreamHandle
    else command tool with streaming=true
        SM->>SM: resolve to SkillTool{streaming:true}
        SM->>TR: runStreaming(tool, params, onChunk)
        TR->>CR: runStreaming(cmd, onChunk, timeout)
        CR-->>TR: StreamHandle
        TR-->>SM: StreamHandle
        SM-->>Client: StreamHandle (live)
    else command tool without streaming
        SM->>SM: fall through to executeToolWithMeta
        SM-->>Client: synchronous result via onChunk
    end
```

### 4.1b ToolState Lifecycle

```
processGoal() start → ToolState::clear()           ← reset all per-session state
     ↓
xRunForkedLoop turn 1 → handler set("browser:page", handle)
xRunForkedLoop turn 2 → handler get("browser:page") → reuse handle
     ↓
processGoal() end    → next processGoal() clears again
```

### 4.2 DrivenCore Tick-Based Execution

```mermaid
sequenceDiagram
    participant UI as CLI / TUI
    participant DC as DrivenCore
    participant DP as DrivenProvider
    participant DEC as ResponseDecoder
    participant SM as SkillManager

    UI->>DC: submitGoal("find log files")
    DC->>DC: xBuildInitialMessages(goal)
    DC->>DC: xBuildToolSchemas()
    DC->>DP: startRequestStreaming(sys, msgs, tools)
    DC->>DC: state = AwaitingLlm

    loop tick cycle
        UI->>DC: tick()
        DC->>DP: tick()
        DP->>DEC: feed(curl response bytes)
        DEC-->>DP: [LlmToken, ToolStart, ToolEnd, Complete]
        DP-->>DC: AppCoreEvent vector
        DC->>DC: xHandleLlmEvents(events)
        DC-->>UI: forwarded AppCoreEvent vector
    end

    rect rgb(255, 240, 230)
        Note over SM,DC: Tool call phase
        DC->>DC: xExecuteTools()
        DC->>SM: executeTool("glob", params)
        SM-->>DC: result
        DC->>DC: xStartLlmRequest(includeTools=true)
        DC->>DP: startRequestStreaming(...)
    end

    rect rgb(230, 255, 230)
        Note over DP,DC: LLM returns text
        DP-->>DC: Complete("found 3 files")
        DC->>DC: xFinishGoal(text)
        DC->>DC: state = Idle
    end
```

### 4.2b AppCoreThread DrivenCore Wrapper

```mermaid
sequenceDiagram
    participant UI as CLI / TUI
    participant ACT as AppCoreThread
    participant DC as DrivenCore
    participant DP as DrivenProvider

    UI->>ACT: start(cmdRcvr, evtSender)
    ACT->>ACT: spawn thread → xRun()

    UI->>ACT: cmdSender.send(SubmitGoal{"hello"})
    ACT->>ACT: ppoll returns → drain commands
    ACT->>DC: submitGoal("hello")

    loop ppoll/tick
        ACT->>ACT: ppoll(cmdFd, wakeupFd, providerTimeout)
        ACT->>ACT: drain commands (if any)
        ACT->>DC: tick()
        DC->>DP: tick()
        DP-->>DC: events
        DC-->>ACT: events
        ACT->>UI: evtSender.send(event)
        ACT->>ACT: wakeupFn() (optional)
    end

    UI->>ACT: stop()
    ACT->>ACT: write wakeupFd → poll exits
    ACT->>ACT: join thread
```

### 4.2c Docker Tool Execution (Containerized)

```mermaid
sequenceDiagram
    participant SkillRunner
    participant DockerRunner
    participant ContainerMgr
    participant Docker

    SkillRunner->>DockerRunner: run(tool, params)
    DockerRunner->>ContainerMgr: acquireContainer(tool)
    ContainerMgr->>ContainerMgr: lookup/start container (shared or per‑tool)
    ContainerMgr-->>DockerRunner: containerId
    DockerRunner->>DockerRunner: build command (args mode or stdin)
    DockerRunner->>Docker: docker exec -i <containerId> sh -c '<command>'
    Docker-->>DockerRunner: stdout/stderr
    DockerRunner-->>SkillRunner: result
```

**Note**: When a skill declares a `composeFile`, the `SkillRunner` first calls `ComposeManager::startEnvironment` to bring up the compose stack; the tool container is then attached to the resulting network.

---

## 5. Testing Requirements

### 5.1 Unit Tests (Google Test)

| Class                | Test Case                                                   | Verification                                       |
| -------------------- | ----------------------------------------------------------- | -------------------------------------------------- |
| `ToolRunner`         | `run` with `bash` tool                                      | Input "echo hello" → output "hello\n"              |
| `ToolRunner`         | `run` with `args` mode and object params                    | Command receives `--file=test.txt` style arguments |
| `ToolRunner`         | `run` with a command that sleeps 31 seconds                 | Returns `"ERROR: timeout"` (enforces timeoutSecs)  |
| `ToolRunner`         | `runStreaming` basic echo                                   | onChunk called with output, handle.isDone() true   |
| `DependencyGraph`    | `classifyTool` READER prefix                                | Returns READER                                       |
| `DependencyGraph`    | `classifyTool` WRITER prefix                                | Returns WRITER                                       |
| `DependencyGraph`    | `classifyTool` unknown                                      | Returns READ_WRITE                                   |
| `DependencyGraph`    | `buildBatches` mixed readers/writers                        | Correct ordering: readers batch first, then writers  |
| `DependencyGraph`    | `executeBatches` reader command tools                       | `CommandRunner::runAll` called for parallel execution |
| `ToolState`          | `set`/`get`/`has`/`remove`/`clear`                          | All CRUD operations correct                          |
| `SkillManager`       | `executeToolStreaming` system tool                          | Synchronous handler, single onChunk call             |
| `SkillManager`       | `toolState` accessor                                        | Returns reference to `m_toolState`                   |
| `DrivenProvider`     | Non-streaming request completes                             | HTTP 200, response body decoded into events          |
| `DrivenProvider`     | Streaming request emits tokens                              | tick() returns LlmToken events incrementally         |
| `DrivenProvider`     | Cancel in-flight request                                    | active() returns false, no more events               |
| `DrivenCore`         | submitGoal from idle                                        | Transitions to AwaitingLlm, starts request           |
| `DrivenCore`         | runSync returns result                                      | Returns final output text, core idle                 |
| `DrivenCore`         | LLM returns tool calls                                      | Transitions to ExecutingTools                        |
| `DrivenCore`         | Max tool call turns exceeded                                | xFailGoal with error message                         |
| `DrivenCore`         | User message persisted on submitGoal                        | loadMessages(sessionId) contains user role           |
| `DrivenCore`         | Assistant text persisted after runSync                      | loadMessages(sessionId) contains assistant role      |
| `DrivenCore`         | No persistence without session                              | loadMessages(0) returns empty                        |
| `DrivenCore`         | Session switch persists to correct session                  | Messages go to correct session after setSession call |
| `DrivenCore`         | setSession before submit                                    | Session ID used in persistence calls                 |
| `AgentTui`           | Injected session reused on first submit                     | No second session created; user msg persisted        |
| `AgentTui`           | ResumeSession wires DrivenCore                              | New messages go to resumed session                   |
| `AppCoreThread`      | start() launches thread                                     | running() returns true                               |
| `AppCoreThread`      | SubmitGoal command                                          | Core receives goal, events sent back                 |
| `AppCoreThread`      | Shutdown command                                            | Thread exits, running() returns false                |
| `SkillLoader`        | `validateAgainstSchema` valid manifest                      | Returns 0                                            |
| `SkillLoader`        | `validateAgainstSchema` invalid manifest                    | Returns -1, errors populated                         |
| `SkillLoader`        | Schema file missing                                         | Warning printed, validation passes through           |

### 5.2 End‑to‑End Tests (against mock DeepSeek)

#### Positive Tests

| ID     | Scenario        | Steps                                                                                                                    | Expected                                                   |
| ------ | --------------- | ------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------- |
| E2E‑01 | First run       | Start agent with empty components dir                                                                                    | Built‑in tools and meta‑skills created under `components/` |
| E2E‑02 | Infer new tool  | Send goal: "create a tool that counts lines in a file"                                                                   | New tool `count_lines` appears; can be invoked             |
| E2E‑03 | Use tool        | Invoke `count_lines` with `{path:"/etc/passwd"}`                                                                         | Returns number of lines                                    |
| E2E‑04 | Infer new prompt/skill | Send goal: "create a skill that lists files in a directory and filters those containing 'log' using list_files and grep" | New prompt appears; dependencies correct |
| E2E‑05 | Use prompt       | Invoke prompt with `{directory:"/var/log"}`                                                                               | Returns array of existing files with "log" in name         |

#### Skill Schema Validation Tests

| ID     | Scenario                      | Steps                                                                               | Expected                                                       |
| ------ | ----------------------------- | ----------------------------------------------------------------------------------- | -------------------------------------------------------------- |
| E2E‑S1 | Valid skill.json              | Load skills with well-formed manifest                                               | Component loaded, tools+prompts available                      |
| E2E‑S2 | Invalid skill.json            | Load skills with manifest missing required `version` field                          | Warning printed, component skipped                             |
| E2E‑S3 | Schema file missing           | Delete `skills/schema.json`, restart agent                                          | Warning printed, validation disabled, all manifests load       |
| E2E‑S4 | Skill with `streaming` tool   | Define tool with `"streaming": true`, invoke via `executeToolStreaming`             | Streaming handle returned, chunks delivered via callback       |

#### Negative E2E Tests (Critical for Guardrails)

| ID     | Scenario                      | Steps                                                                               | Expected                                                       |
| ------ | ----------------------------- | ----------------------------------------------------------------------------------- | -------------------------------------------------------------- |
| E2E‑N1 | Skill with missing dependency | Create skill that depends on `nonexistent_tool`, then invoke it                     | Error message: `missing dependencies: nonexistent_tool`        |
| E2E‑N2 | Tool that hangs               | Invoke tool that runs `sleep 100`                                                   | Returns `"ERROR: timeout"` within 32 seconds                   |
| E2E‑N3 | Skill using `{{goal}}`        | Create skill with prompt `"Process: {{goal}}"`, invoke with goal "test"             | LLM receives `"Process: test"`                                 |
| E2E‑N4 | Tool with `args` mode         | Create tool with `inputMode: "args"`, command `wc -l`, params `{"input":"a\nb\nc"}` | Command receives arguments correctly (exact behaviour defined) |

### 5.3 Docker‑Specific Tests

#### Unit Tests (with mock Docker CLI)

| Class                 | Test Case                          | Verification                                         |
| --------------------- | ---------------------------------- | ---------------------------------------------------- |
| `ContainerManager`    | `acquireContainer` with HIGH trust | Same container ID for two different high‑trust tools |
| `ContainerManager`    | `acquireContainer` with LOW trust  | Different containers for different tool names        |
| `ContainerManager`    | `pruneIdleContainers`              | Container idle > timeout removed                     |
| `DependencyInstaller` | `installDependencies`              | Idempotent installation                              |
| `ComposeManager`      | `startEnvironment`                 | `docker-compose up -d` called only once per skill    |
| `DockerToolRunner`    | `run` with `args` mode             | Command arguments passed correctly inside container  |
| `DockerToolRunner`    | `run` with timeout                 | Timeout enforced (kills container command)           |
| `DockerToolRunner`    | `runStreaming` pooled              | StreamHandle returned, chunks via callback           |
| `DockerToolRunner`    | `runStreaming` ephemeral           | StreamHandle returned, `docker run --rm` used        |

#### End‑to‑End Tests (with real Docker daemon)

| ID     | Scenario                                     | Expected                             |
| ------ | -------------------------------------------- | ------------------------------------ |
| E2E‑D1 | Basic tool in default image (`ubuntu:22.04`) | Output matches command               |
| E2E‑D2 | `aptDependencies` installed correctly        | Tool can use installed package       |
| E2E‑D3 | Trust level HIGH – container sharing         | Two tools run in same container      |
| E2E‑D4 | Trust level LOW – isolation                  | Each tool gets its own container     |
| E2E‑D5 | Skill with `docker-compose.yml`              | Services start, tool can connect     |
| E2E‑D6 | Container pruning                            | Idle container removed after timeout |
| E2E‑D7 | Persistent compose lifecycle                 | `startPersistent` → tool calls → `stopPersistent` cleanly tears down |
| E2E‑D8 | Playwright browser skill                     | `playwright` skill runs E2E test via browser.sh + bridge.js |

---

## 6. CLI Entry Point

```bash
agent [--a0-dir <path>] [--env-file <path>] [--log-file <path>] [--resume <session-id>]
      [--api-key <key>] [--mock-api <url>]
      [--docker-host <url>] [--container-idle-timeout <seconds>]
      [--max-idle-containers <count>] [--default-docker-image <image>] [--no-docker]
      [--max-parallel <n>] [--external-repo <url>] [--skill-arg <key=val> ...]
      <subcommand>
```

**Subcommands**:

| Subcommand | Flags | Description |
|-----------|-------|-------------|
| *(none)* | — | Interactive REPL mode |
| `run` | `--skill <name> --params <json>` | Execute a skill and exit |
| `terminal` | `--terminal-id <id> --cwd <path>` | PTY terminal session (shell prompt) |
| `kill-all` | — | Stop daemon processes (b1, c2) |
| `session` | `export`, `list` | Session operations |

- `--a0-dir` : Root directory for non-committed agent artifacts (default `./.a0`). Created on startup if missing; on first creation, automatically appended to `.gitignore` when CWD is a git repository. All runtime state (b1 socket/pid, SQLite database, skills store, logs) is scoped under this directory.
- `--env-file` : Path to `.env` file to load (default `./.env`). Each line is `KEY=VALUE`; `#` comments and blank lines are skipped. **The implementation must overwrite existing environment variables** (i.e., use `setenv(key, val, 1)`).
- `--log-file <path>` : Redirect stderr to file. Child daemons derive their own path (e.g., `/tmp/a0.log` → b1 gets `/tmp/a0-b1.log`). All three daemons (a0, b1, c2) support this flag.

- `--api-key` : DeepSeek API key; if not provided, read from environment (see precedence below).
- `--mock-api` : Override the API URL (for testing, e.g., `http://localhost:8080`).

**Terminal subcommand flags**:

- `--terminal-id <id>` : Terminal identifier for c2 stream lookup. Matches the `terminalId` returned by `POST /api/terminal/open`.
- `--cwd <path>` : Working directory for the terminal shell. a0 `chdir()`s to this path before creating the PTY and forking the login shell.

**Execution flags**:

- `--max-parallel <n>` : Maximum concurrent tool executions for the DependencyGraph batching subsystem (default `4`). Controls how many subprocesses run simultaneously in a READER batch.
- `--external-repo <url>` : External a0 repository URL for self-development scripts and tool resources. The repo is cloned into `a0Dir/external/a0/` at startup (or fetched/checkout/reset if already present). Sets global var `A0_SRC_DIR`.
- `--skill-arg <key=val>` : Repeatable flag. Passes arguments to skill tool handlers via `ToolState` (keyed `"args:<skill>-<arg>"`). Bare keys (without `=`) are treated as `key=true`.

**Docker‑specific flags**:

- `--docker-host` : Docker daemon socket URL (default `unix:///var/run/docker.sock`).
- `--container-idle-timeout` : Seconds after which an idle container is pruned (default `300`).
- `--max-idle-containers` : Maximum number of idle containers allowed per pool (default `10`).
- `--default-docker-image` : Default image when tool does not specify `dockerImage` (default `ubuntu:22.04`).
- `--no-docker` : Disable Docker integration; fall back to host runner for all tools.

**Environment variables** (override defaults):

- `A0_DIR` — default `.a0/` root (overridden by `--a0-dir`)
- `A0_DOCKER_HOST`
- `A0_CONTAINER_IDLE_TIMEOUT`
- `A0_MAX_IDLE_CONTAINERS`
- `A0_DEFAULT_DOCKER_IMAGE`

**API key precedence (highest to lowest)**:

1. `--api-key` command line argument.
2. `DEEPSEEK_API_KEY` environment variable (from parent process).
3. `DEEPSEEK_API_KEY` set in `--env-file` (default `.env`).
4. `DEEPSEEK_API_KEY` set in `~/.deepseek.env`.

**Example**:

```bash
export DEEPSEEK_API_KEY="sk-..."
agent --components-dir ./my_components --container-idle-timeout 600 --max-idle-containers 5 \
      --max-parallel 8 --external-repo https://github.com/opensassi/a0 \
      --skill-arg playwright-headless=true
```

---

## 7. Development Instructions

### 7.1 Build Setup

**Requirements**:

- C++17 compiler (GCC 9+, Clang 12+, or MSVC 2019+)
- CMake 3.15+
- libcurl (for HTTP requests to DeepSeek API)
- jsoncpp or nlohmann/json (JSON parsing)
- (Optional) gcov/lcov for coverage, Google Test for unit tests
- **Docker** (for containerized execution) – optional but required for Docker features

**CMakeLists.txt** (as in original, plus `ENABLE_TRACE` and `ENABLE_COVERAGE` options). The Docker sub‑module sources reside in `./src/docker/` and are compiled into the main library.

### 7.2 Startup Sequence

On launch, `main.cpp` follows this order:

1. **Parse flags (two-pass)** — extract `--env-file` first, then all flags including `--a0-dir`, `--max-parallel`, `--external-repo`, `--skill-arg` (repeatable).
2. **Parse `--skill-arg`** — split each `key=val` pair into a map (bare keys → `"true"`).
3. **Resolve API key** — `--api-key` → `DEEPSEEK_API_KEY` env → `.env` → `~/.deepseek.env`.
4. **Initialize `.a0/`** — `ensureA0Dir(a0Dir)` creates the directory if missing. On first creation, if the CWD is a git repository, appends `.a0/` to `.gitignore`.
5. **`--kill-all` cleanup** — reads `.a0/b1.pid` and the c2 PID file, sends SIGTERM/SIGKILL, unlinks sockets, exits.
6. **Component construction** — skill registry (SkillLoader with valijson schema validation), tool runners, Docker managers, etc.
7. **Handler registration** — `xRegisterSystemHandlers()` registers all C++ handlers with `HandlerContext` (carrying `ToolState*`).
8. **Wire settings** — `core.setMaxParallel(maxParallel)`, `core.setExternalRepo(externalRepo)`, `core.setSkillArgs(skillArgs)`.
9. **`core.init(skillsDir, a0Dir)`** — loads manifests (schema-validated), clones external repo if `--external-repo` set, injects skill args into ToolState, generates session ID.
10. **b1 auto-launch** — if not `--no-b1` and not in `--run` mode, start/connect to b1 supervisor using paths under `a0Dir`.
11. **`--run` mode** — execute a skill non-interactively and exit.
12. **Interactive REPL** — `core.run()` blocks until EOF.

---

## 8. File Layout

```
project/
├── CMakeLists.txt
├── c2/
│   └── web/                             # c2 dashboard SPA (WebComponents)
│       ├── index.html
│       ├── css/main.css
│       ├── js/
│       │   ├── app.js                   # Bootstrap, router, selectById, sanitizeId
│       │   ├── store.js
│       │   ├── sse.js
│       │   ├── api.js
│       │   └── components/              # 16 WebComponents, each in own folder
│       │       ├── app-shell/index.js + component.json
│       │       ├── app-header/index.js + component.json
│       │       ├── dashboard-page/index.js + component.json
│       │       └── ... (13 more)
│       └── lib/xterm/                   # xterm.js terminal emulator
├── docker/
│   ├── Dockerfile.playwright            # Playwright browser automation image
│   └── docker-compose.playwright.yml    # Playwright compose stack
├── scripts/
│   ├── browser.sh                       # Browser CLI shim
│   └── playwright-bridge.js             # Node.js HTTP daemon for 23 Playwright actions
├── src/
│   ├── a0_dir.h/.cpp                    # .a0/ directory lifecycle (create, gitignore)
│   ├── llm_provider.h                   # Abstract async LLM provider interface
│   ├── driven_provider.h/.cpp           # Universal curl_multi LLM provider base
│   ├── deepseek_provider.h/.cpp         # DeepSeek subclass of DrivenProvider
│   ├── driven_core.h/.cpp               # Tick-based state machine (replaces AgentCore)
│   ├── command_runner.h/.cpp            # All subprocess management (fork/exec/pipe/alarm)
│   ├── dependency_graph.h/.cpp          # Reader/writer classification + batch execution
│   ├── tool_state.h/.cpp                # Thread-safe per-session key-value state bag
│   ├── handler_results.h                # HandlerResult struct for system tool dispatch
│   ├── system_handlers.h/.cpp           # Free-standing C++ handler functions (xBash, xRead, xGitCommand, etc.)
│   ├── main.cpp
│   ├── agent_core.cpp/.h
│   ├── tool_runner.cpp/.h               # HostToolRunner + DockerToolRunner
│   ├── skill_runner.cpp/.h
│   ├── deepseek_provider.cpp/.h
│   ├── context_manager.cpp/.h
│   ├── dependency_resolver.cpp/.h
│   ├── trace.h
│   ├── stream_registry.h/.cpp           # Streaming IPC support
│   ├── docker_security_filter.h/.cpp    # Docker command filtering
│   ├── mpsc.h                           # MPSC channel (multi-producer single-consumer)
│   ├── response_decoder.h/.cpp          # SSE/JSON LLM response decoder
│   ├── driven_provider.h/.cpp           # Async LLM provider (curl_multi-based)
│   ├── driven_core.h/.cpp               # Tick-based state machine agent core
│   ├── app_core_thread.h/.cpp           # Thread-safe DrivenCore wrapper with poll loop
│   ├── docker/                          # Docker sub‑module (required)
│   │   ├── technical-specification.md
│   │   ├── container_manager.cpp/.h
│   │   ├── compose_manager.cpp/.h
│   │   ├── docker_tool_runner.cpp/.h
│   │   ├── dependency_installer.cpp/.h
│   │   └── CMakeLists.txt
│   └── skills/                          # Skills sub‑module (required)
│       ├── technical-specification.md   # Full skills integration spec
│       ├── skills.h                     # Data structures + SkillManager facade + ToolHandler typedef
│       ├── skill_manager.cpp
│       ├── skill_loader.h/.cpp          # Now with valijson schema validation
│       ├── version_manager.h/.cpp
│       ├── validation_engine.h/.cpp
│       └── CMakeLists.txt
├── test/
│   ├── unit/
│   │   ├── test_tool_runner.cpp
│   │   ├── test_skill_runner.cpp
│   │   ├── test_docker_*.cpp            # Docker unit tests
│   │   ├── test_skill_manager.cpp
│   │   ├── test_skill_loader.cpp
│   │   ├── test_version_manager.cpp
│   │   ├── test_validation_engine.cpp
│   │   ├── test_dependency_graph.cpp    # DependencyGraph batching + execution
│   │   ├── test_tool_state.cpp          # ToolState CRUD + thread safety
│   │   ├── test_pipeline_execution.cpp  # End-to-end pipeline tests
│   │   ├── test_external_a0.cpp         # External repo clone tests
│   │   ├── test_skill_args.cpp          # --skill-arg injection tests
│   │   ├── test_skill_pipeline.cpp      # Multi-step skill pipeline tests
│   │   └── test_skill_schema.cpp        # JSON Schema validation tests
│   ├── e2e/
│   │   ├── mock_deepseek_server.py
│   │   ├── fixtures/
│   │   │   ├── find_related_files_response.json
│   │   │   ├── infer_skill_response.json
│   │   │   ├── infer_tool_response.json
│   │   │   ├── tool_calls_response.json
│   │   │   └── tools_for_prompt_response.json
│   │   ├── run_e2e_tests.sh
│   │   ├── test_playwright_e2e.sh       # 14 Playwright browser E2E tests
│   │   ├── test_c2_dashboard_e2e.sh     # 29-assertion c2 dashboard E2E test (headed browser)
│   │   └── test_c2_agent_interact.sh    # 25-assertion agent interaction page E2E test
├── skills/                          # three-tier namespace
│   ├── schema.json                  # Draft-07 JSON Schema for skill.json validation
│   ├── README.md                    # Developer guide (661 lines)
│   ├── system/                      # shipped with agent (read-only)
│   ├── local/                       # agent-created (writable)
│   │   └── playwright/              # Playwright skill (22 tools, 1 prompt)
│   │       ├── skill.json
│   │       └── prompts/
│   │           └── e2e_test.md
│   └── github_<user>/               # installed from GitHub (read-only)
├── .a0/                             # agent internal state (auto-created, gitignored)
│   ├── b1.pid                       # b1 supervisor PID file
│   ├── b1.sock                      # b1 supervisor Unix socket
│   ├── db/
│   │   └── sessions.db              # SQLite session database
│   ├── store/                       # version archive keyed by commit hash

│   └── lock.json                    # version refcounts + metadata

└── coverage_html/                   # generated after coverage run
```

---

---

## 10. E2E Testing

### 10.1 Test Framework

E2E tests use the **Playwright bridge** (`scripts/playwright-bridge.js`) running on port 3100, controlled via curl or the `selectById` bridge action. Tests open a headed Chromium window for visual debugging and navigate the c2 web dashboard.

**Test scripts**: 
- `test/e2e/test_c2_dashboard_e2e.sh` — 29-assertion dashboard E2E test (navigation, terminal, screenshots)
- `test/e2e/test_c2_agent_interact.sh` — 25-assertion agent interaction page E2E test (message viewer, agent info, input panel)

### 10.2 Running Tests

```bash
# Build with TRACE logging for verbose diagnostics
cmake -B build -DENABLE_TRACE=ON && cmake --build build -j$(nproc)

# Run E2E test (headed browser)
bash test/e2e/test_c2_dashboard_e2e.sh
bash test/e2e/test_c2_agent_interact.sh

# Keep daemons running after test for interactive inspection
bash test/e2e/test_c2_dashboard_e2e.sh --no-cleanup
bash test/e2e/test_c2_agent_interact.sh --no-cleanup
```

### 10.3 Log Files

When run with `--no-cleanup`, log files persist at:

| File | Source | Contents |
|------|--------|----------|
| `/tmp/c2-e2e.log` | c2 daemon | c2 startup, SSE broadcasts, terminal_open, b1 registrations |
| `/tmp/c2-e2e-a0.log` | a0 terminal | a0 terminal: b1 launch, PTY creation, stream chunks |
| `/tmp/c2-e2e-a0-b1.log` | b1 daemon | b1: agent register, stream relay, terminal_open |
| `/tmp/bridge_c2_e2e.log` | Playwright bridge | Bridge HTTP requests |

Each daemon supports `--log-file <path>`: the parent derives and propagates child log paths automatically:
```
c2 --log-file /tmp/c2-e2e.log
  └── a0 terminal --log-file /tmp/c2-e2e-a0.log
        └── b1 --log-file /tmp/c2-e2e-a0-b1.log
```

### 10.4 TRACE Logging

Built with `-DENABLE_TRACE=ON`, the `TRACE_LOG(msg)` macro writes `[TRACE] file:line msg` to stderr. Key trace points:

| Daemon | File | Trace point |
|--------|------|-------------|
| c2 | `c2_listener.cpp` | b1 register, disconnect, stream_data, stream_end, user_prompt |
| c2 | `dashboard_server.cpp` | terminal_open (b1 path + direct fork) |
| c2 | `sse_manager.cpp` | sse broadcast (all events) |
| b1 | `supervisor.cpp` | agent register, disconnect, stream relay, terminal_open |

### 10.5 Test Helpers

The test script provides helpers for shadow-DOM-safe element access:

```bash
# Navigate to element by its component.json ID (handles shadow DOM)
clickById nav-hosts         # Click "Hosts" nav link
typeById terminal-cwd "/tmp"  # Type into terminal cwd input
inspectById terminal-status   # Get terminal status element text

# Log inspection
assert_log /tmp/c2-e2e.log "terminal_open"   # Assert log contains pattern
show_log /tmp/c2-e2e.log 10                    # Show last 10 lines
```

Bridge action `selectById` with `perform: "click"|"type"|"focus"` recursively searches light DOM and all shadow roots to find elements by their component-manifest ID.

### 10.6 WebComponent Architecture

Each c2 web UI component lives in its own directory with a component manifest:

```
c2/web/js/components/
  app-shell/
    index.js          # Component implementation
    component.json    # IDs, routes, events, dependencies
  app-header/
    index.js
    component.json
  dashboard-page/
    index.js
    component.json
  interact-page/
    index.js          # Agent interaction page with conversation view + message input
    component.json
  ...17 components total
```

The `component.json` manifest lists every element ID, its type, action, and whether it's static or dynamically generated:

```json
{
  "name": "app-header",
  "ids": {
    "nav-dashboard": { "type": "link", "action": "navigate /" },
    "nav-hosts":     { "type": "link", "action": "navigate /hosts" }
  },
  "dynamicIds": {
    "host-card-{slug}": { "pattern": "host-card-{slug}", "type": "container" }
  },
  "events": { "emits": ["navigate"], "listens": [] }
}
```

Dynamic IDs are generated by `window.sanitizeId(raw)` which slugifies the raw value and truncates to 24 characters.

### 10.7 Test Assertions

The test runs 29 assertions covering:

| Category | Count | Examples |
|----------|-------|---------|
| Prerequisites | 6 | Binaries exist, ports free, playwright available |
| Launch | 4 | c2 starts, bridge starts, browser launches |
| Navigation | 6 | Click Dashboard/Hosts/Projects/Settings links |
| API | 2 | `/api/status`, `/api/stats` return valid JSON |
| Terminal | 4 | Navigate, status, console errors, screenshot |

The agent interaction E2E test (`test_c2_agent_interact.sh`) runs 25 assertions:

| Category | Count | Examples |
|----------|-------|---------|
| Prerequisites | 5 | Binaries exist, ports free, playwright available |
| Seed DB | 1 | SQLite database created with 5 messages |
| c2 startup | 2 | c2 starts, API ready |
| b1 registration | 2 | Mock b1 registers, c2 confirms agent |
| Bridge | 2 | Bridge starts, ready |
| Agent title | 1 | "Agent Monitor: ..." heading |
| Messages | 2 | All 5 messages load, conversation status shows count |
| Input area | 1 | Message textarea exists |
| Buttons | 2 | "Send (next turn)" and "Send & Interrupt" found |
| Agent details | 1 | Shows session UUID, PID, state, host, project |
| Agent API | 1 | Returns pid\|state\|hostname\|project |
| Screenshot | 1 | Screenshot saved |
| Browser close | 1 | Browser closes cleanly |
| Cleanup | 2 | Browser closes, processes cleaned |

### 10.8 Running the Full Test Suite

Run all three test layers before every commit to verify no regressions. Expected time: ~2 minutes (unit) + ~20 seconds (agent E2E) + ~60 seconds (c2 E2E).

```bash
# 1. Unit tests (34 suites, Google Test, C++)
ctest --test-dir build --output-on-failure

# 2. Agent E2E tests (6 tests, mock DeepSeek API, no browser)
bash test/e2e/run_e2e_tests.sh

# 3. c2 dashboard E2E tests (headed browser, Playwright)
bash test/e2e/test_c2_dashboard_e2e.sh
bash test/e2e/test_c2_agent_interact.sh
```

A convenience runner is available at `test/e2e/run_all_tests.sh` that executes all three layers sequentially.

---

## Appendix A: Docker Integration Sub‑Module Specification

The complete technical specification for the Docker sub‑module is provided in a separate document: `./src/docker/technical-specification.md`. It includes detailed class specifications, sequence diagrams, configuration options, and testing requirements. The sub‑module is **required** for containerized execution; when `--no-docker` is used, the agent reverts to host‑only execution, but the Docker code is still compiled.

**Location**: `./src/docker/technical-specification.md`  
**Source code**: `./src/docker/`

## Appendix B: Skills Sub‑Module Specification

The complete technical specification for the Skills sub‑module is provided in a separate document: `./src/skills/technical-specification.md`. It includes detailed class specifications, sequence diagrams, configuration options, and testing requirements. The sub‑module is **required** — it implements a three-tier namespace system with version validation via historical log replay and GitHub distribution support.

`SkillManager` serves as the unified tool dispatch layer, replacing `SystemToolRegistry`. A handler registry (`m_handlers`) holds C++ functions registered at startup by `xRegisterSystemHandlers()` in `main.cpp`. `executeTool()` and `schemas()` provide the single point of dispatch and schema generation for the LLM.

**Location**: `./src/skills/technical-specification.md`  
**Source code**: `./src/skills/`

## Appendix C: Concurrency Model Specification

The complete technical specification for the concurrency model is provided in a separate document: `./specs/concurrency-model.md`. It covers all concurrent execution in the a0 agent ecosystem — independent threads, async event loops, and inter-process communication — as a high-level C4 architecture where each component is a concurrency context (thread or async loop) rather than a source file.

**Key findings:**
- 9 concurrency contexts across 3 processes (a0: 6, b1: 1, c2: 2)
- All IPC uses Unix domain sockets with `BufferedSocket` for buffered reads (100B chunks, 64KB max)
- Three independent mutex domains in c2 (`C2Listener::m_b1Mutex`, `B1Registry::m_mutex`, `SseManager::m_mutex`) — no nested acquisition, deadlock-free
- `DrivenProvider` is single-thread by design; `curl_multi_wait` required before `perform` for async DNS
- `AppCoreThread` is fully implemented but currently unused (TUI drives `DrivenCore` directly on the FTXUI thread)

**Location**: `./specs/concurrency-model.md`  
**Source code**: Cross-cuts entire source tree (see §10 of the concurrency spec for file mapping)

---


