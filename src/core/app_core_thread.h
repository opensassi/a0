#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "core/driven_core.h"
#include "llm/deepseek_provider.h"
#include "shared/mpsc.h"
#include "shared/resource_provider.h"
#include "skills/skills.h"

namespace a0 {

/// Thread-safe application core that owns DrivenCore and runs it in its own
/// poll()-based event loop.
///
/// Usage:
///   AppCoreThread app(apiKey, model, skillMgr, persistence);
///
///   auto [cmdSender, cmdRcvr] = mpsc::Channel<mpsc::Command>::create();
///   auto [evtSender, evtRcvr] = mpsc::Channel<mpsc::AppCoreEvent>::create();
///
///   app.start(std::move(cmdRcvr), std::move(evtSender));
///   cmdSender.send(SubmitGoal{"hello"});
///   // ... receive events from evtRcvr ...
///   app.stop();
///
class AppCoreThread {
public:
    AppCoreThread(const std::string& apiKey,
                  const std::string& model,
                  a0::skills::SkillManager* skillMgr,
                  a0::persistence::PersistenceStore* persistence = nullptr,
                  ResourceProvider* resourceProvider = nullptr,
                  int64_t tokenFlushSize = 256,
                  int64_t toolFlushSize = 4096,
                  int64_t outputPreviewSize = 4096,
                  const std::string& personaName = "",
                  const std::vector<std::string>& personaSkills = {},
                  const std::vector<std::string>& personaTools = {});
    ~AppCoreThread();

    AppCoreThread(const AppCoreThread&) = delete;
    AppCoreThread& operator=(const AppCoreThread&) = delete;

    /// Start the background thread.
    /// \param cmdRcvr     Command receiver — the thread reads commands from this.
    /// \param evtSender   Event sender — the thread sends events through this.
    /// \param wakeupFn    Optional callback called after events are sent, to wake the UI loop.
    /// Set mock URL for testing (DrivenProvider will connect here instead of real API).
    void setMockUrl(const std::string& url) { m_mockUrl = url; }

    void start(mpsc::Receiver<mpsc::Command> cmdRcvr,
               mpsc::Sender<mpsc::AppCoreEvent> evtSender,
               std::function<void()> wakeupFn = nullptr);

    /// Set the wakeup callback called after events are sent (for UI thread wakeup).
    /// Can be called before or after start(). Thread-safe.
    void setWakeupFn(std::function<void()> fn) { m_wakeupFn = std::move(fn); }

    /// Signal the thread to exit and join.
    void stop();

    /// True while the thread is running.
    bool running() const { return m_running.load(); }

private:
    std::string m_apiKey;
    std::string m_model;
    std::string m_mockUrl;
    std::string m_personaName;
    std::vector<std::string> m_personaSkills;
    std::vector<std::string> m_personaTools;
    a0::skills::SkillManager* m_skillMgr;
    a0::persistence::PersistenceStore* m_persistence;
    ResourceProvider* m_resourceProvider = nullptr;
    int64_t m_tokenFlushSize = 256;
    int64_t m_toolFlushSize = 4096;
    int64_t m_outputPreviewSize = 4096;

    mpsc::Receiver<mpsc::Command> m_cmdReceiver;
    mpsc::Sender<mpsc::AppCoreEvent> m_evtSender;
    std::function<void()> m_wakeupFn;

    int m_wakeupFd = -1;  // eventfd for waking the poll loop in stop()
    std::thread m_thread;
    std::atomic<bool> m_running{false};

    void xRun();
};

} // namespace a0
