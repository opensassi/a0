#pragma once

#include <string>
#include "nlohmann/json.hpp"
#include "unix_socket.h"

namespace a0::ipc {

struct Message {
    std::string type;
    int pid = 0;
    std::string sessionUuid;
    std::string workdir;
    std::string hostname;
    nlohmann::json agents;
    std::string status;
    std::string error;
    std::string reason;
};

namespace MessageType {
    constexpr const char* REGISTER  = "register";
    constexpr const char* ACK       = "ack";
    constexpr const char* HEARTBEAT = "heartbeat";
    constexpr const char* UPDATE    = "update";
    constexpr const char* SHUTDOWN  = "shutdown";
} // namespace MessageType

std::string serialize(const Message& msg);
int deserialize(const std::string& jsonLine, Message& msg);
int recvMessage(UnixSocket& sock, Message& msg, int timeoutMs = 5000);
int sendMessage(UnixSocket& sock, const Message& msg);

} // namespace a0::ipc
