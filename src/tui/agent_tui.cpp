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
#include <chrono>
#include <csignal>
#include <ctime>
#include <iostream>
#include <termios.h>
#include <thread>
#include <unistd.h>
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

    // FTXUI installs an empty SIGSEGV handler for terminal cleanup, but
    // returning from SIGSEGV re-executes the faulting instruction.  If the
    // fault is permanent (e.g. a shared_ptr use-after-free in the DOM tree)
    // this becomes an infinite SIGSEGV loop at 100% CPU.
    //
    // Override with a handler that restores the terminal, then re-raises
    // with SIG_DFL so the kernel produces a core dump.
    {
        struct sigaction sa = {};
        sa.sa_handler = [](int) {
            // Show cursor, exit alt screen, disable bracketed paste
            static const char restore[] = "\033[?25h\033[?1049l\033[?2004l";
            write(STDOUT_FILENO, restore, sizeof(restore) - 1);
            // Restore termios — ICANON+ECHO for line-editing, ISIG for Ctrl+C
            struct termios term;
            tcgetattr(STDIN_FILENO, &term);
            term.c_lflag |= ECHO | ICANON | ISIG | IEXTEN;
            term.c_iflag |= ICRNL | IXON;
            tcsetattr(STDIN_FILENO, TCSANOW, &term);
            signal(SIGSEGV, SIG_DFL);
            raise(SIGSEGV);
        };
        sa.sa_flags = SA_RESETHAND;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, nullptr);
    }

    while (!loop.HasQuitted()) {
        drainEvents();
        loop.RunOnce();
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
            // Invalidate the frame immediately so the next RunOnce renders with
            // the new state. Post(Event::Custom) is used rather than
            // RequestAnimationFrame() because the latter relies on the
            // EventListener thread's 20ms timeout, causing stale display.
            m_screen->Post(ftxui::Event::Custom);
        }
    }
}

void AgentTui::xHandleCoreEvent(const mpsc::AppCoreEvent& ev) {
    if (std::holds_alternative<mpsc::LlmStart>(ev)) {
        const auto& e = std::get<mpsc::LlmStart>(ev);
        xOnLlmStart(e.streamId, e.roundSeq);
    } else if (std::holds_alternative<mpsc::LlmChunk>(ev)) {
        const auto& e = std::get<mpsc::LlmChunk>(ev);
        xOnLlmChunk(e.streamId, e.seq, e.text, e.isFinal);
    } else if (std::holds_alternative<mpsc::LlmComplete>(ev)) {
        const auto& e = std::get<mpsc::LlmComplete>(ev);
        xOnLlmComplete(e.streamId, e.finishReason);
    } else if (std::holds_alternative<mpsc::ToolStart>(ev)) {
        const auto& e = std::get<mpsc::ToolStart>(ev);
        xOnToolStart(e.invocationId, e.toolCallId, e.toolName, e.arguments);
    } else if (std::holds_alternative<mpsc::ToolChunk>(ev)) {
        const auto& e = std::get<mpsc::ToolChunk>(ev);
        xOnToolChunk(e.invocationId, e.seq, e.text, e.streamType);
    } else if (std::holds_alternative<mpsc::ToolEnd>(ev)) {
        const auto& e = std::get<mpsc::ToolEnd>(ev);
        xOnToolEnd(e.invocationId, e.exitCode, e.totalBytes, e.outputPreview);
    } else if (std::holds_alternative<mpsc::Complete>(ev)) {
        const auto& e = std::get<mpsc::Complete>(ev);
        xOnComplete(e.sessionId, e.summary);
    } else if (std::holds_alternative<mpsc::Error>(ev)) {
        const auto& e = std::get<mpsc::Error>(ev);
        xOnError(e.source, e.contextId, e.message);
    } else if (std::holds_alternative<mpsc::SessionReady>(ev)) {
        const auto& e = std::get<mpsc::SessionReady>(ev);
        xOnSessionReady(e.dbId, e.uuid);
    } else if (std::holds_alternative<mpsc::SessionList>(ev)) {
        const auto& e = std::get<mpsc::SessionList>(ev);
        xOnSessionList(e.entries);
    } else if (std::holds_alternative<mpsc::SessionHistory>(ev)) {
        const auto& e = std::get<mpsc::SessionHistory>(ev);
        xOnSessionHistory(e.dbId, e.uuid, e.messages);
    } else if (std::holds_alternative<mpsc::LoadResourceResult>(ev)) {
        const auto& e = std::get<mpsc::LoadResourceResult>(ev);
        xOnLoadResourceResult(e.id, e.data);
    }
}

void AgentTui::submitInput(const std::string& input) {
    xHandleSubmit(input);
}

// ============================================================================
// New event handlers
// ============================================================================

