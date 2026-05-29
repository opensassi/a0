# Technical Paper: Minimal Component‑Based Agent (C++ Implementation)

## Version 2.1 – Docker Integration and Containerized Execution

---

## 1. Overview

This document provides the complete technical specification and development plan for a **minimal self‑evolving agent** written in **C++17**. The agent connects to the DeepSeek API, maintains a file‑based repository of **tools** (atomic bash commands) and **skills** (LLM prompts with eager tool calls, parameter substitution, optional validators), and runs inside a VM isolation environment (or directly on a host). All logs are stored locally.

**Core features**:

- Tools and skills share JSON Schema I/O (strings, arrays, objects).
- Skills support **eager tool calls** (`{{tool:name ...}}`) that execute before the LLM request.
- Skills support **parameter substitution** (`{{key}}`) where `key` is a field from the invocation parameters (e.g., `{{goal}}`).
- **Validators** (optional chain of tools) process the LLM response; validators are simple tool names (future extension for transformation bindings).
- **Dependencies** declared per skill (tools and other skills required) – **must be checked before execution**.
- **Timeout enforcement** for all tool executions (30 seconds default).
- **Docker execution mode**: Tools can run inside isolated containers (default Ubuntu 22.04) with configurable trust levels (HIGH/MEDIUM/LOW), shared container pools, custom images, and `apt` dependencies. Skills can bring up multi‑service environments using `docker-compose.yml`.
- Performance monitoring (optional, for future C++ optimization).
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
};

struct ValidatorBinding {
    std::string toolName;
    std::optional<std::string> transform; // future: JSONPath binding
};

struct Skill {
    std::string name;
    std::string description;
    std::string prompt;                   // may contain {{tool:...}} and {{key}} placeholders
    std::vector<std::string> dependencies;    // tool and skill names required
    std::vector<ValidatorBinding> validators; // post-LLM processing chain

    // Docker compose support
    std::string composeFile;                     // path to docker-compose.yml (relative to skill dir)
    std::vector<std::string> aptDependencies;    // rolled up into container(s) used by skill's tools
};
```

### 2.2 Abstract Interfaces

```cpp
class ComponentRegistry {
public:
    virtual ~ComponentRegistry() = default;
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
    // Runs a tool with the given parameters. Must enforce a 30-second timeout.
    // Returns the stdout output as a string (or JSON for structured outputs).
    // On timeout, returns a string starting with "ERROR: timeout".
    // On command failure, returns a string starting with "ERROR:".
    virtual json run(const Tool& tool, const json& params) = 0;
};

class InferenceProvider {
public:
    virtual ~InferenceProvider() = default;
    virtual std::string complete(const std::string& systemPrompt,
                                  const std::string& userPrompt) = 0;
    virtual void setMockUrl(const std::string& url) = 0;
};

struct ContextFrame {
    std::string role;
    std::string content;
};

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

struct LogEntry {
    std::string sessionId;
    int64_t timestamp;
    std::string eventType;
    std::string data;
};

class InvocationLogger {
public:
    virtual ~InvocationLogger() = default;
    virtual void log(const LogEntry& entry) = 0;
    virtual bool replay(const std::string& sessionId,
                        std::function<void(const LogEntry&)> callback) = 0;
    virtual std::vector<std::string> listSessions() const = 0;
};

class DependencyResolver {
public:
    virtual ~DependencyResolver() = default;
    virtual bool checkToolDependencies(const Tool& tool) const = 0;
    virtual bool checkSkillDependencies(const Skill& skill) const = 0;
    virtual std::vector<std::string> missingDependencies(const Skill& skill) const = 0;
};

