#include "agent_tui.h"
#include "message_panel.h"
#include "input_panel.h"
#include "status_bar.h"
#include "dialog_manager.h"
#include "session_manager.h"
#include "markdown_renderer.h"
#include "clipboard.h"

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/component/loop.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include <ctime>
#include <utility>

namespace a0::tui {

AgentTui::AgentTui(::AgentCore* core,
                   ::a0::persistence::PersistenceStore* persistence,
                   ::a0::skills::SkillManager* skills,
                   std::function<bool()> b1Status,
                   bool noPermissions)
    : m_core(core)
    , m_persistence(persistence)
    , m_skills(skills)
    , m_b1Status(std::move(b1Status))
    , m_noPermissions(noPermissions)
    , m_messagePanel(std::make_unique<MessagePanel>())
    , m_inputPanel(std::make_unique<InputPanel>())
    , m_statusBar(std::make_unique<StatusBar>())
    , m_dialogMgr(std::make_unique<DialogManager>())
    , m_sessionMgr(std::make_unique<SessionManager>(persistence))
    , m_markdown(std::make_unique<MarkdownRenderer>())
{
    xBuildLayout();
}

AgentTui::~AgentTui() = default;

int AgentTui::run(bool testMode) {
    auto screen = testMode
        ? ftxui::ScreenInteractive::FixedSize(80, 24)
        : ftxui::ScreenInteractive::Fullscreen();
    m_screen = &screen;

    // Set initial focus to the input panel before starting the loop.
    // TakeFocus on the root component recursively activates the first
    // focusable child (Input inside CatchEvent inside Container inside Modal).
    m_mainComponent->TakeFocus();

    auto loop = ftxui::Loop(&screen, m_mainComponent);
    loop.Run();

    if (!m_sessionUuid.empty()) {
        m_sessionMgr->endCurrent();
    }

    m_screen = nullptr;
    return 0;
}

void AgentTui::shutdown() {
    if (m_screen) {
        m_screen->ExitLoopClosure()();
    }
}

int AgentTui::resumeSession(const std::string& uuid) {
    int64_t dbId = 0;
    int rc = m_sessionMgr->resume(uuid, dbId);
    if (rc != 0) return -1;

    m_sessionUuid = uuid;
    m_sessionDbId = dbId;

    if (m_persistence) {
        auto msgs = m_persistence->loadMessages(dbId);
        m_messagePanel->loadHistory(msgs);
        m_statusBar->setMessageCount(msgs.size());
    }

    m_statusBar->setSessionId(uuid);
    return 0;
}

std::string AgentTui::currentSessionId() const {
    return m_sessionUuid;
}

void AgentTui::xBuildLayout() {
    auto mainContainer = xBuildMainContainer();

    m_inputPanel->setOnSubmit([this](const std::string& input) {
        xHandleSubmit(input);
    });

    m_inputPanel->setOnInterrupt([this] {
        xHandleInterrupt();
    });

    m_dialogMgr->setMainComponent(mainContainer);
    auto wrapped = m_dialogMgr->component();

    // Wrap with mouse selection handler for copy-on-select.
    // Does NOT consume mouse events — child components still receive them.
    m_mainComponent = ftxui::CatchEvent(wrapped, [this](ftxui::Event event) -> bool {
        if (event.is_mouse()) {
            auto& mouse = event.mouse();
            if (mouse.button == ftxui::Mouse::Left) {
                if (mouse.motion == ftxui::Mouse::Pressed) {
                    m_mouseDown = true;
                    m_mouseMoved = false;
                } else if (mouse.motion == ftxui::Mouse::Moved && m_mouseDown) {
                    m_mouseMoved = true;
                } else if (mouse.motion == ftxui::Mouse::Released && m_mouseDown) {
                    m_mouseDown = false;
                    if (m_mouseMoved) {
                        // Read PRIMARY selection (set by terminal text selection)
                        // and copy to clipboard via OSC 52 + xclip fallback
                        std::string selected;
                        FILE* pipe = ::popen("xclip -o -selection primary 2>/dev/null", "r");
                        if (pipe) {
                            char buf[4096];
                            size_t n;
                            while ((n = ::fread(buf, 1, sizeof(buf), pipe)) > 0)
                                selected.append(buf, n);
                            ::pclose(pipe);
                        }
                        if (!selected.empty())
                            copyToClipboard(selected);
                    }
                }
            }
        }
        return false;  // don't consume — let children handle the event
    });
}

ftxui::Component AgentTui::xBuildMainContainer() {
    return ftxui::Container::Vertical({
        m_statusBar->component(),
        m_messagePanel->component() | ftxui::flex,
        m_inputPanel->component(),
    });
}

int AgentTui::xHandleSubmit(const std::string& input) {
    if (input.empty()) return 0;
    if (input[0] == '/') {
        return xHandleCommand(input);
    }
    return xProcessGoal(input);
}

int AgentTui::xHandleInterrupt() {
    if (!m_streaming || !m_screen) return 0;
    m_screen->Post([this] { xOnInterrupted(); });
    return 0;
}

int AgentTui::xHandleCommand(const std::string& cmd) {
    if (cmd == "/sessions") return xCmdSessions();
    if (cmd == "/help") return xCmdHelp();
    if (cmd == "/clear") return xCmdClear();
    if (cmd == "/quit" || cmd == ":q") return xCmdQuit();
    if (cmd == "/export") return xCmdExport();
    return 0;
}

int AgentTui::xProcessGoal(const std::string& goal) {
    if (!m_screen) return -1;

    // Use session from AgentCore (created centrally by ensureSession).
    // This avoids the FK crash from creating a session with agentId=0.
    if (m_core) {
        m_core->ensureSession();
        m_sessionUuid = m_core->currentSessionId();
        m_sessionDbId = m_core->sessionDbId();
    }
    if (m_sessionUuid.empty()) {
        m_sessionUuid = "tui-" + std::to_string(std::time(nullptr));
    }

    m_agentState = AgentState::Thinking;
    m_statusBar->setSessionId(m_sessionUuid);
    m_statusBar->setAgentState(m_agentState);

    MessageEntry userEntry;
    userEntry.role = MessageRole::User;
    userEntry.content = goal;
    userEntry.timestamp = std::time(nullptr);
    int userIdx = m_messagePanel->append(userEntry);

    m_streamingEntryIndex = m_messagePanel->beginStreaming(MessageRole::Assistant);
    m_streaming = true;

    m_statusBar->setMessageCount(m_messagePanel->count());
    m_inputPanel->setEnabled(false);
    m_inputPanel->clear();

    // Defer completion to event loop to avoid nested mutation
    std::string response = "Processing: " + goal;
    m_screen->Post([this, response, userIdx] {
        (void)userIdx;
        if (m_streamingEntryIndex >= 0) {
            m_messagePanel->streamUpdate(m_streamingEntryIndex, response);
            m_messagePanel->endStream(m_streamingEntryIndex);
        }
        m_streaming = false;
        m_streamingEntryIndex = -1;
        m_agentState = AgentState::Idle;
        m_statusBar->setAgentState(m_agentState);
        m_statusBar->setMessageCount(m_messagePanel->count());
        m_inputPanel->setEnabled(true);
        m_inputPanel->focus();
    });

    return 0;
}

void AgentTui::xOnToken(const std::string& token) {
    if (!m_screen) return;
    m_screen->Post([this, token] {
        if (m_streamingEntryIndex >= 0) {
            m_messagePanel->streamUpdate(m_streamingEntryIndex, token);
        }
    });
}

void AgentTui::xOnToolStart(const std::string& name, const nlohmann::json& /*params*/) {
    if (!m_screen) return;
    m_screen->Post([this, name] {
        m_messagePanel->appendToolCall(name, ToolState::Running);
        m_agentState = AgentState::Executing;
        m_statusBar->setAgentState(m_agentState);
    });
}

void AgentTui::xOnToolEnd(const std::string& name, const std::string& output, bool success) {
    if (!m_screen) return;
    m_screen->Post([this, name, output, success] {
        auto state = success ? ToolState::Completed : ToolState::Failed;
        m_messagePanel->appendToolCall(name, state, output);
        m_agentState = AgentState::Thinking;
        m_statusBar->setAgentState(m_agentState);
    });
}

void AgentTui::xOnComplete(const std::string& fullOutput) {
    if (!m_screen) return;
    m_screen->Post([this, fullOutput] {
        if (m_streamingEntryIndex >= 0) {
            m_messagePanel->streamUpdate(m_streamingEntryIndex, fullOutput);
            m_messagePanel->endStream(m_streamingEntryIndex);
        }
        m_streaming = false;
        m_streamingEntryIndex = -1;
        m_agentState = AgentState::Idle;
        m_statusBar->setAgentState(m_agentState);
        m_statusBar->setMessageCount(m_messagePanel->count());
        m_inputPanel->setEnabled(true);
        m_inputPanel->focus();
    });
}

void AgentTui::xOnError(const std::string& error) {
    if (!m_screen) return;
    m_screen->Post([this, error] {
        MessageEntry errEntry;
        errEntry.role = MessageRole::Error;
        errEntry.content = error;
        m_messagePanel->append(errEntry);
        m_streaming = false;
        m_streamingEntryIndex = -1;
        m_agentState = AgentState::Error;
        m_statusBar->setAgentState(m_agentState);
        m_inputPanel->setEnabled(true);
    });
}

void AgentTui::xOnInterrupted() {
    if (!m_screen) return;
    if (m_streamingEntryIndex >= 0) {
        m_messagePanel->endStream(m_streamingEntryIndex);
    }
    MessageEntry sysEntry;
    sysEntry.role = MessageRole::System;
    sysEntry.content = "Interrupted";
    m_messagePanel->append(sysEntry);
    m_streaming = false;
    m_streamingEntryIndex = -1;
    m_agentState = AgentState::Idle;
    m_statusBar->setAgentState(m_agentState);
    m_inputPanel->setEnabled(true);
}

int AgentTui::xCmdSessions() {
    auto sessions = m_sessionMgr->list(20);
    std::vector<std::pair<std::string, std::string>> items;
    for (const auto& s : sessions) {
        items.emplace_back(s.uuid, s.uuid);
    }
    if (items.empty()) {
        items.emplace_back("(no sessions)", "");
    }
    m_dialogMgr->showList("Sessions", items, [this](const std::string& id) {
        if (!id.empty()) {
            resumeSession(id);
        }
    });
    return 0;
}

int AgentTui::xCmdHelp() {
    m_dialogMgr->showHelp();
    return 0;
}

int AgentTui::xCmdClear() {
    m_messagePanel->clear();
    return 0;
}

int AgentTui::xCmdQuit() {
    shutdown();
    return 0;
}

int AgentTui::xCmdExport() {
    m_statusBar->showStatus("Export not yet implemented", 3);
    return 0;
}

} // namespace a0::tui
