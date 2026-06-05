#include "ipc_protocol.h"
#include <vector>
#include <algorithm>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>

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
    if (msg.streamId != 0) j["streamId"] = msg.streamId;
    if (msg.chunkSeq != 0) j["chunkSeq"] = msg.chunkSeq;
    if (!msg.chunkDirection.empty()) j["chunkDirection"] = msg.chunkDirection;
    if (!msg.chunkData.empty()) j["chunkData"] = msg.chunkData;
    if (!msg.terminalId.empty()) j["terminalId"] = msg.terminalId;
    if (!msg.contextType.empty()) j["contextType"] = msg.contextType;
    if (!msg.contextId.empty()) j["contextId"] = msg.contextId;
    if (!msg.cwd.empty()) j["cwd"] = msg.cwd;
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
    if (j.contains("streamId") && j["streamId"].is_number_integer()) {
        msg.streamId = j["streamId"].get<int64_t>();
    }
    if (j.contains("chunkSeq") && j["chunkSeq"].is_number_integer()) {
        msg.chunkSeq = j["chunkSeq"].get<int>();
    }
    if (j.contains("chunkDirection") && j["chunkDirection"].is_string()) {
        msg.chunkDirection = j["chunkDirection"].get<std::string>();
    }
    if (j.contains("chunkData") && j["chunkData"].is_string()) {
        msg.chunkData = j["chunkData"].get<std::string>();
    }
    if (j.contains("terminalId") && j["terminalId"].is_string()) {
        msg.terminalId = j["terminalId"].get<std::string>();
    }
    if (j.contains("contextType") && j["contextType"].is_string()) {
        msg.contextType = j["contextType"].get<std::string>();
    }
    if (j.contains("contextId") && j["contextId"].is_string()) {
        msg.contextId = j["contextId"].get<std::string>();
    }
    if (j.contains("cwd") && j["cwd"].is_string()) {
        msg.cwd = j["cwd"].get<std::string>();
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

// ============================================================================
// BufferedSocket implementation
// ============================================================================

BufferedSocket::BufferedSocket(int fd) : m_fd(fd) {}

BufferedSocket::~BufferedSocket() { close(); }

int BufferedSocket::release() {
    int fd = m_fd;
    m_fd = -1;
    m_buffer.clear();
    return fd;
}

void BufferedSocket::close() {
    if (m_fd >= 0) ::close(m_fd);
    m_fd = -1;
    m_buffer.clear();
}

int BufferedSocket::send(const Message& msg) {
    if (m_fd < 0) return -1;
    std::string wire = serialize(msg);
    size_t total = 0;
    while (total < wire.size()) {
        ssize_t n = ::send(m_fd, wire.data() + total,
                           wire.size() - total, MSG_NOSIGNAL);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += static_cast<size_t>(n);
    }
    return 0;
}

int BufferedSocket::recv(Message& msg, int timeoutMs) {
    if (m_fd < 0) return RECV_ERR;

    // 1. Check existing buffer for a complete message
    auto nl = m_buffer.find('\n');
    if (nl != std::string::npos) {
        std::string line = m_buffer.substr(0, nl);
        m_buffer.erase(0, nl + 1);
        return deserialize(line, msg) == 0 ? RECV_OK : RECV_ERR;
    }

    // 2. No complete message — poll for data
    struct pollfd pfd = {m_fd, POLLIN, 0};
    int rc = ::poll(&pfd, 1, timeoutMs);
    if (rc < 0) return RECV_ERR;
    if (rc == 0) return RECV_AGAIN;

    // 3. Read what's available (up to READ_CHUNK bytes)
    char buf[READ_CHUNK];
    ssize_t n = ::recv(m_fd, buf, sizeof(buf), 0);
    if (n <= 0) return RECV_ERR;

    m_buffer.append(buf, static_cast<size_t>(n));

    // 4. Guard against unbounded growth
    if (m_buffer.size() > MAX_BUFFER) {
        m_buffer.clear();
        return RECV_ERR;
    }

    // 5. Scan for message delimiter in accumulated buffer
    nl = m_buffer.find('\n');
    if (nl != std::string::npos) {
        std::string line = m_buffer.substr(0, nl);
        m_buffer.erase(0, nl + 1);
        return deserialize(line, msg) == 0 ? RECV_OK : RECV_ERR;
    }

    // Partial data received but no complete message yet
    return RECV_AGAIN;
}

} // namespace a0::ipc
