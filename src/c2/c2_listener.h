#pragma once

#include <string>
#include "nlohmann/json.hpp"
#include "unix_socket.h"
#include "b1_registry.h"

namespace a0::c2 {

class C2Listener {
public:
    C2Listener(const std::string& socketPath, B1Registry* registry);
    ~C2Listener();

    int run();
    void shutdown();

private:
    std::string m_socketPath;
    B1Registry* m_registry;
    ipc::UnixSocket m_listenSocket;
    int m_listenFd = -1;
    bool m_running = false;

    int xHandleMessage(const nlohmann::json& msg, int peerFd);
    int xHandleRegister(const nlohmann::json& msg);
    int xHandleUpdate(const nlohmann::json& msg);
    void xCleanupStaleSocket();
};

} // namespace a0::c2
