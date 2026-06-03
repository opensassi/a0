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
        if (m_c2Fd >= 0) {
            pollFds.push_back({m_c2Fd, POLLIN, 0});
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
            }
        }
        ++idx;

        // Handle c2 messages (prompt_reply)
        if (m_c2Fd >= 0) {
            if (pollFds[idx].revents & (POLLIN | POLLHUP | POLLERR)) {
                if (pollFds[idx].revents & (POLLHUP | POLLERR)) {
                    ::close(m_c2Fd);
                    m_c2Fd = -1;
                } else {
                    ipc::UnixSocket c2Sock(m_c2Fd);
                    ipc::Message msg;
                    int recvRc = ipc::recvMessage(c2Sock, msg, 100);
                    if (recvRc == 0) {
                        if (msg.type == ipc::MessageType::PROMPT_REPLY) {
                            xHandlePromptReply(msg);
                        } else if (msg.type == ipc::MessageType::STREAM_INPUT) {
                            xHandleStreamInput(msg);
                        } else if (msg.type == ipc::MessageType::TERMINAL_OPEN) {
                            // c2 requests a terminal → forward to first connected a0
                            xHandleTerminalOpen(msg, -1);
                        }
                    } else {
                        ::close(m_c2Fd);
                        m_c2Fd = -1;
                    }
                    c2Sock.release();
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
                    ::close(fd);
                    m_agents.erase(fd);
                    continue;
                }

                ipc::UnixSocket peerSock(fd);
                ipc::Message msg;
                int recvRc = ipc::recvMessage(peerSock, msg, 100);
                if (recvRc == 0) {
                    if (msg.type == ipc::MessageType::REGISTER) {
                        xHandleRegister(msg, fd);
                    } else if (msg.type == ipc::MessageType::HEARTBEAT) {
                        xHandleHeartbeat(msg, fd);
                    } else if (msg.type == ipc::MessageType::USER_PROMPT) {
                        xHandleUserPrompt(msg, fd);
                    } else if (msg.type == ipc::MessageType::STREAM_DATA) {
                        TRACE_LOG("b1: relay stream_data streamId=" << msg.streamId
                                  << " fd=" << fd);
                        // Track stream owner and forward to c2
                        m_streamOwners[msg.streamId] = fd;
                        xSendToC2(msg);
                    } else if (msg.type == ipc::MessageType::STREAM_END) {
                        m_streamOwners.erase(msg.streamId);
                        xSendToC2(msg);
                    } else if (msg.type == ipc::MessageType::TERMINAL_READY) {
                        // a0 opened a terminal → notify c2
                        xSendToC2(msg);
                    }
                } else {
                    ::close(fd);
                    m_agents.erase(fd);
                }
                peerSock.release();
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
        if (elapsed >= 5 && m_c2Fd >= 0) {
            xPushSnapshotToC2();
            m_lastC2Push = now;
        }
    }

    return 0;
}

void Supervisor::shutdown() {
    m_running = false;
    m_listenSocket.close();
    if (m_c2Fd >= 0) {
        ::close(m_c2Fd);
        m_c2Fd = -1;
    }
    for (auto& pair : m_agents) {
        ::close(pair.first);
    }
    m_agents.clear();
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
    // Forward to c2
    if (m_c2Fd < 0) return -1;

    auto it = m_agents.find(peerFd);
    int a0Pid = (it != m_agents.end()) ? it->second.pid : 0;

    ipc::Message fwd;
    fwd.type = ipc::MessageType::USER_PROMPT;
    fwd.pid = a0Pid;
    fwd.sessionUuid = msg.sessionUuid;
    fwd.toolCallId = msg.toolCallId;
    fwd.prompt = msg.prompt;

    ipc::UnixSocket c2Sock(m_c2Fd);
    int rc = ipc::sendMessage(c2Sock, fwd);
    c2Sock.release();
    return rc;
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
    if (m_c2Fd < 0) return -1;

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

    ipc::UnixSocket c2Sock(m_c2Fd);
    int rc = ipc::sendMessage(c2Sock, update);
    if (rc < 0) {
        ::close(m_c2Fd);
        m_c2Fd = -1;
        return -1;
    }
    c2Sock.release();
    return 0;
}

int Supervisor::xSendToC2(const ipc::Message& msg) {
    if (m_c2Fd < 0) return -1;

    ipc::UnixSocket c2Sock(m_c2Fd);
    int rc = ipc::sendMessage(c2Sock, msg);
    if (rc < 0) {
        ::close(m_c2Fd);
        m_c2Fd = -1;
        return -1;
    }
    c2Sock.release();
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
        // Child: launch a0 terminal in the requested directory
        setsid();
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
            m_c2Fd = c2Sock.release();
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
        execlp(c2Path.c_str(), "c2", "--socket", m_c2SocketPath.c_str(), nullptr);
        _exit(127);
    } else if (pid > 0) {
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
                    m_c2Fd = retrySock.release();
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