void AgentTui::xOnLlmStart(int64_t streamId, int roundSeq) {
    m_hasActiveStream = true;
    m_currentStreamId = streamId;
    m_currentRoundSeq = roundSeq;
    m_streamingText.clear();

    if (m_assistantEntryIndex < 0) {
        MessageEntry entry;
        entry.role = MessageRole::Assistant;
        entry.content = "";
        m_assistantEntryIndex = m_messagePanel->append(entry);
    }
    m_agentState = AgentState::Thinking;
    m_statusBar->setAgentState(m_agentState);
}

void AgentTui::xOnLlmChunk(int64_t streamId, int seq, const std::string& text, bool isFinal) {
    m_streamingText += text;
    if (m_assistantEntryIndex >= 0) {
        m_messagePanel->appendOrUpdateAssistantText(m_assistantEntryIndex, m_streamingText);
    }
}

void AgentTui::xOnLlmComplete(int64_t streamId, const std::string& finishReason) {
    if (m_assistantEntryIndex >= 0 && !m_streamingText.empty()) {
        m_messagePanel->appendOrUpdateAssistantText(m_assistantEntryIndex, m_streamingText);
    }
    m_statusBar->setMessageCount(m_messagePanel->count());
    m_hasActiveStream = false;
}

void AgentTui::xOnToolStart(int64_t invocationId, const std::string& toolCallId,
                            const std::string& toolName, const std::string& arguments) {
    m_agentState = AgentState::Executing;
    m_statusBar->setAgentState(m_agentState);
    if (m_assistantEntryIndex >= 0) {
        m_messagePanel->finalizeAssistant(m_assistantEntryIndex);
        m_messagePanel->appendAssistantTool(m_assistantEntryIndex, toolName, ToolState::Running, arguments);
    }
    m_inputPanel->setEnabled(false);
    m_statusBar->setMessageCount(m_messagePanel->count());
}

void AgentTui::xOnToolChunk(int64_t invocationId, int seq, const std::string& text,
                            const std::string& streamType) {
    // Append to the last running tool entry's output instead of creating a new child
    if (m_assistantEntryIndex >= 0) {
        m_messagePanel->updateLastAssistantToolOutput(m_assistantEntryIndex, text);
    }
}

void AgentTui::xOnToolEnd(int64_t invocationId, int exitCode, int64_t totalBytes,
                          const std::string& outputPreview) {
    ToolState state = exitCode == 0 ? ToolState::Completed : ToolState::Failed;
    if (m_assistantEntryIndex >= 0) {
        m_messagePanel->updateLastAssistantTool(m_assistantEntryIndex, state, outputPreview);
    }
    if (m_agentState != AgentState::Thinking) {
        m_agentState = AgentState::Thinking;
        m_statusBar->setAgentState(m_agentState);
    }
}

