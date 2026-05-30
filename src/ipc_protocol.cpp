#include "ipc_protocol.h"
#include <vector>
#include <algorithm>

namespace a0::ipc {

std::string serialize(const Message& msg) {
    nlohmann::json j;
    j["type"] = msg.type;
    if (msg.pid != 0) j["pid"] = msg.pid;
    if (!msg.sessionUuid.empty()) j["session"] = msg.sessionUuid;
    if (!msg.workdir.empty()) j["wd"] = msg.workdir;
    if (!msg.hostname.empty()) j["hostname"] = msg.hostname;
    if (!msg.agents.is_null()) j["agents"] = msg.agents;
    if (!msg.status.empty()) j["status"] = msg.status;
    if (!msg.error.empty()) j["error"] = msg.error;
    if (!msg.reason.empty()) j["reason"] = msg.reason;
    if (!msg.toolCallId.empty()) j["toolCallId"] = msg.toolCallId;
    if (!msg.prompt.empty()) j["prompt"] = msg.prompt;
    return j.dump() + "\n";
}

int deserialize(const std::string& jsonLine, Message& msg) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonLine);
    } catch (...) {
        return -1;
    }

    if (!j.contains("type") || !j["type"].is_string()) {
        return -2;
    }

    msg = Message();
    msg.type = j["type"].get<std::string>();

    if (j.contains("pid") && j["pid"].is_number_integer()) {
        msg.pid = j["pid"].get<int>();
    }
    if (j.contains("session") && j["session"].is_string()) {
        msg.sessionUuid = j["session"].get<std::string>();
    }
    if (j.contains("wd") && j["wd"].is_string()) {
        msg.workdir = j["wd"].get<std::string>();
    }
    if (j.contains("hostname") && j["hostname"].is_string()) {
        msg.hostname = j["hostname"].get<std::string>();
    }
    if (j.contains("agents")) {
        msg.agents = j["agents"];
    }
    if (j.contains("status") && j["status"].is_string()) {
        msg.status = j["status"].get<std::string>();
    }
    if (j.contains("error") && j["error"].is_string()) {
        msg.error = j["error"].get<std::string>();
    }
    if (j.contains("reason") && j["reason"].is_string()) {
        msg.reason = j["reason"].get<std::string>();
    }
    if (j.contains("toolCallId") && j["toolCallId"].is_string()) {
        msg.toolCallId = j["toolCallId"].get<std::string>();
    }
    if (j.contains("prompt") && j["prompt"].is_string()) {
        msg.prompt = j["prompt"].get<std::string>();
    }

    return 0;
}

int recvMessage(UnixSocket& sock, Message& msg, int timeoutMs) {
    std::string buffer;
    while (true) {
        int pollRc = sock.pollReadable(timeoutMs);
        if (pollRc < 0) return -1;
        if (pollRc == 0) return -2;

        std::vector<char> chunk(1);
        size_t received = 0;
        int rc = sock.recv(chunk, received);
        if (rc < 0) return -1;
        if (received == 0) return -1;

        buffer.push_back(chunk[0]);
        if (chunk[0] == '\n') {
            if (!buffer.empty()) buffer.pop_back();
            return deserialize(buffer, msg);
        }
    }
}

int sendMessage(UnixSocket& sock, const Message& msg) {
    std::string wire = serialize(msg);
    int rc = sock.send(wire);
    if (rc < 0) return -1;
    return 0;
}

} // namespace a0::ipc