class SkillRunner {
public:
    virtual ~SkillRunner() = default;
    // Expands skill.prompt by:
    //   1. Replacing {{key}} placeholders with values from params (where key matches a top-level field).
    //   2. Replacing {{tool:name key="value" ...}} placeholders with the result of executing the named tool.
    virtual std::string expandPrompt(const Skill& skill, const json& params) = 0;
    // Runs the validator chain. If any validator returns a string starting with "ERROR:",
    // the final result is prefixed with "VALIDATOR_ERROR:".
    virtual json runValidators(const Skill& skill, const json& input) = 0;
    // Executes the skill: expandPrompt -> InferenceProvider.complete() -> runValidators.
    // Before calling complete, it must verify that all dependencies are present (using DependencyResolver).
    virtual json execute(const Skill& skill, const json& params) = 0;
};

class SchemaInferenceEngine {
public:
    virtual ~SchemaInferenceEngine() = default;
    virtual Tool inferTool(const std::string& naturalLanguageDescription) = 0;
    virtual Skill inferSkill(const std::string& naturalLanguageDescription) = 0;
};

class AgentCore {
public:
    virtual ~AgentCore() = default;
    virtual bool init(const std::string& componentsDir) = 0;
    // Processes a user goal. Matches skills by exact name (case-sensitive) against the goal string.
    // If no exact match, uses SchemaInferenceEngine to infer a new skill.
    // Before executing a skill (whether found or inferred), checks dependencies via DependencyResolver.
    virtual json processGoal(const std::string& goal) = 0;
    virtual bool resumeSession(const std::string& sessionId) = 0;
    virtual std::string currentSessionId() const = 0;
    virtual void run() = 0;
};

// === Docker Integration Interfaces ===
// Full implementations reside in ./src/docker/

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
    virtual std::string startEnvironment(const Skill& skill, const std::string& skillDirectory) = 0;
    virtual void stopEnvironment(const Skill& skill) = 0;
    virtual void markUsed(const Skill& skill) = 0;
};

class DockerToolRunner : public ToolRunner {
public:
    virtual ~DockerToolRunner() = default;
    // Constructor (in implementation) will take ContainerManager* and ComposeManager*.
};
```

---

## 3. System Architecture

The architecture includes both host and Docker execution paths, with mandatory dependency check and container pooling.

```mermaid
graph TB
    User((User))
    DeepSeek[("DeepSeek API")]

    subgraph "Agent Process"
        Core[AgentCore]
        Context[ContextManager]
        Reg[ComponentRegistry]
        HostRunner[HostToolRunner]
        DockerRunner[DockerToolRunner]
        SkillRunner[SkillRunner]
        InfEngine[SchemaInferenceEngine]
        Logger[InvocationLogger]
        DepResolver[DependencyResolver]
        ContainerMgr[ContainerManager]
        ComposeMgr[ComposeManager]
    end

    subgraph "File System"
        ComponentsDir[(components/)]
        LogsDir[(logs/)]
    end

    subgraph "Docker Engine"
        Containers[(Containers)]
        ComposeStacks[(Compose Stacks)]
    end

    User --> Core
    Core --> Reg
    Core --> DepResolver
    Core --> HostRunner
    Core --> DockerRunner
    Core --> SkillRunner
    SkillRunner --> DepResolver
    SkillRunner --> DeepSeek
    SkillRunner --> HostRunner
    SkillRunner --> DockerRunner
    Core --> InfEngine
    InfEngine --> DeepSeek
    InfEngine --> Reg
    Core --> Logger
    Logger --> LogsDir
    Reg --> ComponentsDir

    DockerRunner --> ContainerMgr
    DockerRunner --> ComposeMgr
    ContainerMgr --> Containers
    ComposeMgr --> ComposeStacks
