# DockerComposeManager Spec

## §1. Overview
Manages Docker Compose environments for skills. Bridges skill lifecycle events to docker-compose up/down operations and tracks running stacks with idle timeouts. Owns `ComposeStackInfo` entries mapped by prompt name. Delegates all CLI calls to `DockerCLIWrapper`.

**Base class:** `ComposeManager` (from `shared/agent_interfaces.h:186`)
**Source files:** `compose_manager.h`, `compose_manager.cpp`
**Dependencies:** `DockerCLIWrapper` (static utility), `shared/agent_interfaces.h` (Prompt, ComposeManager)
**Lifecycle:** Created per-session with idle timeout. Stacks accumulate until explicitly stopped or implicitly pruned by caller.

## §2. Component Specifications

```cpp
struct ComposeStackInfo {
    std::string composeFile;   // path to the compose file for this stack
    std::string networkName;   // Docker network name for this stack
    time_t lastUsed;           // timestamp of last activity
};

class DockerComposeManager : public ComposeManager {
public:
    /**
     * @param idleTimeout Seconds before a stack is considered idle
     */
    explicit DockerComposeManager(int idleTimeout);

    /**
     * @brief  Start a compose environment for a prompt
     * @param  prompt         The prompt requesting the environment
     * @param  skillDirectory Filesystem path to the prompt's compose file
     * @return The Docker network name for the started stack, or empty string on failure
     */
    std::string startEnvironment(const Prompt& prompt,
                                  const std::string& skillDirectory) override;

    /**
     * @brief  Stop and remove a compose environment
     * @param  prompt The prompt whose environment to tear down
     * @retval void  Errors are swallowed
     */
    void stopEnvironment(const Prompt& prompt) override;

    /**
     * @brief  Bump the last-used timestamp for a prompt's stack
     * @param  prompt The prompt to mark
     * @retval void  No-op if stack does not exist
     */
    void markUsed(const Prompt& prompt) override;

    /**
     * @brief  Record which prompt is currently active
     * @param  prompt The active prompt
     * @retval void  Stores prompt.name internally
     */
    void setCurrentPrompt(const Prompt& prompt) override;

    /**
     * @brief  Get the network of the currently active prompt
     * @return Network name string, or empty if none set
     */
    std::string getCurrentNetwork() const override;

    /**
     * @brief  Clear the currently active prompt name
     * @retval void  Resets m_currentPromptName to empty
     */
    void clearCurrentPrompt() override;

    // --- Persistent compose (multi-call lifecycle) ---

    /**
     * @brief  Start a persistent compose environment that stays alive across multiple tool calls
     * @param  name           Logical name for the persistent stack
     * @param  composeFile    Path to docker-compose.yml
     * @param  skillDirectory Working directory
     * @return Network name, or empty on failure
     */
    std::string startPersistent(const std::string& name,
                                 const std::string& composeFile,
                                 const std::string& skillDirectory) override;

    /**
     * @brief  Tear down a persistent compose environment
     * @param  name Logical name of the persistent stack
     */
    void stopPersistent(const std::string& name) override;

    /**
     * @brief  Check if a compose stack is in persistent mode
     * @param  name Logical name to check
     * @return true if persistent
     */
    bool isPersistent(const std::string& name) const override;

private:
    int m_idleTimeout;                                              // idle threshold in seconds
    std::unordered_map<std::string, ComposeStackInfo> m_stacks;    // prompt name → stack info
    std::unordered_set<std::string> m_persistentStacks;            // names of persistent stacks
    std::string m_currentPromptName;                               // currently active prompt name
};
```

## §3. Architecture Diagram

```mermaid
graph TB
    subgraph Callers
        Session[Session Manager]
        ToolRunner[DockerToolRunnerImpl]
    end

    subgraph DockerComposeManager
        DCM[DockerComposeManager]
        Stacks[m_stacks: unordered_map]
        Persistent[m_persistentStacks: unordered_set]
        Current[m_currentPromptName: string]
    end

    subgraph CLI
        DCW[DockerCLIWrapper]
    end

    Session -->|startEnvironment / stopEnvironment| DCM
    Session -->|setCurrentPrompt / clearCurrentPrompt| DCM
    ToolRunner -->|getCurrentNetwork| DCM
    DCM -->|composeUp / composeDown| DCW
    DCM -->|getNetworkName| DCW
    DCM -->|read/write| Stacks
    DCM -->|read/write| Persistent
    DCM -->|read/write| Current
```

