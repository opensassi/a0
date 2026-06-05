#include "supervisor.h"
#include "command_runner.h"
#include "ipc_protocol.h"
#include "trace.h"

std::string g_b1LogFile;
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <fstream>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace a0::b1 {

Supervisor::Supervisor(const std::string& socketPath,
                       const std::string& pidPath,
                       const std::string& c2SocketPath,
                       const std::string& workdir)
    : m_socketPath(socketPath)
    , m_pidPath(pidPath)
    , m_c2SocketPath(c2SocketPath)
    , m_workdir(workdir)
{
}

Supervisor::~Supervisor() {
    shutdown();
}

int Supervisor::init() {
    int rc = xCheckExistingInstance();
    if (rc < 0) return -3;

    xCleanupStaleSocket();

    rc = xWritePidFile();
    if (rc < 0) return -2;

    rc = m_listenSocket.bindAndListen(m_socketPath);
    if (rc < 0) return -1;

    m_listenFd = m_listenSocket.fd();
    m_running = true;
    m_lastC2Push = std::chrono::steady_clock::now();

    xLaunchC2IfNeeded();

    return 0;
}

int Supervisor::run() {
    if (!m_running) return -1;

    std::vector<struct pollfd> pollFds;

    while (m_running) {
        pollFds.clear();
        pollFds.push_back({m_listenFd, POLLIN, 0});
        if (m_c2Socket.fd() >= 0) {
            pollFds.push_back({m_c2Socket.fd(), POLLIN, 0});
        }
        for (auto& pair : m_agents) {
            pollFds.push_back({pair.first, POLLIN, 0});
        }

        int rc = ::poll(pollFds.data(), pollFds.size(), 1000);
        if (rc < 0) {
            if (errno == EINTR) continue;
            continue;
        }

        int idx = 0;

        // Accept new connections
        if (pollFds[idx].revents & POLLIN) {
            ipc::UnixSocket client;
            if (m_listenSocket.accept(client) == 0 && client.isOpen()) {
                int clientFd = client.release();
                m_agents[clientFd] = AgentRecord{};
                m_agents[clientFd].pid = 0;
                m_agents[clientFd].fd = clientFd;
                m_agents[clientFd].connectedAt = std::chrono::steady_clock::now();
                m_agentSockets.emplace(clientFd, clientFd);
            }
        }
        ++idx;

        // Handle c2 messages (prompt_reply)
        if (m_c2Socket.fd() >= 0) {
            if (pollFds[idx].revents & (POLLIN | POLLHUP | POLLERR)) {
                if (pollFds[idx].revents & (POLLHUP | POLLERR)) {
                    m_c2Socket.close();
                } else {
                    ipc::Message msg;
                    int r = m_c2Socket.recv(msg, 100);
                    if (r == ipc::RECV_OK) {
                        if (msg.type == ipc::MessageType::PROMPT_REPLY) {
                            xHandlePromptReply(msg);
                        } else if (msg.type == ipc::MessageType::STREAM_INPUT) {
                            xHandleStreamInput(msg);
                        } else if (msg.type == ipc::MessageType::TERMINAL_OPEN) {
                            xHandleTerminalOpen(msg, -1);
                        }
                    } else if (r < 0) {
                        m_c2Socket.close();
                    }
                }
            }
            ++idx;
        }

        // Handle agent messages
        size_t agentStart = idx;
        for (size_t i = agentStart; i < pollFds.size(); ++i) {
            int fd = pollFds[i].fd;
            if (pollFds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                if (pollFds[i].revents & (POLLHUP | POLLERR)) {
                    auto it = m_agents.find(fd);
                    if (it != m_agents.end()) {
                        TRACE_LOG("b1: agent disconnected pid=" << it->second.pid
                                  << " session=" << it->second.sessionUuid);
                    }
                    m_agentSockets.erase(fd);
                    ::close(fd);
                    m_agents.erase(fd);
                    continue;
                }

                auto sockIt = m_agentSockets.find(fd);
                if (sockIt == m_agentSockets.end()) continue;
                ipc::Message msg;
                int r = sockIt->second.recv(msg, 100);
                if (r == ipc::RECV_OK) {
                    if (msg.type == ipc::MessageType::REGISTER) {
                        xHandleRegister(msg, fd);
                    } else if (msg.type == ipc::MessageType::HEARTBEAT) {
                        xHandleHeartbeat(msg, fd);
                    } else if (msg.type == ipc::MessageType::USER_PROMPT) {
                        xHandleUserPrompt(msg, fd);
                    } else if (msg.type == ipc::MessageType::STREAM_DATA) {
                        TRACE_LOG("b1: relay stream_data streamId=" << msg.streamId
                                  << " fd=" << fd);
                        m_streamOwners[msg.streamId] = fd;
                        xSendToC2(msg);
                    } else if (msg.type == ipc::MessageType::STREAM_END) {
                        m_streamOwners.erase(msg.streamId);
                        xSendToC2(msg);
                    } else if (msg.type == ipc::MessageType::TERMINAL_READY) {
                        xSendToC2(msg);
                    }
                } else if (r < 0) {
                    m_agentSockets.erase(fd);
                    ::close(fd);
                    m_agents.erase(fd);
                }
            }
        }

        // Detect crashes via waitpid
        for (auto& pair : m_agents) {
            int pid = pair.second.pid;
            if (pid > 0) {
                int status;
                int wpid = waitpid(pid, &status, WNOHANG);
                if (wpid == pid) {
                    pair.second.state = AgentState::CRASHED;
                }
            }
        }

        // Periodic c2 push
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - m_lastC2Push).count();
        if (elapsed >= 5 && m_c2Socket.fd() >= 0) {
            xPushSnapshotToC2();
            m_lastC2Push = now;
        }
    }

    return 0;
}

