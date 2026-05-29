#include "supervisor.h"
#include "command_runner.h"
#include "ipc_protocol.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
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
    xCleanupStaleSocket();

    int rc = xWritePidFile();
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
        // Rebuild poll list: listen socket + all peer fds
        pollFds.clear();
        pollFds.push_back({m_listenFd, POLLIN, 0});
        for (auto& pair : m_agents) {
            pollFds.push_back({pair.first, POLLIN, 0});
        }

        int rc = ::poll(pollFds.data(), pollFds.size(), 1000);
        if (rc < 0) {
            if (errno == EINTR) continue;
            continue;
        }

        // Accept new connections
        if (pollFds[0].revents & POLLIN) {
            ipc::UnixSocket client;
            if (m_listenSocket.accept(client) == 0 && client.isOpen()) {
                int clientFd = client.release();
                m_agents[clientFd] = AgentRecord{};
                m_agents[clientFd].pid = 0;
                m_agents[clientFd].connectedAt = std::chrono::steady_clock::now();
            }
        }

        // Handle peer messages
        for (size_t i = 1; i < pollFds.size(); ++i) {
            int fd = pollFds[i].fd;
            if (pollFds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                if (pollFds[i].revents & (POLLHUP | POLLERR)) {
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
                    }
                } else {
                    ::close(fd);
                    m_agents.erase(fd);
                }
                peerSock.release();
            }
        }

        // Detect crashes via waitpid
        std::vector<int> toRemove;
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

    it->second.pid = msg.pid;
    it->second.sessionUuid = msg.sessionUuid;
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

    // Launch c2
    pid_t pid = fork();
    if (pid == 0) {
        execlp("c2", "c2", "--socket", m_c2SocketPath.c_str(), nullptr);
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