```

**Caption**: The agent supports two tool runners: `HostToolRunner` (for direct subprocess execution) and `DockerToolRunner` (for containerized execution with pooling and compose environments). `ContainerManager` and `ComposeManager` are implemented in the `./src/docker` sub‑directory.

---

## 4. Detailed Data Flow

The following sequence diagrams show the execution paths for skills and Docker‑based tools.

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

### 4.2 Docker Tool Execution (Containerized)

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
| `ToolRunner`         | `run` with `split_lines` tool                               | Input "a\nb\nc" → output `["a","b","c"]`           |
| `ToolRunner`         | `run` with `bash` tool                                      | Input "echo hello" → output "hello\n"              |
| `ToolRunner`         | `run` with `args` mode and object params                    | Command receives `--file=test.txt` style arguments |
| `ToolRunner`         | `run` with a command that sleeps 31 seconds                 | Returns `"ERROR: timeout"` (enforces 30s limit)    |
| `SkillRunner`        | `expandPrompt` with `{{goal}}` placeholder                  | Substitutes value from `params["goal"]`            |
| `SkillRunner`        | `expandPrompt` with eager tool call                         | Eager execution, substitution                      |
| `SkillRunner`        | `runValidators` with failing validator                      | Result starts with `"VALIDATOR_ERROR: ERROR: ..."` |
| `SkillRunner`        | `execute` when dependencies missing                         | Returns error message, does not call LLM           |
| `ComponentRegistry`  | `loadFromDirectory` with malformed JSON                     | Skips file, logs warning, continues                |
| `DependencyResolver` | `checkSkillDependencies` on skill with missing tool         | Returns false, missing list includes the tool      |
| `InvocationLogger`   | `log` then `replay`                                         | Log two entries, replay → callback receives both   |
| `AgentCore`          | `processGoal` with exact skill name match                   | Uses the skill, does not infer a new one           |
| `AgentCore`          | `processGoal` with non‑existent goal and inference disabled | Falls back to inference (or returns error)         |

### 5.2 End‑to‑End Tests (against mock DeepSeek)

#### Positive Tests

| ID     | Scenario        | Steps                                                                                                                    | Expected                                                   |
| ------ | --------------- | ------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------- |
| E2E‑01 | First run       | Start agent with empty components dir                                                                                    | Built‑in tools and meta‑skills created under `components/` |
| E2E‑02 | Infer new tool  | Send goal: "create a tool that counts lines in a file"                                                                   | New tool `count_lines` appears; can be invoked             |
| E2E‑03 | Use tool        | Invoke `count_lines` with `{path:"/etc/passwd"}`                                                                         | Returns number of lines                                    |
| E2E‑04 | Infer new skill | Send goal: "create a skill that lists files in a directory and filters those containing 'log' using list_files and grep" | New skill appears; dependencies correct                    |
| E2E‑05 | Use skill       | Invoke skill with `{directory:"/var/log"}`                                                                               | Returns array of existing files with "log" in name         |

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

#### End‑to‑End Tests (with real Docker daemon)

| ID     | Scenario                                     | Expected                             |
| ------ | -------------------------------------------- | ------------------------------------ |
| E2E‑D1 | Basic tool in default image (`ubuntu:22.04`) | Output matches command               |
| E2E‑D2 | `aptDependencies` installed correctly        | Tool can use installed package       |
| E2E‑D3 | Trust level HIGH – container sharing         | Two tools run in same container      |
| E2E‑D4 | Trust level LOW – isolation                  | Each tool gets its own container     |
| E2E‑D5 | Skill with `docker-compose.yml`              | Services start, tool can connect     |
| E2E‑D6 | Container pruning                            | Idle container removed after timeout |

---

## 6. CLI Entry Point

```bash
agent --components-dir <path> [--env-file <path>] [--resume <session-id>]
      [--api-key <key>] [--mock-api <url>]
      [--docker-host <url>] [--container-idle-timeout <seconds>]
      [--max-idle-containers <count>] [--default-docker-image <image>] [--no-docker]
```

- `--components-dir` : Root directory for components (default `./components`).
- `--env-file` : Path to `.env` file to load (default `./.env`). Each line is `KEY=VALUE`; `#` comments and blank lines are skipped. **The implementation must overwrite existing environment variables** (i.e., use `setenv(key, val, 1)`).
- `--resume` : Session ID to replay from `./logs/`.
- `--api-key` : DeepSeek API key; if not provided, read from environment (see precedence below).
- `--mock-api` : Override the API URL (for testing, e.g., `http://localhost:8080`).

**Docker‑specific flags**:

