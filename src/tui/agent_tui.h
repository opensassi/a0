#pragma once

#include <string>
#include <memory>
#include <functional>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include <ctime>
#include "styles.h"
#include "../mpsc.h"

namespace a0::tui {

class MessagePanel;
class InputPanel;
class StatusBar;
class DialogManager;
class MarkdownRenderer;

class AgentTui {
public:
    AgentTui(mpsc::Sender<mpsc::Command> cmdSender,
             mpsc::Receiver<mpsc::AppCoreEvent> evtReceiver,
             std::function<bool()> b1Status = nullptr,
             bool testMode = false);

    virtual ~AgentTui();

    int run();

    void shutdown();

    ftxui::Component component() const { return m_mainComponent; }

    void setScreen(ftxui::ScreenInteractive* screen);
    void clearScreen();
    ftxui::ScreenInteractive* screenPtr() const { return m_screen; }

    void submitInput(const std::string& input);
    void drainEvents();

    mpsc::Sender<mpsc::Command>& cmdSender() { return m_cmdSender; }

private:
    mpsc::Sender<mpsc::Command> m_cmdSender;
    mpsc::Receiver<mpsc::AppCoreEvent> m_evtReceiver;
    std::function<bool()> m_b1Status;

    std::unique_ptr<MessagePanel> m_messagePanel;
    std::unique_ptr<InputPanel> m_inputPanel;
    std::unique_ptr<StatusBar> m_statusBar;
    std::unique_ptr<DialogManager> m_dialogMgr;
    std::unique_ptr<MarkdownRenderer> m_markdown;

    std::string m_sessionUuid;
    int64_t m_sessionDbId = 0;
    AgentState m_agentState = AgentState::Idle;

    ftxui::ScreenInteractive* m_screen = nullptr;
    ftxui::Component m_mainComponent;

    bool m_mouseDown = false;
    bool m_mouseMoved = false;

    bool m_pasteActive = false;
    std::string m_pasteBuffer;
    int m_pasteCounter = 0;
    std::unordered_map<int, std::string> m_pastedContents;

    bool m_testMode = false;
    std::string m_streamingText;
    int m_streamingEntryIndex = -1;

    std::string xExpandPastePlaceholders(const std::string& input);
    void xProcessPasteBuffer();

    void xBuildLayout();
    ftxui::Component xBuildMainContainer();

    void xHandleCoreEvent(const ::a0::mpsc::AppCoreEvent& ev);

    int xHandleSubmit(const std::string& input);
    int xHandleInterrupt();
    int xHandleCommand(const std::string& cmd);

    void xOnToken(const std::string& token);
    void xOnToolStart(const std::string& name, const std::string& arguments);
    void xOnToolEnd(const std::string& name, const std::string& output, bool success);
    void xOnComplete(const std::string& fullOutput);
    void xOnError(const std::string& error);
    void xOnSessionReady(int64_t dbId, const std::string& uuid);
    void xOnSessionList(const std::vector<mpsc::SessionList::Entry>& sessions);
    void xOnSessionHistory(int64_t dbId, const std::string& uuid,
                           const std::vector<mpsc::SessionMessage>& messages);

    int xCmdSessions();
    int xCmdHelp();
    int xCmdClear();
    int xCmdQuit();
};

} // namespace a0::tui