void Supervisor::shutdown() {
    m_running = false;
    m_listenSocket.close();
    m_c2Socket.close();
    for (auto& pair : m_agentSockets) {
        pair.second.close();
        ::close(pair.first);
    }
    m_agentSockets.clear();
    m_agents.clear();

    // Kill c2 child if we launched it
    if (m_c2ChildPid > 0) {
        ::kill(m_c2ChildPid, SIGTERM);
        for (int i = 0; i < 20; ++i) {
            if (::kill(m_c2ChildPid, 0) != 0) break;
            usleep(100000);
        }
        if (::kill(m_c2ChildPid, 0) == 0) {
            ::kill(m_c2ChildPid, SIGKILL);
        }
        int status;
        ::waitpid(m_c2ChildPid, &status, 0);
        m_c2ChildPid = -1;
    }

    ipc::UnixSocket::unlinkPath(m_socketPath);
}

size_t Supervisor::agentCount() const {
    return m_agents.size();
}

int Supervisor::xHandleRegister(const ipc::Message& msg, int peerFd) {
    auto it = m_agents.find(peerFd);
    if (it == m_agents.end()) return -1;

    TRACE_LOG("b1: agent register pid=" << msg.pid
              << " session=" << msg.sessionUuid);
    it->second.pid = msg.pid;
    it->second.sessionUuid = msg.sessionUuid;
    it->second.fd = peerFd;
    it->second.state = AgentState::RUNNING;
    it->second.lastHeartbeat = std::chrono::steady_clock::now();

    ipc::Message ack;
    ack.type = ipc::MessageType::ACK;
    ack.status = "registered";
    ipc::UnixSocket peerSock(peerFd);
    ipc::sendMessage(peerSock, ack);
    peerSock.release();
    return 0;
}

int Supervisor::xHandleHeartbeat(const ipc::Message& msg, int peerPid) {
    (void)msg;
    auto it = m_agents.find(peerPid);
    if (it == m_agents.end()) return -1;
    it->second.lastHeartbeat = std::chrono::steady_clock::now();
    return 0;
}

int Supervisor::xHandleUserPrompt(const ipc::Message& msg, int peerFd) {
    if (m_c2Socket.fd() < 0) return -1;

    auto it = m_agents.find(peerFd);
    int a0Pid = (it != m_agents.end()) ? it->second.pid : 0;

    ipc::Message fwd;
    fwd.type = ipc::MessageType::USER_PROMPT;
    fwd.pid = a0Pid;
    fwd.sessionUuid = msg.sessionUuid;
    fwd.toolCallId = msg.toolCallId;
    fwd.prompt = msg.prompt;

    return m_c2Socket.send(fwd);
}

int Supervisor::xHandlePromptReply(const ipc::Message& msg) {
    // Forward to the a0 agent with matching session
    int agentFd = xFindAgentFdBySession(msg.sessionUuid);
    if (agentFd < 0) {
        std::cerr << "b1: prompt_reply for unknown session " << msg.sessionUuid << "\n";
        return -1;
    }

    ipc::Message fwd;
    fwd.type = ipc::MessageType::PROMPT_REPLY;
    fwd.sessionUuid = msg.sessionUuid;
    fwd.toolCallId = msg.toolCallId;

    ipc::UnixSocket agentSock(agentFd);
    int rc = ipc::sendMessage(agentSock, fwd);
    agentSock.release();

    if (rc < 0) {
        ::close(agentFd);
        m_agents.erase(agentFd);
    }
    return rc;
}

int Supervisor::xDetectCrashes() {
    int count = 0;
    for (auto& pair : m_agents) {
        int pid = pair.second.pid;
        if (pid > 0) {
            int status;
            int rc = waitpid(pid, &status, WNOHANG);
            if (rc == pid) {
                pair.second.state = AgentState::CRASHED;
                ++count;
            }
        }
    }
    return count;
}