- `--docker-host` : Docker daemon socket URL (default `unix:///var/run/docker.sock`).
- `--container-idle-timeout` : Seconds after which an idle container is pruned (default `300`).
- `--max-idle-containers` : Maximum number of idle containers allowed per pool (default `10`).
- `--default-docker-image` : Default image when tool does not specify `dockerImage` (default `ubuntu:22.04`).
- `--no-docker` : Disable Docker integration; fall back to host runner for all tools.

**Environment variables** (override defaults):

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
agent --components-dir ./my_components --container-idle-timeout 600 --max-idle-containers 5
```

---

## 7. Development Instructions (Revised)

### 7.1 Build Setup

**Requirements**:

- C++17 compiler (GCC 9+, Clang 12+, or MSVC 2019+)
- CMake 3.15+
- libcurl (for HTTP requests to DeepSeek API)
- jsoncpp or nlohmann/json (JSON parsing)
- (Optional) gcov/lcov for coverage, Google Test for unit tests
- **Docker** (for containerized execution) – optional but required for Docker features

**CMakeLists.txt** (as in original, plus `ENABLE_TRACE` and `ENABLE_COVERAGE` options). The Docker sub‑module sources reside in `./src/docker/` and are compiled into the main library.

### 7.2 Implementation Plan (Test‑Driven, 90% Coverage)

Follow these steps **in order**:

#### Step 1: Write individual specification files for each source module

- For each `.cpp` file, create a `.spec.md` describing input/output contracts, error handling, and edge cases.

#### Step 1b: Interface assertion tests

- Before writing any implementation, create compile‑time or runtime assertions that verify each interface’s contract is internally consistent.
  - For `ToolRunner`: write a test that expects `run` to handle `inputMode == "args"` and enforce a timeout (even with a stub).
  - For `SkillRunner`: write a test that passes a `params` object with a `{{goal}}` placeholder and asserts that the expanded prompt contains the substituted value.
  - These tests initially fail against stubs, forcing the implementer to provide the required behaviour.

#### Step 2: Write stub implementations with no logic

- Implement all functions with empty bodies or `return {};` (or throw `std::logic_error("stub")`).
- Ensure the code compiles and links.

#### Step 3: Implement tests against the stub

- Use Google Test.
- Write unit tests that **expect failure** (assertions that the stub does not yet satisfy the spec).
- Also write “stub tests” that verify the stub returns default values without crashing.
- All tests should pass against the stub **only** for trivial cases; most functional tests fail.

#### Step 4: Implement code incrementally

- For each function, write the minimal implementation to pass its tests.
- Commit after each passing test.
- Use **TDD red‑green‑refactor** cycle.

#### Step 5: Ensure tests pass – when unsure, consult spec file

- The spec file is **authoritative**; fix either the test or the implementation accordingly.

#### Step 6: Use code coverage tool – enforce 90% coverage

- Configure CMake with `--coverage` (GCC/Clang).
- Run `lcov --capture --directory . --output-file coverage.info`
- Generate HTML report: `genhtml coverage.info --output-directory coverage_html`
- **Requirement**: line coverage ≥90%, function coverage ≥90%, branch coverage ≥90%.
- Any uncovered lines must be justified (e.g., defensive `assert` or unreachable error handling).

#### Step 7: Exhaustive trace logging with `#ifdef TRACE`

- Define `TRACE` macro in build.
- Log every function entry, exit, important variable values, tool execution, LLM request/response, validator chain steps.
- Log to `stderr` or a separate file (`trace.log`).

#### Step 8: Set up end‑to‑end testing with mock DeepSeek API

- Create a mock HTTP server (e.g., using `cpp-httplib` or a simple Python script) that returns fixture data.
- Fixtures stored in `test/fixtures/deepseek/`.

#### Step 8a: Negative E2E tests

- In addition to happy‑path tests, write end‑to‑end tests that deliberately trigger error conditions:
  - Missing dependency.
  - Timeout.
  - Parameter substitution.
  - `args` mode.
- Each negative test must verify that the agent returns an appropriate error message and does not crash.

#### Step 9: Run E2E tests after every change (CI hook)