## §4. Data Flow

```mermaid
sequenceDiagram
    participant S as Session
    participant DCM as DockerComposeManager
    participant Map as m_stacks
    participant CLI as DockerCLIWrapper

    S->>DCM: startEnvironment(prompt, dir)
    DCM->>DCM: prompt.composeFile empty?
    alt empty
        DCM-->>S: return ""
    end
    DCM->>Map: find(prompt.name)
    alt exists
        DCM->>Map: update lastUsed
        DCM-->>S: return networkName
    end
    DCM->>CLI: composeUp(composeFile, dir)
    CLI-->>DCM: throws on failure
    alt composeUp throws
        DCM-->>S: return ""
    end
    DCM->>CLI: getNetworkName(composeFile, dir)
    CLI-->>DCM: networkName
    DCM->>Map: store(name → ComposeStackInfo)
    DCM-->>S: return networkName

    S->>DCM: stopEnvironment(prompt)
    DCM->>Map: find(prompt.name)
    alt not found
        DCM-->>S: return (no-op)
    end
    DCM->>CLI: composeDown(composeFile, "")
    CLI-->>DCM: errors swallowed
    DCM->>Map: erase(prompt.name)
```

### Persistent Compose Flow

```mermaid
sequenceDiagram
    participant SR as SkillRunner
    participant DCM as DockerComposeManager
    participant CLI as DockerCLIWrapper

    SR->>DCM: startPersistent(name, composeFile, dir)
    DCM->>DCM: composeFile empty?
    alt empty
        DCM-->>SR: return ""
    end
    DCM->>DCM: find(name) in m_stacks?
    alt exists
        DCM->>DCM: m_persistentStacks.insert(name)
        DCM->>DCM: update lastUsed
        DCM-->>SR: return networkName
    end
    DCM->>CLI: composeUp(composeFile, dir)
    DCM->>CLI: getNetworkName(composeFile, dir)
    DCM->>DCM: store in m_stacks + m_persistentStacks
    DCM-->>SR: networkName

    SR->>DCM: stopPersistent(name)
    DCM->>DCM: find(name) in m_stacks?
    alt not found
        DCM-->>SR: return (no-op)
    end
    DCM->>CLI: composeDown(composeFile, "")
    DCM->>DCM: erase from m_persistentStacks + m_stacks
```

## §5. Testing Requirements

| Method | Test case | Expected outcome |
|--------|-----------|-----------------|
| `startEnvironment` | composeFile empty | Returns `""`, no CLI call |
| `startEnvironment` | Existing stack | Returns cached networkName, updates timestamp |
| `startEnvironment` | New stack, composeUp succeeds | Returns networkName, entry in m_stacks |
| `startEnvironment` | composeUp throws | Returns `""` (error caught), no map entry |
| `stopEnvironment` | Existing stack | composeDown called, entry erased |
| `stopEnvironment` | Non-existent stack | No-op |
| `markUsed` | Existing stack | lastUsed updated |
| `markUsed` | Non-existent stack | No-op |
| `setCurrentPrompt` | Any prompt | m_currentPromptName set |
| `getCurrentNetwork` | Current prompt has stack | Returns networkName |
| `getCurrentNetwork` | No current prompt | Returns `""` |
| `clearCurrentPrompt` | After set | m_currentPromptName cleared |
| `startPersistent` | New stack | composeUp called, entry in both maps |
| `startPersistent` | Already running | Existing stack, timestamp updated |
| `startPersistent` | composeFile empty | Returns `""` |
| `stopPersistent` | Active persistent stack | composeDown, entries removed from both maps |
| `stopPersistent` | Non-existent stack | No-op |
| `isPersistent` | Stack is in m_persistentStacks | true |
| `isPersistent` | Ephemeral or missing | false |

## §6. (not used)

## §7. CLI Entry Point

`DockerComposeManager` is instantiated in `main.cpp` with the `--container-idle-timeout` value (default 300s). A pointer to it is passed to `DockerToolRunnerImpl` as the `ComposeManager*` argument. `SkillRunner` calls `startEnvironment`/`stopEnvironment` before/after skill execution, and `DockerToolRunnerImpl` calls `getCurrentNetwork()` to attach containers to the compose network.
