#include "app_core_thread.h"
#include "persistence/persistence_store.h"
#include "trace.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <unistd.h>

namespace a0 {

AppCoreThread::AppCoreThread(const std::string& apiKey,
                             const std::string& model,
                             a0::skills::SkillManager* skillMgr,
                             a0::persistence::PersistenceStore* persistence)
    : m_apiKey(apiKey)
    , m_model(model)
    , m_skillMgr(skillMgr)
    , m_persistence(persistence)
{
    m_wakeupFd = eventfd(0, EFD_NONBLOCK);
}

AppCoreThread::~AppCoreThread() {
    stop();
    if (m_wakeupFd >= 0) {
        close(m_wakeupFd);
        m_wakeupFd = -1;
    }
}

void AppCoreThread::start(mpsc::Receiver<mpsc::Command> cmdRcvr,
                           mpsc::Sender<mpsc::AppCoreEvent> evtSender,
                           std::function<void()> wakeupFn) {
    if (m_running.exchange(true)) return;

    m_cmdReceiver = std::move(cmdRcvr);
    m_evtSender = std::move(evtSender);
    m_wakeupFn = std::move(wakeupFn);

    m_thread = std::thread(&AppCoreThread::xRun, this);
}

void AppCoreThread::stop() {
    if (!m_running.exchange(false)) return;

    // Wake up the poll loop
    if (m_wakeupFd >= 0) {
        uint64_t one = 1;
        write(m_wakeupFd, &one, sizeof(one));
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void AppCoreThread::xRun() {
    TRACE_LOG("AppCoreThread started");

    DeepSeekProvider provider(m_apiKey, m_model);
    if (!m_mockUrl.empty()) {
        provider.setMockUrl(m_mockUrl);
    }
    DrivenCore core(&provider, m_skillMgr, m_persistence);

    // Block SIGCHLD so we handle it via waitpid(WNOHANG) in the poll loop
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGCHLD);
    pthread_sigmask(SIG_BLOCK, &sigmask, nullptr);

    sigset_t waitset;
    sigemptyset(&waitset);

    while (m_running.load()) {
        struct pollfd fds[2];
        nfds_t nfds = 0;

        // Wakeup fd (written by stop())
        if (m_wakeupFd >= 0) {
            fds[nfds].fd = m_wakeupFd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            nfds++;
        }

        // Command queue fd
        int cmdFd = m_cmdReceiver.poll_fd();
        if (cmdFd >= 0) {
            fds[nfds].fd = cmdFd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            nfds++;
        }

        // Compute poll timeout
        int timeoutMs = 100; // default 100ms idle poll

        if (provider.active()) {
            int provTimeout = provider.timeoutMs();
            if (provTimeout >= 0 && provTimeout < timeoutMs) {
                timeoutMs = provTimeout;
            }
        }

        struct timespec ts;
        ts.tv_sec = timeoutMs / 1000;
        ts.tv_nsec = (timeoutMs % 1000) * 1000000;
        int rc = ppoll(fds, nfds, &ts, &waitset);

        if (rc < 0) {
            if (errno == EINTR) continue;
            TRACE_LOG("AppCoreThread: poll error: " << strerror(errno));
            break;
        }

        // Clear wakeup fd
        if (m_wakeupFd >= 0) {
            uint64_t val;
            read(m_wakeupFd, &val, sizeof(val));
        }

        // Handle dead children (tools that exited)
        while (waitpid(-1, nullptr, WNOHANG) > 0) {}

        // Check if stop was requested
        if (!m_running.load()) break;

        // Drain commands
        auto commands = m_cmdReceiver.drain();
        for (auto& cmd : commands) {
            if (std::holds_alternative<mpsc::SubmitGoal>(cmd)) {
                if (core.idle()) {
                    m_evtSender.send(mpsc::LlmToken{"[thinking]\n"});
                    core.submitGoal(std::get<mpsc::SubmitGoal>(cmd).goal);
                }
            } else if (std::holds_alternative<mpsc::Cancel>(cmd)) {
                core.cancel();
            } else if (std::holds_alternative<mpsc::Shutdown>(cmd)) {
                m_running.store(false);
            }
        }

        // Tick the core
        if (!core.idle()) {
            auto events = core.tick();
            for (auto& ev : events) {
                m_evtSender.send(std::move(ev));
            }
            // Wake up the UI after sending events
            if (!events.empty() && m_wakeupFn) {
                m_wakeupFn();
            }
        }
    }

    TRACE_LOG("AppCoreThread stopped");
}

} // namespace a0
