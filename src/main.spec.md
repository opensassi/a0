# Main Spec

## 1. Overview
Entry-point module. Parses CLI flags in two passes, loads `.env` files, resolves the DeepSeek API key through a priority chain, instantiates all concrete components (registry, providers, runners, Docker managers), wires them into `DefaultAgentCore`, and runs the interactive loop.

## 2. Component Specifications

### `loadEnvFile`
```cpp
/**
 * @param path Path to a .env file
 * Reads lines in KEY=VALUE format, skips empty/comment (#) lines.
 * Calls setenv(KEY, VALUE, 1) for each valid pair.
 * Silently returns if the file cannot be opened.
 */
static void loadEnvFile(const std::string& path);
```

### `hasFlag`
```cpp
/**
 * @param argc main() argument count
 * @param argv main() argument vector
 * @param name Flag name (e.g. "--no-docker")
 * @retval true  Flag present in argv
 * @retval false Flag absent
 */
static bool hasFlag(int argc, char* argv[], const std::string& name);
```

### `getFlag`
```cpp
/**
 * @param argc        main() argument count
 * @param argv        main() argument vector
 * @param name        Flag name (e.g. "--api-key")
 * @param defaultVal  Fallback value
 * @return Value from argv, then from env var A0_<NAME>
 *         (e.g. --container-idle-timeout → A0_CONTAINER_IDLE_TIMEOUT),
 *         then defaultVal.
 */
static std::string getFlag(int argc, char* argv[],
                            const std::string& name,
                            const std::string& defaultVal);
```

### `main`
```cpp
/**
 * Entry point.
 * @param argc Argument count
 * @param argv Argument vector
 * @retval 0  Normal exit
 * @retval 1  Component initialization failure
 *
 * Flag parsing (two-pass):
 *   1st pass: extract --env-file before any env var reads
 *   2nd pass: --env-file, --components-dir, --api-key, --mock-api, --resume
 *
 * API key resolution order:
 *   1. --api-key CLI flag
 *   2. DEEPSEEK_API_KEY env var (from env or .env)
 *   3. ~/.deepseek.env file
 *
 * Docker conditional on --no-docker flag:
 *   - present: dockerRunner and composeMgr remain nullptr
 *   - absent:  DockerContainerManager, DockerComposeManager,
 *              DockerToolRunnerImpl allocated
 *
 * Wire-up order:
 *   skillManager (replaces legacy registry),
 *   toolRunner, provider, context, logger,
 *   depResolver, inferenceEngine, skillRunner → DefaultAgentCore
 *
 * CLI subcommand routing:
 *   "a0 skill list/install/remove/gc/validate/pin" → SkillManager methods
 *   Other arguments → interactive REPL via AgentCore.run()
 *
 * Post-init: resume session if --resume given,
 *            then call core.run() (interactive REPL).
 * Cleanup: delete Docker objects in reverse order.
 *
 * Wire-up detail:
 *   SkillManager is constructed with "skills/", "./.a0/store", "./.a0/logs"
 *   SkillManager::loadAll() called during init
 *   DefaultAgentCore receives SkillManager* via new constructor parameter
 */
int main(int argc, char* argv[]);
```

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph Entry
        M[main]
        LF[loadEnvFile]
        HF[hasFlag]
        GF[getFlag]
    end

    subgraph Core_Components
        SM[SkillManager]              # Skills sub-module facade
        SKR[FileSystemSkillRegistry]  # Legacy adapter
        TR[SubprocessToolRunner]
        DP[DeepSeekProvider]
        CM[DefaultContextManager]
        LG[JsonLinesLogger]
        DR[DefaultDependencyResolver]
        IE[DefaultSchemaInferenceEngine]
        SR[DefaultSkillRunner]
        CORE[DefaultAgentCore]
    end

    subgraph Skills_SubModule
        SL[SkillLoader]
        VM[VersionManager]
        VE[ValidationEngine]
    end

    subgraph Docker_Components
        DCM[DockerContainerManager]
        DCoM[DockerComposeManager]
        DTR[DockerToolRunnerImpl]
    end

    subgraph Config
        ENV[.env file]
        HOMEENV[~/.deepseek.env]
        CLI[argv flags]
    end

    M -->|first pass| CLI
    M -->|--env-file| ENV
    M -->|--api-key / env / ~/.deepseek.env| DP
    M --> HF
    M -->|--no-docker?| DCM
    M -->|--no-docker?| DCoM
    M -->|--no-docker?| DTR
    M -->|a0 skill ...| SM

    M --> SM
    M --> TR
    M --> DP
    M --> CM
    M --> LG
    M --> DR
    M --> IE
    M --> SR
    M --> CORE

    SM --> SL
    SM --> VM
    SM --> VE
    VE --> LG

    SR --> TR
    SR --> DP
    SR --> SM
    SR --> DR
    SR --> DTR
    SR --> DCoM

    CORE --> SM
    CORE --> TR
    CORE --> SR
    CORE --> DP
    CORE --> CM
    CORE --> LG
    CORE --> DR
    CORE --> IE
    CORE --> DTR
    CORE --> DCoM

    DTR --> DCM
    DTR --> DCoM

    CLI --> LF
    LF --> ENV
    LF --> HOMEENV
