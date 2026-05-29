#pragma once

#include <string>
#include <vector>
#include <chrono>

namespace a0::ipc {

class UnixSocket {
public:
    UnixSocket();
    explicit UnixSocket(int fd);
    UnixSocket(UnixSocket&& other) noexcept;
    UnixSocket& operator=(UnixSocket&& other) noexcept;
    UnixSocket(const UnixSocket&) = delete;
    UnixSocket& operator=(const UnixSocket&) = delete;
    ~UnixSocket();

    int bindAndListen(const std::string& socketPath, int backlog = 5);
    int accept(UnixSocket& client);
    int connect(const std::string& socketPath, int timeoutMs = 5000);
    int send(const std::string& data);
    int recv(std::vector<char>& buf, size_t& received);
    int pollReadable(int timeoutMs = -1) const;
    int pollWritable(int timeoutMs = -1) const;
    void close();
    static void unlinkPath(const std::string& socketPath);

    int fd() const { return m_fd; }
    bool isOpen() const { return m_fd >= 0; }

    /// Release ownership of the fd. Caller must close it.
    int release() { int fd = m_fd; m_fd = -1; return fd; }

private:
    int m_fd = -1;
    int xSetNonBlocking();
    int xCreateSocket();
};

} // namespace a0::ipc