int Supervisor::xPushSnapshotToC2() {
    if (m_c2Socket.fd() < 0) return -1;

    ipc::Message update;
    update.type = ipc::MessageType::UPDATE;
    update.pid = getpid();

    nlohmann::json agents = nlohmann::json::array();
    for (const auto& pair : m_agents) {
        nlohmann::json agent;
        agent["pid"] = pair.second.pid;
        agent["session"] = pair.second.sessionUuid;
        switch (pair.second.state) {
            case AgentState::RUNNING: agent["state"] = "running"; break;
            case AgentState::CRASHED: agent["state"] = "crashed"; break;
            case AgentState::STOPPED: agent["state"] = "stopped"; break;
        }
        agents.push_back(agent);
    }
    update.agents = agents;

    int rc = m_c2Socket.send(update);
    if (rc < 0) {
        m_c2Socket.close();
        return -1;
    }
    return 0;
}

int Supervisor::xSendToC2(const ipc::Message& msg) {
    if (m_c2Socket.fd() < 0) return -1;

    int rc = m_c2Socket.send(msg);
    if (rc < 0) {
        m_c2Socket.close();
        return -1;
    }
    return 0;
}

int Supervisor::xSendToAgent(int agentFd, const ipc::Message& msg) {
    ipc::UnixSocket sock(agentFd);
    int rc = ipc::sendMessage(sock, msg);
    sock.release();
    return rc;
}

int Supervisor::xHandleStreamData(const ipc::Message& msg, int peerFd) {
    m_streamOwners[msg.streamId] = peerFd;
    return xSendToC2(msg);
}

int Supervisor::xHandleStreamEnd(const ipc::Message& msg, int peerFd) {
    (void)peerFd;
    m_streamOwners.erase(msg.streamId);
    return xSendToC2(msg);
}

int Supervisor::xHandleStreamInput(const ipc::Message& msg) {
    auto it = m_streamOwners.find(msg.streamId);
    if (it == m_streamOwners.end()) return -1;
    int agentFd = it->second;
    return xSendToAgent(agentFd, msg);
}

int Supervisor::xHandleTerminalOpen(const ipc::Message& msg, int peerFd) {
    (void)peerFd;
    TRACE_LOG("b1: terminal_open cwd=" << msg.cwd);
    // Resolve cwd to an absolute path so the terminal a0 finds the right b1 socket
    // c2 sends an absolute cwd, but resolve it here too for safety
    std::string cwd = m_workdir;
    if (!msg.cwd.empty()) {
        char resolved[4096];
        if (realpath(msg.cwd.c_str(), resolved)) {
            cwd = resolved;
        }
    } else {
        char resolved[4096];
        if (realpath(cwd.c_str(), resolved)) {
            cwd = resolved;
        }
    }

    // Resolve a0 binary path from own binary location
    std::string a0Path;
    {
        char buf[4096];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            std::string self(buf);
            auto slash = self.rfind('/');
            if (slash != std::string::npos)
                a0Path = self.substr(0, slash) + "/a0";
        }
    }
    if (a0Path.empty()) a0Path = "a0";

    // Derive child log file path
    std::string a0Log;
    if (!g_b1LogFile.empty()) {
        auto dot = g_b1LogFile.rfind('.');
        a0Log = (dot != std::string::npos)
            ? g_b1LogFile.substr(0, dot) + "-a0" + g_b1LogFile.substr(dot)
            : g_b1LogFile + "-a0";
    }

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        // Redirect child stdout/stderr
        {
            const char* logDir = getenv("A0_LOG_DIR");
            const char* sessionId = getenv("A0_SESSION_ID");
            std::string termLog;
            if (logDir && sessionId)
                termLog = std::string(logDir) + "/a0-" + sessionId + "-"
                        + std::to_string(getpid()) + "-term.log";
            int fd = -1;
            if (!termLog.empty())
                fd = ::open(termLog.c_str(), O_WRONLY|O_CREAT|O_APPEND, 0644);
            if (fd < 0) fd = ::open("/dev/null", O_WRONLY);
            if (fd >= 0) {
                ::dup2(fd, STDOUT_FILENO);
                ::dup2(fd, STDERR_FILENO);
                if (fd > 2) ::close(fd);
            }
        }
        // Child: launch a0 terminal in the requested directory
        std::string a0Dir = cwd + "/.a0";
        if (!msg.terminalId.empty()) {
                    if (!a0Log.empty()) {
                execlp(a0Path.c_str(), "a0", "--a0-dir", a0Dir.c_str(),
                       "--log-file", a0Log.c_str(),
                       "terminal", "--cwd", cwd.c_str(),
                       "--terminal-id", msg.terminalId.c_str(), nullptr);
            } else {
                execlp(a0Path.c_str(), "a0", "--a0-dir", a0Dir.c_str(),
                       "terminal", "--cwd", cwd.c_str(),
                       "--terminal-id", msg.terminalId.c_str(), nullptr);
            }
        } else {
            if (!a0Log.empty()) {
                execlp(a0Path.c_str(), "a0", "--a0-dir", a0Dir.c_str(),
                       "--log-file", a0Log.c_str(),
                       "terminal", "--cwd", cwd.c_str(), nullptr);
            } else {
                execlp(a0Path.c_str(), "a0", "--a0-dir", a0Dir.c_str(),
                       "terminal", "--cwd", cwd.c_str(), nullptr);
            }
        }
        _exit(127);
    }
    return (pid > 0) ? 0 : -1;
}

