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
#include "shared/mpsc.h"

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
    int m_assistantEntryIndex = -1;

    // New streaming state
    int64_t m_currentStreamId = 0;
    int m_currentRoundSeq = 0;
    bool m_hasActiveStream = false;

    // Resource cache for expanded tool outputs
    struct ResourceCacheEntry {
        std::string data;
        int64_t timestamp;
        size_t size;
    };
    std::unordered_map<int64_t, ResourceCacheEntry> m_resourceCache;
    int64_t m_resourceCacheMaxBytes = 64 * 1024 * 1024;
    int64_t m_resourceCacheBytes = 0;

    // Pending resource requests waiting for LoadResourceResult
    struct PendingResourceReq {
        std::function<void(std::string)> callback;
    };
    std::unordered_map<int64_t, std::vector<PendingResourceReq>> m_pendingResourceReqs;

    std::string xExpandPastePlaceholders(const std::string& input);
    void xProcessPasteBuffer();

    void xBuildLayout();
    ftxui::Component xBuildMainContainer();

    void xHandleCoreEvent(const ::a0::mpsc::AppCoreEvent& ev);

    int xHandleSubmit(const std::string& input);
    int xHandleInterrupt();
    int xHandleCommand(const std::string& cmd);

    // New event handlers
    void xOnLlmStart(int64_t streamId, int roundSeq);
    void xOnLlmChunk(int64_t streamId, int seq, const std::string& text, bool isFinal);
    void xOnLlmComplete(int64_t streamId, const std::string& finishReason);
    void xOnToolStart(int64_t invocationId, const std::string& toolCallId,
                      const std::string& toolName, const std::string& arguments);
    void xOnToolChunk(int64_t invocationId, int seq, const std::string& text,
                      const std::string& streamType);
    void xOnToolEnd(int64_t invocationId, int exitCode, int64_t totalBytes,
                    const std::string& outputPreview);
    void xOnComplete(int64_t sessionId, const std::string& summary);
    void xOnError(const std::string& source, int64_t contextId, const std::string& message);
    void xOnSessionReady(int64_t dbId, const std::string& uuid);
    void xOnSessionList(const std::vector<mpsc::SessionList::Entry>& sessions);
    void xOnSessionHistory(int64_t dbId, const std::string& uuid,
                           const std::vector<mpsc::SessionMessage>& messages);
    void xOnLoadResourceResult(int64_t id, const std::string& data);

    // Resource cache helpers
    void xEvictResourceCache();
    void xRequestResource(int64_t id, int64_t offset, int64_t limit,
                          std::function<void(std::string)> onData);

    int xCmdSessions();
    int xCmdHelp();
    int xCmdClear();
    int xCmdQuit();
};

} // namespace a0::tui