```

## 4. Data Flow

```mermaid
sequenceDiagram
    participant OS as OS (argv, env)
    participant M as main()
    participant LF as loadEnvFile()
    participant SM as SkillManager
    participant DP as DeepSeekProvider
    participant DockerMgr as Docker Managers
    participant SR as DefaultSkillRunner
    participant CORE as DefaultAgentCore
    participant User as User (stdin/stdout)
    participant SL as SkillLoader

    OS->>M: argc, argv
    M->>M: First pass: extract --env-file
    M->>LF: loadEnvFile(".env")
    M->>M: Second pass: parse remaining flags
    M->>M: API key: --api-key ? env ? ~/.deepseek.env
    alt --no-docker absent
        M->>DockerMgr: new DockerContainerManager(...)
        M->>DockerMgr: new DockerComposeManager(...)
        M->>DockerMgr: new DockerToolRunnerImpl(...)
    else --no-docker present
        M->>DockerMgr: All nullptr
    end
    M->>DP: DeepSeekProvider(apiKey)
    M->>M: setMockUrl(mockUrl) if --mock-api

    rect rgb(240, 240, 255)
        Note over M,SM: Skills sub-module init
        M->>SM: SkillManager("skills/", ".a0/store", ".a0/logs")
        SM->>SL: loadAll()
        SL-->>SM: index built
        SM-->>M: ready
    end

    M->>SR: DefaultSkillRunner(toolRunner, provider, skillManager, ...)
    M->>CORE: DefaultAgentCore(skillManager, ..., dockerRunner, ...)

    alt argv == "a0 skill ..."
        M->>SM: dispatch command (list/install/remove/gc/validate)
        SM-->>User: result
        M->>OS: return 0
    else interactive REPL
        alt --resume <id>
            M->>CORE: resumeSession(sessionId)
        end
        M->>CORE: init(componentsDir)
        alt init fails
            CORE-->>M: false
            M->>OS: return 1
        else init succeeds
            CORE-->>M: true
        end
        M->>CORE: run()
        loop REPL
            CORE->>User: prompt
            User->>CORE: input
            CORE->>CORE: processGoal(input)
            CORE-->>User: result
        end
        CORE-->>M: return
    end
    M->>DockerMgr: delete dockerRunner, composeMgr, containerMgr
    M->>OS: return 0
```

## 5. Error Handling

| Error Condition | Signal | Notes |
|---|---|---|
| `loadEnvFile` file not found | Silent return | Not an error — env file is optional |
| `.env` parse error (no `=`) | Line skipped | Malformed lines silently ignored |
| CLI flag missing value (`--api-key` without arg) | Unexpected next flag used as value | Known limitation |
| `std::stoi` parse failure | Falls back to default | Exceptions caught by `catch(...)` |
| `core.init` fails | Prints error to `cerr`, `return 1` | |
| Docker init with no Docker daemon | Likely throws in constructor | Propagates uncaught |
| API key not found anywhere | Provider constructs with empty key | Runtime inference failure |

## 6. Edge Cases

| Edge Case | Behavior |
|---|---|
| No `--env-file` flag | Defaults to `.env` in CWD |
| `--env-file` specified twice | Second value wins |
| Both `--api-key` and `DEEPSEEK_API_KEY` env | CLI flag takes priority |
| `--no-docker` combined with Docker-requiring tools | `dockerRunner` is `nullptr`; `SkillRunner` must handle |
| `--resume` with invalid session ID | `resumeSession` returns `false`, continues with fresh session |
| No flags at all | All defaults: `.env`, `./skills`, no Docker constraints |
| `skillsDir` does not exist | `SkillManager::loadAll` returns -1, program exits with 1 |
| `a0 skill install` with no args | Prints usage, exits cleanly |
| `a0 skill install` validation fails | Prints failure report, exits 1, no files changed |
| `a0 skill list` with installed skills | Prints table of all namespaces |
| Empty `.env` file | No environment variables set, no error |
| `--container-idle-timeout` set to non-numeric string | Defaults to 300 |

## 7. Testing Requirements

| Method | Test Case |
|---|---|
| `loadEnvFile` | Valid file, missing file, malformed line, comment lines, duplicate keys |
| `hasFlag` | Flag present, flag absent, partial match, `--` terminator |
| `getFlag` | Flag with value, flag without value, env var fallback, default fallback, `A0_` env var mapping |
| `main` (integration) | No flags, all flags, `--no-docker`, `--resume` valid, `--resume` invalid, missing API key, Docker unavailable, init failure |
| `main` (`a0 skill` routing) | `a0 skill list` → SkillManager::listSkills called, output printed |
| `main` (`a0 skill` install) | `a0 skill install https://github.com/alice/utils` → install flow executed |
| `main` (`a0 skill` validation fail) | Install fails validation → error message printed, exit 1 |
| `main` (SkillManager init fail) | `skills/` directory missing → exit 1 |