int Supervisor::xFindAgentFdByStream(int64_t streamId) const {
    auto it = m_streamOwners.find(streamId);
    if (it != m_streamOwners.end()) return it->second;
    return -1;
}

int Supervisor::xFindAgentFdBySession(const std::string& sessionUuid) const {
    for (const auto& pair : m_agents) {
        if (pair.second.sessionUuid == sessionUuid) {
            return pair.first;
        }
    }
    return -1;
}

int Supervisor::xLaunchC2IfNeeded() {
    ipc::UnixSocket c2Sock;
    int rc = c2Sock.connect(m_c2SocketPath, 100);
    if (rc == 0) {
        ipc::Message reg;
        reg.type = ipc::MessageType::REGISTER;
        reg.pid = getpid();
        reg.workdir = m_workdir;
        char hostname[256] = {};
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            reg.hostname = hostname;
        }
        rc = ipc::sendMessage(c2Sock, reg);
        if (rc == 0) {
            m_c2Socket = ipc::BufferedSocket(c2Sock.release());
            return 0;
        }
    }

    // Resolve c2 path from own binary location
    std::string c2Path;
    {
        char buf[4096];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            std::string self(buf);
            auto slash = self.rfind('/');
            if (slash != std::string::npos)
                c2Path = self.substr(0, slash) + "/c2";
        }
    }
    if (c2Path.empty()) c2Path = "c2";

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        // Redirect child stdout/stderr
        const char* logDir = getenv("A0_LOG_DIR");
        const char* sessionId = getenv("A0_SESSION_ID");
        std::string c2Log;
        if (logDir && sessionId)
            c2Log = std::string(logDir) + "/a0-" + sessionId + "-"
                  + std::to_string(getpid()) + "-c2.log";
        {
            int fd = -1;
            if (!c2Log.empty())
                fd = ::open(c2Log.c_str(), O_WRONLY|O_CREAT|O_APPEND, 0644);
            if (fd < 0) fd = ::open("/dev/null", O_WRONLY);
            if (fd >= 0) {
                ::dup2(fd, STDOUT_FILENO);
                ::dup2(fd, STDERR_FILENO);
                if (fd > 2) ::close(fd);
            }
        }
        execlp(c2Path.c_str(), "c2", "--socket", m_c2SocketPath.c_str(), nullptr);
        _exit(127);
    } else if (pid > 0) {
        m_c2ChildPid = pid;
        for (int i = 0; i < 30; ++i) {
            ipc::UnixSocket retrySock;
            rc = retrySock.connect(m_c2SocketPath, 100);
            if (rc == 0) {
                ipc::Message reg;
                reg.type = ipc::MessageType::REGISTER;
                reg.pid = getpid();
                reg.workdir = m_workdir;
                rc = ipc::sendMessage(retrySock, reg);
                if (rc == 0) {
                    m_c2Socket = ipc::BufferedSocket(retrySock.release());
                    return 0;
                }
            }
            usleep(100000);
        }
    }
    return -1;
}

int Supervisor::xCheckExistingInstance() {
    std::ifstream f(m_pidPath);
    if (!f) return 0;  // no PID file → fresh start

    int existingPid = 0;
    f >> existingPid;
    if (existingPid <= 0) return 0;

    // If the process is alive, another b1 is already running for this workdir
    if (::kill(static_cast<pid_t>(existingPid), 0) == 0) {
        std::cerr << "b1: another instance already running (pid="
                  << existingPid << ") for workdir=" << m_workdir << "\n";
        return -1;
    }

    // Stale PID file — will be overwritten by xWritePidFile
    return 0;
}

void Supervisor::xCleanupStaleSocket() {
    ipc::UnixSocket::unlinkPath(m_socketPath);
}

int Supervisor::xWritePidFile() {
    std::ofstream f(m_pidPath);
    if (!f) return -1;
    f << getpid() << std::endl;
    return 0;
}

} // namespace a0::b1
