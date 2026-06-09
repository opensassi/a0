#include "agent_tui.h"
#include "message_panel.h"
#include "input_panel.h"
#include "status_bar.h"
#include "dialog_manager.h"
#include "markdown_renderer.h"
#include "clipboard.h"
#include "shared/trace.h"

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/component/loop.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include <ctime>
#include <iostream>
#include <thread>
#include <utility>

namespace a0::tui {

AgentTui::AgentTui(mpsc::Sender<mpsc::Command> cmdSender,
                   mpsc::Receiver<mpsc::AppCoreEvent> evtReceiver,
                   std::function<bool()> b1Status,
                   bool testMode)
    : m_cmdSender(std::move(cmdSender))
    , m_evtReceiver(std::move(evtReceiver))
    , m_b1Status(std::move(b1Status))
    , m_testMode(testMode)
    , m_messagePanel(std::make_unique<MessagePanel>())
    , m_inputPanel(std::make_unique<InputPanel>())
    , m_statusBar(std::make_unique<StatusBar>())
    , m_dialogMgr(std::make_unique<DialogManager>())
    , m_markdown(std::make_unique<MarkdownRenderer>())
{
    TRACE_LOG("AgentTui constructed");
    xBuildLayout();
}

AgentTui::~AgentTui() = default;

void AgentTui::setScreen(ftxui::ScreenInteractive* screen) {
    m_screen = screen;
}

void AgentTui::clearScreen() {
    m_screen = nullptr;
}

int AgentTui::run() {
    ftxui::ScreenInteractive ownedScreen =
        m_testMode
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
        drainEvents();
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

void AgentTui::drainEvents() {
    auto events = m_evtReceiver.drain();
    if (!events.empty()) {
        for (auto& ev : events) {
            xHandleCoreEvent(ev);
        }
        if (m_screen) {
            m_screen->RequestAnimationFrame();
        }
    }
}

void AgentTui::xHandleCoreEvent(const mpsc::AppCoreEvent& ev) {
    if (std::holds_alternative<mpsc::LlmToken>(ev)) {
        xOnToken(std::get<mpsc::LlmToken>(ev).text);
    } else if (std::holds_alternative<mpsc::ToolStart>(ev)) {
        const auto& ts = std::get<mpsc::ToolStart>(ev);
        xOnToolStart(ts.toolName, ts.arguments);
    } else if (std::holds_alternative<mpsc::ToolEnd>(ev)) {
        const auto& te = std::get<mpsc::ToolEnd>(ev);
        xOnToolEnd(te.toolName, te.output, te.exitCode == 0);
    } else if (std::holds_alternative<mpsc::RoundComplete>(ev)) {
        xOnRoundComplete(std::get<mpsc::RoundComplete>(ev).text);
    } else if (std::holds_alternative<mpsc::Complete>(ev)) {
        xOnComplete(std::get<mpsc::Complete>(ev).text);
    } else if (std::holds_alternative<mpsc::Error>(ev)) {
        xOnError(std::get<mpsc::Error>(ev).message);
    } else if (std::holds_alternative<mpsc::SessionReady>(ev)) {
        const auto& sr = std::get<mpsc::SessionReady>(ev);
        xOnSessionReady(sr.dbId, sr.uuid);
    } else if (std::holds_alternative<mpsc::SessionList>(ev)) {
        const auto& sl = std::get<mpsc::SessionList>(ev);
        xOnSessionList(sl.entries);
    } else if (std::holds_alternative<mpsc::SessionHistory>(ev)) {
        const auto& sh = std::get<mpsc::SessionHistory>(ev);
        xOnSessionHistory(sh.dbId, sh.uuid, sh.messages);
    }
}

void AgentTui::submitInput(const std::string& input) {
    xHandleSubmit(input);
}

// ============================================================================
// Event handlers
// ============================================================================

void AgentTui::xOnToken(const std::string& token) {
    m_streamingText += token;
    if (m_assistantEntryIndex >= 0) {
        m_messagePanel->appendOrUpdateAssistantText(m_assistantEntryIndex, m_streamingText);
    }
}

void AgentTui::xOnToolStart(const std::string& name, const std::string& arguments) {
    m_agentState = AgentState::Executing;
    m_statusBar->setAgentState(m_agentState);
    if (m_assistantEntryIndex >= 0) {
        m_messagePanel->appendAssistantTool(m_assistantEntryIndex, name, ToolState::Running, arguments);
    }
    m_inputPanel->setEnabled(false);
    m_statusBar->setMessageCount(m_messagePanel->count());
}

void AgentTui::xOnToolEnd(const std::string& name, const std::string& output, bool success) {
    ToolState state = success ? ToolState::Completed : ToolState::Failed;
    if (m_assistantEntryIndex >= 0) {
        m_messagePanel->updateLastAssistantTool(m_assistantEntryIndex, state, output);
    }
    if (m_agentState != AgentState::Thinking) {
        m_agentState = AgentState::Thinking;
        m_statusBar->setAgentState(m_agentState);
    }
}

void AgentTui::xOnRoundComplete(const std::string& text) {
    if (m_assistantEntryIndex >= 0) {
        m_messagePanel->endCurrentAssistantText(m_assistantEntryIndex);
        m_streamingText.clear();
    }
    m_statusBar->setMessageCount(m_messagePanel->count());
    if (m_screen) {
        m_screen->RequestAnimationFrame();
    }
}

void AgentTui::xOnComplete(const std::string& fullOutput) {
    if (m_assistantEntryIndex >= 0) {
        const std::string& text = m_streamingText.empty() ? fullOutput : m_streamingText;
        m_messagePanel->appendOrUpdateAssistantText(m_assistantEntryIndex, text);
        m_messagePanel->finalizeAssistant(m_assistantEntryIndex);
        m_assistantEntryIndex = -1;
        m_streamingText.clear();
    }

    m_agentState = AgentState::Idle;
    m_statusBar->setAgentState(m_agentState);
    m_inputPanel->setEnabled(true);
    m_inputPanel->component()->TakeFocus();
    m_statusBar->setMessageCount(m_messagePanel->count());
}

void AgentTui::xOnError(const std::string& error) {
    if (m_assistantEntryIndex >= 0) {
        const std::string& text = m_streamingText.empty() ? error : m_streamingText;
        m_messagePanel->appendOrUpdateAssistantText(m_assistantEntryIndex, text);
        m_messagePanel->finalizeAssistant(m_assistantEntryIndex);
        m_assistantEntryIndex = -1;
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

void AgentTui::xOnSessionReady(int64_t dbId, const std::string& uuid) {
    m_sessionDbId = dbId;
    m_sessionUuid = uuid;
    m_statusBar->setSessionId(uuid);
}

void AgentTui::xOnSessionList(const std::vector<mpsc::SessionList::Entry>& sessions) {
    std::vector<std::pair<std::string, std::string>> items;
    for (const auto& s : sessions) {
        std::string label = s.uuid.substr(0, 8) + " (" + std::to_string(s.messageCount) + " msgs)";
        items.emplace_back(label, s.uuid);
    }
    if (items.empty()) {
        items.emplace_back("(no sessions)", "");
    }
    m_dialogMgr->showList("Sessions", items, [this](const std::string& id) {
        if (!id.empty()) {
            m_cmdSender.send(mpsc::ResumeSession{id});
        }
    });
}

void AgentTui::xOnSessionHistory(int64_t dbId, const std::string& uuid,
                                  const std::vector<mpsc::SessionMessage>& messages) {
    if (!uuid.empty()) {
        m_sessionDbId = dbId;
        m_sessionUuid = uuid;
        m_statusBar->setSessionId(uuid);
        m_messagePanel->clear();

        for (const auto& m : messages) {
            MessageEntry entry;
            if (m.role == "user") {
                entry.role = MessageRole::User;
                entry.content = m.content;
            } else if (m.role == "assistant") {
                entry.role = MessageRole::Assistant;
                entry.content = m.content;
            } else if (m.role == "tool") {
                entry.role = MessageRole::Tool;
                entry.toolName = m.name;
                entry.toolOutput = m.content;
                entry.collapsed = true;
                entry.toolState = ToolState::Completed;
            } else if (m.role == "system") {
                entry.role = MessageRole::System;
                entry.content = m.content;
            } else {
                entry.role = MessageRole::Error;
                entry.content = m.content;
            }
            m_messagePanel->append(entry);
        }
        m_statusBar->setMessageCount(messages.size());
    }
}

// ============================================================================
// Goal submission
// ============================================================================

int AgentTui::xHandleSubmit(const std::string& input) {
    if (input.empty()) return 0;

    if (input[0] == '/') {
        return xHandleCommand(input);
    }

    std::string expanded = xExpandPastePlaceholders(input);

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

    // Create the single assistant entry for this goal
    m_assistantEntryIndex = m_messagePanel->beginAssistant();
    m_streamingText.clear();

    m_agentState = AgentState::Thinking;
    m_statusBar->setAgentState(m_agentState);
    m_statusBar->setMessageCount(m_messagePanel->count());
    m_inputPanel->setEnabled(false);

    m_cmdSender.send(mpsc::SubmitGoal{expanded});

    if (m_screen) {
        m_screen->Post([]{});  // Wake FTXUI event loop
    }

    return 0;
}

int AgentTui::xHandleInterrupt() {
    if (m_agentState != AgentState::Idle) {
        m_cmdSender.send(mpsc::Cancel{});

        if (m_assistantEntryIndex >= 0) {
            m_messagePanel->appendOrUpdateAssistantText(m_assistantEntryIndex, m_streamingText);
            m_messagePanel->finalizeAssistant(m_assistantEntryIndex);
            m_assistantEntryIndex = -1;
            m_streamingText.clear();
        }

        MessageEntry entry;
        entry.role = MessageRole::System;
        entry.content = "Interrupted";
        m_messagePanel->append(entry);

        m_agentState = AgentState::Idle;
        m_statusBar->setAgentState(m_agentState);
        m_statusBar->setMessageCount(m_messagePanel->count());
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
    return 0;
}

// ============================================================================
// Commands
// ============================================================================

int AgentTui::xCmdSessions() {
    m_cmdSender.send(mpsc::ListSessions{20});
    m_statusBar->showStatus("Loading sessions...", 2);
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
    m_cmdSender.send(mpsc::Shutdown{});
    shutdown();
    return 0;
}

// ============================================================================
// Layout
// ============================================================================

void AgentTui::xBuildLayout() {
    auto mainContainer = xBuildMainContainer();

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

    auto renderer = ftxui::Renderer(wrapped, [wrapped]() {
        return wrapped->Render();
    });

    m_mainComponent = ftxui::CatchEvent(renderer, [this](ftxui::Event event) -> bool {
        auto& input = event.input();

        if (event == ftxui::Event::PageUp) {
            m_messagePanel->scrollUp(5);
            return true;
        }
        if (event == ftxui::Event::PageDown) {
            m_messagePanel->scrollDown(5);
            return true;
        }
        if (event == ftxui::Event::Home) {
            m_messagePanel->scrollToTop();
            return true;
        }
        if (event == ftxui::Event::End) {
            m_messagePanel->scrollToBottom();
            return true;
        }

        if (input == "\x1b[200~") {
            m_pasteActive = true;
            m_pasteBuffer.clear();
            return true;
        }

        if (input == "\x1b[201~") {
            m_pasteActive = false;
            xProcessPasteBuffer();
            return true;
        }

        if (m_pasteActive) {
            m_pasteBuffer += input;
            return true;
        }

        // Ctrl+C — interrupt even when input is disabled
        if (event == ftxui::Event::CtrlC) {
            xHandleInterrupt();
            return true;
        }

        if (event.is_mouse()) {
            auto& mouse = event.mouse();

            if (mouse.button == ftxui::Mouse::WheelUp) {
                m_messagePanel->scrollUp(3);
                return true;
            }
            if (mouse.button == ftxui::Mouse::WheelDown) {
                m_messagePanel->scrollDown(3);
                return true;
            }

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