- They must pass before merging.

#### Step 10: Implement Docker sub‑module

The Docker sub‑module is a **required** part of the agent for containerized tool execution. Its complete technical specification is located in `./src/docker/technical-specification.md` and the implementation source code resides in `./src/docker/`. The key implementation phases are:

10.1 Implement `ContainerManager` and `DependencyInstaller` using Docker CLI calls (via `popen` or libcurl over the Docker socket).
10.2 Implement `DockerToolRunner` and integrate into `AgentCore` (tool runner selection based on `dockerImage` presence).
10.3 Implement `ComposeManager` for skill‑level compose environments.
10.4 Add pruning logic and CLI flags as described in Section 6.
10.5 Write unit tests with a mock Docker executable (e.g., a bash script that simulates `docker` commands).
10.6 Run Docker‑specific E2E tests with a real Docker daemon (if available in CI).

All Docker‑related code must be placed in `./src/docker/`, with its own `CMakeLists.txt` included from the top‑level `CMakeLists.txt`.

---

## 8. File Layout

```
project/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── agent_core.cpp/.h
│   ├── component_registry.cpp/.h
│   ├── tool_runner.cpp/.h               # HostToolRunner (renamed from SubprocessToolRunner)
│   ├── skill_runner.cpp/.h
│   ├── deepseek_provider.cpp/.h
│   ├── context_manager.cpp/.h
│   ├── invocation_logger.cpp/.h
│   ├── schema_inference_engine.cpp/.h
│   ├── dependency_resolver.cpp/.h
│   ├── trace.h
│   └── docker/                         # Docker sub‑module (required)
│       ├── technical-specification.md  # Full Docker integration spec
│       ├── container_manager.cpp/.h
│       ├── compose_manager.cpp/.h
│       ├── docker_tool_runner.cpp/.h
│       ├── dependency_installer.cpp/.h
│       └── CMakeLists.txt
├── test/
│   ├── unit/
│   │   ├── test_tool_runner.cpp
│   │   ├── test_skill_runner.cpp
│   │   ├── test_docker_*.cpp            # Docker unit tests
│   │   └── ...
│   ├── e2e/
│   │   ├── mock_deepseek_server.py
│   │   ├── fixtures/
│   │   └── run_e2e_tests.sh
├── specs/                         # one .spec.md per source file
├── logs/                          # created at runtime
├── components/                    # created at runtime
└── coverage_html/                 # generated after coverage run
```

---

## 9. Development Workflow Summary

1. Write spec files for each module.
2. **Write interface assertion tests** that codify the contract.
3. Write stub implementations.
4. Write unit tests (both stub‑passing and failing functional tests).
5. Implement code iteratively until all tests pass.
6. Run coverage tool; ensure ≥90% coverage.
7. Run **positive and negative** E2E tests with mock DeepSeek API; fix any failures.
8. Implement Docker sub‑module (following `./src/docker/technical-specification.md`).
9. Run Docker‑specific tests (unit and E2E with real Docker).
10. Enable `-DTRACE` for debug builds; keep trace logs for diagnosis.
11. Commit and CI verifies all tests + coverage.

---

## 10. Future Extensions

- Streaming LLM responses and validator chains.
- C++ compilation of performance‑critical validators.
- Input/output transformation mappers in validator objects (e.g., JSONPath).
- Sandboxing for `bash` tool (command allowlist).
- Native `mustache` templating for prompt expansion.
- **LXC backend** as alternative to Docker (for environments without Docker).
- Resource limits (CPU/memory) per tool in Docker.

---

## Appendix A: Docker Integration Sub‑Module Specification

The complete technical specification for the Docker sub‑module is provided in a separate document: `./src/docker/technical-specification.md`. It includes detailed class specifications, sequence diagrams, configuration options, and testing requirements. The sub‑module is **required** for containerized execution; when `--no-docker` is used, the agent reverts to host‑only execution, but the Docker code is still compiled.

**Location**: `./src/docker/technical-specification.md`  
**Source code**: `./src/docker/`

---
