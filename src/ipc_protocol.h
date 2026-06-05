#pragma once

#include <string>
#include <cstdint>
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
    std::string toolCallId;
    std::string prompt;

    // Streaming fields
    int64_t streamId = 0;
    int chunkSeq = 0;
    std::string chunkDirection;
    std::string chunkData;
    std::string terminalId;
    std::string contextType;
    std::string contextId;
    std::string cwd;
};

namespace MessageType {
    constexpr const char* REGISTER      = "register";
    constexpr const char* ACK           = "ack";
    constexpr const char* HEARTBEAT     = "heartbeat";
    constexpr const char* UPDATE        = "update";
    constexpr const char* SHUTDOWN      = "shutdown";
    constexpr const char* USER_PROMPT   = "user_prompt";
    constexpr const char* PROMPT_REPLY  = "prompt_reply";
    constexpr const char* STREAM_DATA   = "stream_data";
    constexpr const char* STREAM_END    = "stream_end";
    constexpr const char* STREAM_INPUT  = "stream_input";
    constexpr const char* TERMINAL_OPEN  = "terminal_open";
    constexpr const char* TERMINAL_READY = "terminal_ready";
} // namespace MessageType

std::string serialize(const Message& msg);
int deserialize(const std::string& jsonLine, Message& msg);
int recvMessage(UnixSocket& sock, Message& msg, int timeoutMs = 5000);
int sendMessage(UnixSocket& sock, const Message& msg);

// ============================================================================
// Buffered recv result codes
// ============================================================================

enum RecvResult {
    RECV_OK = 0,      // complete \n-delimited message in msg
    RECV_AGAIN = 1,   // no complete message yet, retry next poll
    RECV_ERR = -1     // fatal, close connection
};

// ============================================================================
// BufferedSocket — persistent per-connection buffered reader
// ============================================================================

class BufferedSocket {
public:
    BufferedSocket() = default;
    explicit BufferedSocket(int fd);
    ~BufferedSocket();

    BufferedSocket(BufferedSocket&& other) noexcept
        : m_fd(other.m_fd), m_buffer(std::move(other.m_buffer)) {
        other.m_fd = -1;
    }
    BufferedSocket& operator=(BufferedSocket&& other) noexcept {
        if (this != &other) {
            close();
            m_fd = other.m_fd;
            m_buffer = std::move(other.m_buffer);
            other.m_fd = -1;
        }
        return *this;
    }

    int fd() const { return m_fd; }
    int release();
    void close();

    /// Read one complete \n-delimited message.
    int recv(Message& msg, int timeoutMs = 5000);

    /// Send a message.
    int send(const Message& msg);

private:
    int m_fd = -1;
    std::string m_buffer;
    static constexpr size_t READ_CHUNK = 100;
    static constexpr size_t MAX_BUFFER = 65536;
};

} // namespace a0::ipc
