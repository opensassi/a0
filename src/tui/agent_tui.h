#pragma once

#include <string>
#include <memory>
#include <functional>
#include <cstdint>
#include <unordered_map>
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include <ctime>
#include "styles.h"
#include "../agent_interfaces.h"
#include "../persistence/persistence_store.h"
#include "../skills/skills.h"

namespace a0::tui {

class MessagePanel;
class InputPanel;
class StatusBar;
class DialogManager;
class SessionManager;
class MarkdownRenderer;

class AgentTui {
public:
    AgentTui(::AgentCore* core,
             ::a0::persistence::PersistenceStore* persistence,
             ::a0::skills::SkillManager* skills,
             std::function<bool()> b1Status = nullptr,
             bool noPermissions = false);

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

private:
    ::AgentCore* m_core;
    ::a0::persistence::PersistenceStore* m_persistence;
    ::a0::skills::SkillManager* m_skills;
    std::function<bool()> m_b1Status;
    bool m_noPermissions;

    std::unique_ptr<MessagePanel> m_messagePanel;
    std::unique_ptr<InputPanel> m_inputPanel;
    std::unique_ptr<StatusBar> m_statusBar;
    std::unique_ptr<DialogManager> m_dialogMgr;
    std::unique_ptr<SessionManager> m_sessionMgr;
    std::unique_ptr<MarkdownRenderer> m_markdown;

    std::string m_sessionUuid;
    int64_t m_sessionDbId = 0;
    AgentState m_agentState = AgentState::Idle;
    bool m_streaming = false;
    int m_streamingEntryIndex = -1;

    ftxui::ScreenInteractive* m_screen = nullptr;
    ftxui::Component m_mainComponent;

    // Mouse drag tracking for copy-on-select (mouse-up in CatchEvent)
    bool m_mouseDown = false;
    bool m_mouseMoved = false;

    // Bracketed paste handling
    bool m_pasteActive = false;
    std::string m_pasteBuffer;
    int m_pasteCounter = 0;
    std::unordered_map<int, std::string> m_pastedContents;

    std::string xExpandPastePlaceholders(const std::string& input);
    void xProcessPasteBuffer();

    void xBuildLayout();
    ftxui::Component xBuildMainContainer();

    int xHandleSubmit(const std::string& input);
    int xHandleInterrupt();
    int xHandleCommand(const std::string& cmd);

    int xProcessGoal(const std::string& goal);

    void xOnToken(const std::string& token);
    void xOnToolStart(const std::string& name, const nlohmann::json& params);
    void xOnToolEnd(const std::string& name, const std::string& output, bool success);
    void xOnComplete(const std::string& fullOutput);
    void xOnError(const std::string& error);
    void xOnInterrupted();

    int xCmdSessions();
    int xCmdHelp();
    int xCmdClear();
    int xCmdQuit();
    int xCmdExport();
};

} // namespace a0::tui
