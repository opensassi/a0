#include "c2_listener.h"
#include "sse_manager.h"
#include "c2_event_store.h"
#include "ipc_protocol.h"
#include <unistd.h>
#include <sys/poll.h>
#include <cerrno>
#include <algorithm>
#include <iostream>
#include <chrono>

namespace a0::c2 {

C2Listener::C2Listener(const std::string& socketPath, B1Registry* registry,
                        SseManager* sse, EventStore* events)
    : m_socketPath(socketPath)
    , m_registry(registry)
    , m_sse(sse)
    , m_events(events)
{
}

C2Listener::~C2Listener() {
    shutdown();
}

int C2Listener::sendToB1(int b1Pid, const ipc::Message& msg) {
    std::lock_guard<std::mutex> lock(m_b1Mutex);
    auto it = m_b1PidToFd.find(b1Pid);
    if (it == m_b1PidToFd.end()) return -1;

    ipc::UnixSocket sock(it->second);
    int rc = ipc::sendMessage(sock, msg);
    sock.release();
    return rc;
}

int C2Listener::run() {
    xCleanupStaleSocket();

    int rc = m_listenSocket.bindAndListen(m_socketPath);
    if (rc < 0) return -1;

    m_listenFd = m_listenSocket.fd();
    m_running = true;

    std::vector<struct pollfd> pollFds;

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
                {
                    std::lock_guard<std::mutex> lock(m_b1Mutex);
                    for (auto it = m_b1PidToFd.begin(); it != m_b1PidToFd.end(); ) {
                        if (it->second == fd) it = m_b1PidToFd.erase(it);
                        else ++it;
                    }
                }
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
                    {
                        std::lock_guard<std::mutex> lock(m_b1Mutex);
                        for (auto it = m_b1PidToFd.begin(); it != m_b1PidToFd.end(); ) {
                            if (it->second == fd) it = m_b1PidToFd.erase(it);
                            else ++it;
                        }
                    }
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
        return xHandleRegister(msg, peerFd);
    } else if (type == "update") {
        return xHandleUpdate(msg);
    } else if (type == "user_prompt") {
        return xHandleUserPrompt(msg);
    } else if (type == "stream_data") {
        return xHandleStreamData(msg);
    } else if (type == "stream_end") {
        return xHandleStreamEnd(msg);
    } else if (type == "terminal_ready") {
        if (m_sse) {
            nlohmann::json ev;
            ev["terminalId"] = msg.value("terminalId", "");
            ev["streamId"] = msg.value("streamId", 0);
            ev["pid"] = msg.value("pid", 0);
            m_sse->broadcast("terminal_ready", ev.dump());
        }
        return 0;
    }

    return -1;
}

int C2Listener::xHandleRegister(const nlohmann::json& msg, int peerFd) {
    if (!msg.contains("pid") || !msg["pid"].is_number_integer()) return -1;
    int pid = msg["pid"].get<int>();
    std::string wd = msg.value("wd", "");
    std::string hostname = msg.value("hostname", "");

    {
        std::lock_guard<std::mutex> lock(m_b1Mutex);
        m_b1PidToFd[pid] = peerFd;
    }

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
            agents.push_back(s);
        }
    }
    return m_registry->updateAgents(pid, agents);
}

int C2Listener::xHandleUserPrompt(const nlohmann::json& msg) {
    std::string session = msg.value("session", "");
    std::string toolCallId = msg.value("toolCallId", "");
    std::string prompt = msg.value("prompt", "");
    if (session.empty() || toolCallId.empty() || prompt.empty()) return -1;

    m_events->upsertPrompt(session, toolCallId, prompt, "");

    if (m_sse) {
        nlohmann::json ev;
        ev["session"] = session;
        ev["toolCallId"] = toolCallId;
        ev["prompt"] = prompt;
        ev["timestamp"] = std::to_string(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        m_sse->broadcast("user_prompt", ev.dump());
    }
    return 0;
}

int C2Listener::xHandleStreamData(const nlohmann::json& msg) {
    if (!m_sse) return -1;
    nlohmann::json ev;
    ev["streamId"] = msg.value("streamId", 0);
    ev["seq"] = msg.value("chunkSeq", 0);
    ev["direction"] = msg.value("chunkDirection", "stdout");
    ev["data"] = msg.value("chunkData", "");
    ev["timestamp"] = std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    m_sse->broadcast("stream_chunk", ev.dump());
    return 0;
}

int C2Listener::xHandleStreamEnd(const nlohmann::json& msg) {
    if (!m_sse) return -1;
    nlohmann::json ev;
    ev["streamId"] = msg.value("streamId", 0);
    ev["exitCode"] = msg.value("pid", 0);
    m_sse->broadcast("stream_end", ev.dump());
    return 0;
}

void C2Listener::xCleanupStaleSocket() {
    ipc::UnixSocket::unlinkPath(m_socketPath);
}

} // namespace a0::c2
