#include "agent_tui.h"
#include "message_panel.h"
#include "input_panel.h"
#include "status_bar.h"
#include "dialog_manager.h"
#include "session_manager.h"
#include "markdown_renderer.h"
#include "clipboard.h"
#include "trace.h"

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/component/loop.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include <ctime>
#include <iostream>
#include <utility>

namespace a0::tui {

AgentTui::AgentTui(a0::LlmProvider* provider,
                   a0::skills::SkillManager* skillMgr,
                   a0::persistence::PersistenceStore* persistence,
                   int64_t agentId,
                   std::function<bool()> b1Status,
                   int64_t sessionDbId,
                   const std::string& sessionUuid)
    : m_persistence(persistence)
    , m_agentId(agentId)
    , m_b1Status(std::move(b1Status))
    , m_provider(provider)
    , m_drivenCore(std::make_unique<a0::DrivenCore>(m_provider, skillMgr, persistence))
    , m_sessionUuid(sessionUuid)
    , m_sessionDbId(sessionDbId)
    , m_messagePanel(std::make_unique<MessagePanel>())
    , m_inputPanel(std::make_unique<InputPanel>())
    , m_statusBar(std::make_unique<StatusBar>())
    , m_dialogMgr(std::make_unique<DialogManager>())
    , m_sessionMgr(std::make_unique<SessionManager>(persistence))
    , m_markdown(std::make_unique<MarkdownRenderer>())
{
    if (m_sessionDbId > 0) {
        m_drivenCore->setSession(m_sessionDbId, m_sessionUuid);
    }
    TRACE_LOG("AgentTui constructed");
    xBuildLayout();
}

AgentTui::~AgentTui() = default;

void AgentTui::setScreen(ftxui::ScreenInteractive* screen) {
    m_screen = screen;
}

void AgentTui::clearScreen() {
    if (!m_sessionUuid.empty()) {
        m_sessionMgr->endCurrent();
    }
    m_screen = nullptr;
}

int AgentTui::run(bool testMode) {
    ftxui::ScreenInteractive ownedScreen =
        testMode
            ? ftxui::ScreenInteractive::FixedSize(80, 24)
            : ftxui::ScreenInteractive::Fullscreen();
    if (!m_screen) {
        m_screen = &ownedScreen;
    }

    TRACE_LOG("AgentTui::run starting");

    std::cout << "\x1b[?2004h";
    std::flush(std::cout);

    m_inputPanel->component()->TakeFocus();

    auto loop = ftxui::Loop(m_screen, m_mainComponent);
    while (!loop.HasQuitted()) {
        loop.RunOnce();
        xTickCore();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    std::cout << "\x1b[?2004l";
    std::flush(std::cout);

    clearScreen();
    return 0;
}

void AgentTui::shutdown() {
    if (m_screen) {
        m_screen->Exit();
    }
}

void AgentTui::xTickCore() {
    if (!m_drivenCore->idle()) {
        TRACE_LOG("xTickCore: core not idle, ticking...");
        auto events = m_drivenCore->tick();
        TRACE_LOG("xTickCore: tick returned " << events.size() << " events");
        for (auto& ev : events) {
            xHandleEvent(ev);
        }
        bool nowIdle = m_drivenCore->idle();
        TRACE_LOG("xTickCore: core idle=" << nowIdle);
        if (!nowIdle && m_screen) {
            m_screen->RequestAnimationFrame();
        }
    }
}

void AgentTui::xHandleEvent(const mpsc::AppCoreEvent& ev) {
    if (std::holds_alternative<mpsc::LlmToken>(ev)) {
        TRACE_LOG("AgentTui: LlmToken(\"" << std::get<mpsc::LlmToken>(ev).text << "\")");
        xOnToken(std::get<mpsc::LlmToken>(ev).text);
    } else if (std::holds_alternative<mpsc::ToolStart>(ev)) {
        const auto& ts = std::get<mpsc::ToolStart>(ev);
        TRACE_LOG("AgentTui: ToolStart(" << ts.toolName << ")");
        xOnToolStart(ts.toolName, ts.arguments);
    } else if (std::holds_alternative<mpsc::ToolEnd>(ev)) {
        const auto& te = std::get<mpsc::ToolEnd>(ev);
        TRACE_LOG("AgentTui: ToolEnd(" << te.toolName << ")");
        xOnToolEnd(te.toolName, te.output, te.exitCode == 0);
    } else if (std::holds_alternative<mpsc::Complete>(ev)) {
        TRACE_LOG("AgentTui: Complete(\"" << std::get<mpsc::Complete>(ev).text << "\")");
        xOnComplete(std::get<mpsc::Complete>(ev).text);
    } else if (std::holds_alternative<mpsc::Error>(ev)) {
        TRACE_LOG("AgentTui: Error(\"" << std::get<mpsc::Error>(ev).message << "\")");
        xOnError(std::get<mpsc::Error>(ev).message);
    }
}

int AgentTui::resumeSession(const std::string& uuid) {
    int64_t dbId = 0;
    int rc = m_sessionMgr->resume(uuid, dbId);
    if (rc != 0) return -1;

    m_sessionUuid = uuid;
    m_sessionDbId = dbId;
    m_statusBar->setSessionId(uuid);
    m_drivenCore->setSession(dbId, uuid);

    if (m_persistence) {
        auto msgs = m_persistence->loadMessages(dbId);
        m_messagePanel->loadHistory(msgs);
        m_statusBar->setMessageCount(msgs.size());
    }

    return 0;
}

std::string AgentTui::currentSessionId() const {
    return m_sessionUuid;
}

void AgentTui::submitInput(const std::string& input) {
    xHandleSubmit(input);
}

// ============================================================================
// Event handlers
// ============================================================================

void AgentTui::xOnToken(const std::string& token) {
    if (m_streamingEntryIndex < 0) {
        m_streamingText.clear();
        m_streamingEntryIndex = m_messagePanel->beginStreaming(MessageRole::Assistant);
    }

    m_streamingText += token;
    m_messagePanel->streamUpdate(m_streamingEntryIndex, m_streamingText);
    m_statusBar->setMessageCount(m_messagePanel->count());
}

void AgentTui::xOnToolStart(const std::string& name, const std::string& /*arguments*/) {
    m_agentState = AgentState::Executing;
    m_statusBar->setAgentState(m_agentState);
    m_messagePanel->appendToolCall(name, ToolState::Running);
    m_inputPanel->setEnabled(false);
    m_statusBar->setMessageCount(m_messagePanel->count());
}

void AgentTui::xOnToolEnd(const std::string& name, const std::string& output, bool success) {
    ToolState state = success ? ToolState::Completed : ToolState::Failed;
    m_messagePanel->appendToolCall(name, state, output);
    if (m_agentState != AgentState::Thinking) {
        m_agentState = AgentState::Thinking;
        m_statusBar->setAgentState(m_agentState);
    }
}

void AgentTui::xOnComplete(const std::string& fullOutput) {
    if (m_streamingEntryIndex >= 0) {
        m_messagePanel->streamUpdate(m_streamingEntryIndex, fullOutput);
        m_messagePanel->endStream(m_streamingEntryIndex);
        m_streamingEntryIndex = -1;
        m_streamingText.clear();
    } else {
        // Non-streaming response: append as a complete assistant message
        MessageEntry entry;
        entry.role = MessageRole::Assistant;
        entry.content = fullOutput;
        m_messagePanel->append(entry);
    }

    m_agentState = AgentState::Idle;
    m_statusBar->setAgentState(m_agentState);
    m_inputPanel->setEnabled(true);
    m_inputPanel->component()->TakeFocus();
    m_statusBar->setMessageCount(m_messagePanel->count());
}

void AgentTui::xOnError(const std::string& error) {
    if (m_streamingEntryIndex >= 0) {
        m_messagePanel->streamUpdate(m_streamingEntryIndex, error);
        m_messagePanel->endStream(m_streamingEntryIndex);
        m_streamingEntryIndex = -1;
        m_streamingText.clear();
    }

    MessageEntry errEntry;
    errEntry.role = MessageRole::Error;
    errEntry.content = error;
    m_messagePanel->append(errEntry);

    m_agentState = AgentState::Idle;
    m_statusBar->setAgentState(m_agentState);
    m_inputPanel->setEnabled(true);
    m_inputPanel->component()->TakeFocus();
    m_statusBar->setMessageCount(m_messagePanel->count());
}

// ============================================================================
// Goal submission
// ============================================================================

int AgentTui::xHandleSubmit(const std::string& input) {
    if (input.empty()) return 0;

    // Check for commands
    if (input[0] == '/') {
        return xHandleCommand(input);
    }

    // Create session on first input
    if (m_sessionUuid.empty()) {
        std::string uuid = "tui-" + std::to_string(std::time(nullptr));
        int64_t agentId = m_agentId > 0 ? m_agentId : 0;
        m_sessionDbId = m_sessionMgr->create(uuid, agentId);
        m_sessionUuid = uuid;
        m_statusBar->setSessionId(uuid);
        m_drivenCore->setSession(m_sessionDbId, uuid);
    }

    // Expand paste placeholders
    std::string expanded = xExpandPastePlaceholders(input);

    // Add user message to display
    MessageEntry userEntry;
    userEntry.role = MessageRole::User;
    userEntry.content = expanded;
    m_messagePanel->append(userEntry);

    if (expanded != input) {
        MessageEntry sysEntry;
        sysEntry.role = MessageRole::System;
        sysEntry.content = "(Paste expanded: " + std::to_string(expanded.size()) + " chars)";
        m_messagePanel->append(sysEntry);
    }

    // Update state
    m_agentState = AgentState::Thinking;
    m_statusBar->setAgentState(m_agentState);
    m_inputPanel->setEnabled(false);

    // Submit goal to DrivenCore
    m_drivenCore->submitGoal(expanded);

    // Tick immediately to process the first batch of events
    xTickCore();

    // Trigger a re-render to show the user message and [thinking]
    if (m_screen) {
        m_screen->Post([]{});  // Wake FTXUI event loop
    }

    return 0;
}

int AgentTui::xHandleInterrupt() {
    if (m_agentState != AgentState::Idle) {
        m_drivenCore->cancel();

        if (m_streamingEntryIndex >= 0) {
            m_messagePanel->streamUpdate(m_streamingEntryIndex, m_streamingText);
            m_messagePanel->endStream(m_streamingEntryIndex);
            m_streamingEntryIndex = -1;
            m_streamingText.clear();
        }

        MessageEntry entry;
        entry.role = MessageRole::System;
        entry.content = "Interrupted";
        m_messagePanel->append(entry);

        m_agentState = AgentState::Idle;
        m_statusBar->setAgentState(m_agentState);
        m_inputPanel->setEnabled(true);
        m_inputPanel->component()->TakeFocus();

        if (m_screen) {
            m_screen->RequestAnimationFrame();
        }
    }
    return 0;
}

int AgentTui::xHandleCommand(const std::string& cmd) {
    if (cmd == "/sessions") return xCmdSessions();
    if (cmd == "/help")      return xCmdHelp();
    if (cmd == "/clear")     return xCmdClear();
    if (cmd == "/quit" || cmd == "/exit") return xCmdQuit();
    if (cmd == "/export")    return xCmdExport();
    return 0;
}

// ============================================================================
// Commands
// ============================================================================

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

// ============================================================================
// Layout
// ============================================================================

void AgentTui::xBuildLayout() {
    auto mainContainer = xBuildMainContainer();

    // Wire input callbacks
    m_inputPanel->setOnSubmit([this](const std::string& input) {
        xHandleSubmit(input);
    });

    m_inputPanel->setOnInterrupt([this] {
        xHandleInterrupt();
    });

    m_inputPanel->setOnChange([this](const std::string& content) {
        if (m_pastedContents.empty()) return;
        std::unordered_map<int, std::string> valid;
        for (auto& [id, stored] : m_pastedContents) {
            std::string marker = "[ PASTED #" + std::to_string(id) + " ]";
            if (content.find(marker) != std::string::npos) {
                valid[id] = stored;
            }
        }
        m_pastedContents = std::move(valid);
    });

    m_dialogMgr->setMainComponent(mainContainer);
    auto wrapped = m_dialogMgr->component();

    // Renderer wrapper: tick DrivenCore before every frame
    auto coreTicker = ftxui::Renderer(wrapped, [this, wrapped]() {
        xTickCore();
        return wrapped->Render();
    });

    // Outer CatchEvent for paste detection and mouse tracking
    m_mainComponent = ftxui::CatchEvent(coreTicker, [this](ftxui::Event event) -> bool {
        auto& input = event.input();

        // Bracketed paste start marker
        if (input == "\x1b[200~") {
            m_pasteActive = true;
            m_pasteBuffer.clear();
            return true;
        }

        // Bracketed paste end marker
        if (input == "\x1b[201~") {
            m_pasteActive = false;
            xProcessPasteBuffer();
            return true;
        }

        // During paste: accumulate and block children
        if (m_pasteActive) {
            m_pasteBuffer += input;
            return true;
        }

        // Mouse events for copy-on-select
        if (event.is_mouse()) {
            auto& mouse = event.mouse();
            if (mouse.button == ftxui::Mouse::Left) {
                if (mouse.motion == ftxui::Mouse::Pressed) {
                    m_mouseDown = true;
                    m_mouseMoved = false;
                } else if (mouse.motion == ftxui::Mouse::Moved && m_mouseDown) {
                    m_mouseMoved = true;
                } else if (mouse.motion == ftxui::Mouse::Released && m_mouseDown) {
                    if (m_mouseMoved && m_screen) {
                        std::string sel = m_screen->GetSelection();
                        if (!sel.empty()) {
                            copyToClipboard(sel);
                            m_statusBar->showStatus("Copied!", 3);
                        }
                    }
                    m_mouseDown = false;
                }
            }
            return false;
        }

        return false;
    });
}

ftxui::Component AgentTui::xBuildMainContainer() {
    return ftxui::Container::Vertical({
        m_statusBar->component(),
        m_messagePanel->component() | ftxui::flex,
        m_inputPanel->component(),
    });
}

// ============================================================================
// Bracketed paste
// ============================================================================

std::string AgentTui::xExpandPastePlaceholders(const std::string& input) {
    if (m_pastedContents.empty() || input.find("[ PASTED #") == std::string::npos) {
        return input;
    }
    std::string result = input;
    for (auto& [id, content] : m_pastedContents) {
        std::string marker = "[ PASTED #" + std::to_string(id) + " ]";
        size_t pos = 0;
        while ((pos = result.find(marker, pos)) != std::string::npos) {
            result.replace(pos, marker.size(), content);
            pos += content.size();
        }
    }
    return result;
}

void AgentTui::xProcessPasteBuffer() {
    if (m_pasteBuffer.empty()) return;

    while (!m_pasteBuffer.empty() &&
           (m_pasteBuffer.back() == '\n' || m_pasteBuffer.back() == '\r')) {
        m_pasteBuffer.pop_back();
    }

    if (m_pasteBuffer.size() <= 20) {
        m_inputPanel->insertText(m_pasteBuffer);
    } else {
        int id = ++m_pasteCounter;
        m_pastedContents[id] = m_pasteBuffer;
        m_inputPanel->insertText("[ PASTED #" + std::to_string(id) + " ] ");
    }
    m_pasteBuffer.clear();
}

} // namespace a0::tui