void AgentTui::xOnComplete(int64_t sessionId, const std::string& summary) {
    if (m_assistantEntryIndex >= 0) {
        const std::string& text = m_streamingText.empty() ? summary : m_streamingText;
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
    m_hasActiveStream = false;
}

void AgentTui::xOnError(const std::string& source, int64_t contextId, const std::string& message) {
    if (m_assistantEntryIndex >= 0) {
        const std::string& text = m_streamingText.empty() ? message : m_streamingText;
        m_messagePanel->appendOrUpdateAssistantText(m_assistantEntryIndex, text);
        m_messagePanel->finalizeAssistant(m_assistantEntryIndex);
        m_assistantEntryIndex = -1;
        m_streamingText.clear();
    }

    MessageEntry errEntry;
    errEntry.role = MessageRole::Error;
    errEntry.content = message;
    errEntry.toolName = source;
    m_messagePanel->append(errEntry);

    m_agentState = AgentState::Error;
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
    m_sessionDbId = dbId;
    m_sessionUuid = uuid;
    m_statusBar->setSessionId(uuid);
    m_messagePanel->clear();
    for (const auto& msg : messages) {
        MessageEntry entry;
        if (msg.role == "user") entry.role = MessageRole::User;
        else if (msg.role == "assistant") entry.role = MessageRole::Assistant;
        else if (msg.role == "tool") entry.role = MessageRole::Tool;
        else entry.role = MessageRole::System;
        entry.content = msg.content;
        entry.toolName = msg.name;
        entry.timestamp = msg.createdAt;
        m_messagePanel->append(entry);
    }
    m_statusBar->setMessageCount(m_messagePanel->count());
}

void AgentTui::xOnLoadResourceResult(int64_t id, const std::string& data) {
    // Cache the result
    ResourceCacheEntry rce;
    rce.data = data;
    rce.timestamp = std::time(nullptr);
    rce.size = data.size();
    m_resourceCacheBytes += data.size();
    m_resourceCache[id] = std::move(rce);
    xEvictResourceCache();

    // Fulfill pending requests
    auto it = m_pendingResourceReqs.find(id);
    if (it != m_pendingResourceReqs.end()) {
        for (auto& req : it->second) {
            if (req.callback) req.callback(data);
        }
        m_pendingResourceReqs.erase(it);
    }
}

void AgentTui::xEvictResourceCache() {
    while (m_resourceCacheBytes > m_resourceCacheMaxBytes && !m_resourceCache.empty()) {
        // Find oldest entry
        auto oldest = m_resourceCache.begin();
        for (auto it = m_resourceCache.begin(); it != m_resourceCache.end(); ++it) {
            if (it->second.timestamp < oldest->second.timestamp)
                oldest = it;
        }
        m_resourceCacheBytes -= oldest->second.size;
        m_resourceCache.erase(oldest);
    }
}

void AgentTui::xRequestResource(int64_t id, int64_t offset, int64_t limit,
                                 std::function<void(std::string)> onData) {
    // Check cache first
    auto it = m_resourceCache.find(id);
    if (it != m_resourceCache.end()) {
        std::string data = it->second.data.substr(offset, limit < 0 ? std::string::npos : limit);
        if (onData) onData(data);
        return;
    }
    // Queue request and send LoadResource command
    m_pendingResourceReqs[id].push_back({std::move(onData)});
    m_cmdSender.send(mpsc::LoadResource{
        ResourceType::ToolOutput, id, offset, limit});
}

// ============================================================================
// UI Commands
// ============================================================================

int AgentTui::xHandleSubmit(const std::string& input) {
    if (input.empty()) return 0;

    if (input[0] == '/') {
        return xHandleCommand(input);
    }

    // Send the goal
    m_cmdSender.send(mpsc::SubmitGoal{input});

    // Add to local history
    m_inputPanel->addHistory(input);

    // Append user message
    MessageEntry entry;
    entry.role = MessageRole::User;
    entry.content = input;
    entry.timestamp = std::time(nullptr);
    m_messagePanel->append(entry);

    m_inputPanel->clear();
    m_statusBar->setMessageCount(m_messagePanel->count());
    return 0;
}

int AgentTui::xHandleInterrupt() {
    m_cmdSender.send(mpsc::Cancel{});
    if (m_assistantEntryIndex >= 0) {
        m_messagePanel->finalizeAssistant(m_assistantEntryIndex);
        m_assistantEntryIndex = -1;
    }
    m_streamingText.clear();
    m_agentState = AgentState::Idle;
    m_statusBar->setAgentState(m_agentState);
    m_inputPanel->setEnabled(true);
    m_statusBar->showStatus("Interrupted", 3);
    return 0;
}

int AgentTui::xHandleCommand(const std::string& cmd) {
    if (cmd == "/sessions" || cmd == "/s") {
        return xCmdSessions();
    } else if (cmd == "/help" || cmd == "/h") {
        return xCmdHelp();
    } else if (cmd == "/clear" || cmd == "/cls") {
        return xCmdClear();
    } else if (cmd == "/quit" || cmd == "/q" || cmd == ":q") {
        return xCmdQuit();
    }
    MessageEntry entry;
    entry.role = MessageRole::System;
    entry.content = "Unknown command: " + cmd + "  (try /help)";
    m_messagePanel->append(entry);
    return 0;
}

int AgentTui::xCmdSessions() {
    m_cmdSender.send(mpsc::ListSessions{20});
    m_statusBar->showStatus("Loading sessions...", 2);
    return 0;
}

int AgentTui::xCmdHelp() {
    auto helpText = ftxui::vbox({
        ftxui::text("a0 TUI Keybindings"),
        ftxui::separator(),
        ftxui::text("Enter        Submit input"),
        ftxui::text("Shift+Enter  Newline"),
        ftxui::text("Up/Down      History navigation"),
        ftxui::text("Ctrl+C       Cancel current"),
        ftxui::text("Ctrl+Q       Quit"),
        ftxui::text(""),
        ftxui::text("Commands:"),
        ftxui::text("  /help        This help"),
        ftxui::text("  /clear       Clear messages"),
        ftxui::text("  /sessions    List sessions"),
        ftxui::text("  /quit        Exit"),
    });
    auto dialog = ftxui::Container::Vertical({
        ftxui::Renderer([helpText] { return helpText | ftxui::border; })
    });
    m_dialogMgr->show(dialog, "Help");
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

void AgentTui::xProcessPasteBuffer() {
    if (m_pasteBuffer.empty()) return;
    int id = ++m_pasteCounter;
    size_t maxSize = 64 * 1024; // 64KB max paste
    std::string content = m_pasteBuffer.substr(0, maxSize);
    if (content.size() < m_pasteBuffer.size())
        content += "\n... (pasted data truncated at 64KB)";
    m_pastedContents[id] = content;
    m_pasteBuffer.clear();
    m_inputPanel->insertText("[ PASTED #" + std::to_string(id) + " ]");
}

} // namespace a0::tui
