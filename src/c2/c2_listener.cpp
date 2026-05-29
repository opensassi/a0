#include "c2_listener.h"
#include "ipc_protocol.h"
#include <unistd.h>
#include <sys/poll.h>
#include <cerrno>
#include <iostream>

namespace a0::c2 {

C2Listener::C2Listener(const std::string& socketPath, B1Registry* registry)
    : m_socketPath(socketPath)
    , m_registry(registry)
{
}

C2Listener::~C2Listener() {
    shutdown();
}

int C2Listener::run() {
    xCleanupStaleSocket();

    int rc = m_listenSocket.bindAndListen(m_socketPath);
    if (rc < 0) return -1;

    m_listenFd = m_listenSocket.fd();
    m_running = true;

    std::vector<struct pollfd> pollFds;
    std::vector<int> peerFds;

    while (m_running) {
        pollFds.clear();
        pollFds.push_back({m_listenFd, POLLIN, 0});

        for (int fd : peerFds) {
            pollFds.push_back({fd, POLLIN, 0});
        }

        rc = ::poll(pollFds.data(), pollFds.size(), 1000);
        if (rc < 0) {
            if (errno == EINTR) continue;
            continue;
        }

        // Accept new b1 connections
        if (pollFds[0].revents & POLLIN) {
            ipc::UnixSocket client;
            if (m_listenSocket.accept(client) == 0 && client.isOpen()) {
                peerFds.push_back(client.release());
            }
        }

        // Handle b1 messages
        for (size_t i = 1; i < pollFds.size(); ++i) {
            int fd = pollFds[i].fd;
            if (pollFds[i].revents & (POLLHUP | POLLERR)) {
                ::close(fd);
                auto it = std::find(peerFds.begin(), peerFds.end(), fd);
                if (it != peerFds.end()) peerFds.erase(it);
                continue;
            }

            if (pollFds[i].revents & POLLIN) {
                ipc::UnixSocket sock(fd);
                ipc::Message msg;
                int r = ipc::recvMessage(sock, msg, 100);
                if (r == 0) {
                    xHandleMessage(nlohmann::json::parse(ipc::serialize(msg)), fd);
                } else {
                    ::close(fd);
                    auto it = std::find(peerFds.begin(), peerFds.end(), fd);
                    if (it != peerFds.end()) peerFds.erase(it);
                }
                sock.release();
            }
        }
    }

    return 0;
}

void C2Listener::shutdown() {
    m_running = false;
    m_listenSocket.close();
}

int C2Listener::xHandleMessage(const nlohmann::json& msg, int peerFd) {
    if (!msg.contains("type") || !msg["type"].is_string()) return -1;

    std::string type = msg["type"].get<std::string>();
    if (type == "register") {
        return xHandleRegister(msg);
    } else if (type == "update") {
        return xHandleUpdate(msg);
    }

    return -1;
}

int C2Listener::xHandleRegister(const nlohmann::json& msg) {
    if (!msg.contains("pid") || !msg["pid"].is_number_integer()) return -1;

    int pid = msg["pid"].get<int>();
    std::string wd = msg.value("wd", "");
    std::string hostname = msg.value("hostname", "");

    return m_registry->upsertB1(pid, wd, hostname);
}

int C2Listener::xHandleUpdate(const nlohmann::json& msg) {
    if (!msg.contains("pid") || !msg["pid"].is_number_integer()) return -1;

    int pid = msg["pid"].get<int>();
    std::vector<AgentSummary> agents;

    if (msg.contains("agents") && msg["agents"].is_array()) {
        for (const auto& ag : msg["agents"]) {
            AgentSummary s;
            s.pid = ag.value("pid", 0);
            s.sessionUuid = ag.value("session", "");
            s.state = ag.value("state", "running");
            s.connectedAt = ag.value("connectedAt", 0);
            s.lastHeartbeat = ag.value("lastHeartbeat", 0);
            agents.push_back(s);
        }
    }

    return m_registry->updateAgents(pid, agents);
}

void C2Listener::xCleanupStaleSocket() {
    ipc::UnixSocket::unlinkPath(m_socketPath);
}

} // namespace a0::c2
