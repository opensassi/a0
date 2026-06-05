#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include "nlohmann/json.hpp"
#include "unix_socket.h"
#include "b1_registry.h"
#include "ipc_protocol.h"

namespace a0::c2 {

class SseManager;
class EventStore;

class C2Listener {
public:
    C2Listener(const std::string& socketPath, B1Registry* registry,
               SseManager* sse, EventStore* events);
    ~C2Listener();

    int run();
    void shutdown();

    int sendToB1(int b1Pid, const ipc::Message& msg);

private:
    std::string m_socketPath;
    B1Registry* m_registry;
    SseManager* m_sse;
    EventStore* m_events;
    ipc::UnixSocket m_listenSocket;
    int m_listenFd = -1;
    bool m_running = false;
    std::unordered_map<int, ipc::BufferedSocket> m_peers;
    std::unordered_map<int, int> m_b1PidToFd;
    std::mutex m_b1Mutex;

    int xHandleMessage(const nlohmann::json& msg, int peerFd);
    int xHandleRegister(const nlohmann::json& msg, int peerFd);
    int xHandleUpdate(const nlohmann::json& msg);
    int xHandleUserPrompt(const nlohmann::json& msg);
    int xHandleStreamData(const nlohmann::json& msg);
    int xHandleStreamEnd(const nlohmann::json& msg);
    void xCleanupStaleSocket();
};

} // namespace a0::c2
