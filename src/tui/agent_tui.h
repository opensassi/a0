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
#include "../llm_provider.h"
#include "../driven_core.h"
#include "../persistence/persistence_store.h"

namespace a0::tui {

class MessagePanel;
class InputPanel;
class StatusBar;
class DialogManager;
class SessionManager;
class MarkdownRenderer;

class AgentTui {
public:
    AgentTui(a0::LlmProvider* provider,
             a0::skills::SkillManager* skillMgr,
             a0::persistence::PersistenceStore* persistence,
             int64_t agentId = 0,
             std::function<bool()> b1Status = nullptr,
             int64_t sessionDbId = 0,
             const std::string& sessionUuid = "");

    virtual ~AgentTui();

    int run(bool testMode = false);
    void shutdown();

    ftxui::Component component() const { return m_mainComponent; }

    void setScreen(ftxui::ScreenInteractive* screen);
    void clearScreen();
    ftxui::ScreenInteractive* screenPtr() const { return m_screen; }

    int resumeSession(const std::string& uuid);
    std::string currentSessionId() const;

    void submitInput(const std::string& input);

    void setMockUrl(const std::string& url) { m_provider->setMockUrl(url); }

private:
    a0::persistence::PersistenceStore* m_persistence;
    int64_t m_agentId = 0;
    std::function<bool()> m_b1Status;
    a0::LlmProvider* m_provider;
    std::unique_ptr<a0::DrivenCore> m_drivenCore;

    std::unique_ptr<MessagePanel> m_messagePanel;
    std::unique_ptr<InputPanel> m_inputPanel;
    std::unique_ptr<StatusBar> m_statusBar;
    std::unique_ptr<DialogManager> m_dialogMgr;
    std::unique_ptr<SessionManager> m_sessionMgr;
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

    std::string m_streamingText;
    int m_streamingEntryIndex = -1;

    std::string xExpandPastePlaceholders(const std::string& input);
    void xProcessPasteBuffer();
    void xTickCore();

    void xBuildLayout();
    ftxui::Component xBuildMainContainer();

    int xHandleSubmit(const std::string& input);
    int xHandleInterrupt();
    int xHandleCommand(const std::string& cmd);

    void xHandleEvent(const a0::mpsc::AppCoreEvent& ev);
    void xOnToken(const std::string& token);
    void xOnToolStart(const std::string& name, const std::string& arguments);
    void xOnToolEnd(const std::string& name, const std::string& output, bool success);
    void xOnComplete(const std::string& fullOutput);
    void xOnError(const std::string& error);

    int xCmdSessions();
    int xCmdHelp();
    int xCmdClear();
    int xCmdQuit();
    int xCmdExport();
};

} // namespace a0::tui
