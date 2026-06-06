# AppCoreThread Spec

## 1. Overview

Thread-safe application core that owns a `DrivenCore` and runs it in a dedicated thread with a `ppoll()`-based event loop. Commands arrive via an MPSC `Receiver<mpsc::Command>`, events are sent back via an MPSC `Sender<mpsc::AppCoreEvent>`. Uses an eventfd wakeup for graceful shutdown. Receives persona name, skills, and tools at construction and passes them to `DrivenCore` on startup.

**Source files:** `src/app_core_thread.h/.cpp`

**Dependencies:** `driven_core.h`, `deepseek_provider.h`, `mpsc.h`, `skills/skills.h`, `persistence/persistence_store.h`

## 2. Component Specifications

```cpp
namespace a0 {

class AppCoreThread {
public:
    AppCoreThread(const std::string& apiKey,
                  const std::string& model,
                  a0::skills::SkillManager* skillMgr,
                  a0::persistence::PersistenceStore* persistence = nullptr,
                  const std::string& personaName = "",
                  const std::vector<std::string>& personaSkills = {},
                  const std::vector<std::string>& personaTools = {});
    ~AppCoreThread();

    void setMockUrl(const std::string& url);

    void start(mpsc::Receiver<mpsc::Command> cmdRcvr,
               mpsc::Sender<mpsc::AppCoreEvent> evtSender,
               std::function<void()> wakeupFn = nullptr);

    void setWakeupFn(std::function<void()> fn);
    void stop();
    bool running() const { return m_running.load(); }

private:
    std::string m_apiKey, m_model, m_mockUrl;
    std::string m_personaName;
    std::vector<std::string> m_personaSkills;
    std::vector<std::string> m_personaTools;
    a0::skills::SkillManager* m_skillMgr;
    a0::persistence::PersistenceStore* m_persistence;

    mpsc::Receiver<mpsc::Command> m_cmdReceiver;
    mpsc::Sender<mpsc::AppCoreEvent> m_evtSender;
    std::function<void()> m_wakeupFn;

    int m_wakeupFd = -1;
    std::thread m_thread;
    std::atomic<bool> m_running{false};

    void xRun();
};

} // namespace a0
```

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph External
        UI[CLI / UI Thread]
        CMD_S[Command Sender]
        EVT_R[Event Receiver]
    end

    subgraph AppCoreThread
        THD[std::thread]
        RUN[xRun ppoll loop]

        subgraph Poll_FDs
            WFD[wakeupFd]
            CFD[cmdFd from Receiver]
        end

        subgraph Owned_Objects
            PROV[DeepSeekProvider]
            CORE[DrivenCore]
        end
    end

    subgraph Channels
        CMD_CH[mpsc::Channel&lt;Command&gt;]
        EVT_CH[mpsc::Channel&lt;AppCoreEvent&gt;]
    end

    UI -->|SubmitGoal / Cancel / SetSession / ListSessions / ResumeSession / Shutdown| CMD_S
    CMD_S --> CMD_CH
    CMD_CH --> CFD
    CFD --> RUN
    RUN --> CORE
    CORE --> PROV
    CORE -->|events| EVT_CH
    EVT_CH --> EVT_R --> UI
    UI -->|stop()| WFD --> RUN
```

## 4. Data Flow

```mermaid
sequenceDiagram
    participant UI as CLI/UI
    participant ACT as AppCoreThread
    participant CORE as DrivenCore
    participant PROV as DeepSeekProvider

    UI->>ACT: start(cmdRcvr, evtSender)
    ACT->>ACT: spawn thread → xRun()
    ACT->>CORE: setPersona, setPersonaSkills, setPersonaTools

    UI->>ACT: cmdSender.send(SetSession{dbId, uuid})
    ACT->>ACT: poll returns → drain commands
    ACT->>CORE: setSession(dbId, uuid)
    ACT->>UI: evtSender.send(SessionReady{...})

    UI->>ACT: cmdSender.send(SubmitGoal{goal})
    ACT->>CORE: submitGoal(goal)
    ACT->>UI: evtSender.send(LlmToken{"[thinking]\\n"})

    loop ppoll/tick
        ACT->>ACT: ppoll(cmdFd, wakeupFd, provider timeout)
        ACT->>ACT: handle SIGCHLD (waitpid)
        ACT->>ACT: drain commands (if any)
        ACT->>CORE: tick()
        CORE->>PROV: tick()
        PROV-->>CORE: events
        CORE-->>ACT: events
        ACT->>UI: evtSender.send(event)
        ACT->>UI: wakeupFn() (if set)
    end

    UI->>ACT: stop()
    ACT->>ACT: write wakeupFd
    ACT->>ACT: poll returns → m_running = false
    ACT->>ACT: join thread
```

## 5. Testing Requirements

| Test | Verification |
|------|-------------|
| start() launches thread | running() returns true |
| SubmitGoal command | Core receives goal, events sent back |
| SetSession command | Core session set, SessionReady sent |
| ListSessions command | Persistence queried, SessionList sent |
| ResumeSession command | Messages loaded, SessionHistory sent |
| Cancel command | Core.cancel() called, returns to idle |
| Shutdown command | Thread exits, running() returns false |
| stop() from idle | Thread joins cleanly, no crash |
| SIGCHLD handling | Zombie children reaped in poll loop |
| setMockUrl before start | Provider created with mock URL |
| Persona setters called | core.setPersona/Skills/Tools invoked in xRun() |
