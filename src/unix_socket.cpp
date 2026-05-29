#include "unix_socket.h"
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <iostream>

namespace a0::ipc {

UnixSocket::UnixSocket() : m_fd(-1) {}

UnixSocket::UnixSocket(int fd) : m_fd(fd) {}

UnixSocket::UnixSocket(UnixSocket&& other) noexcept : m_fd(other.m_fd) {
    other.m_fd = -1;
}

UnixSocket& UnixSocket::operator=(UnixSocket&& other) noexcept {
    if (this != &other) {
        close();
        m_fd = other.m_fd;
        other.m_fd = -1;
    }
    return *this;
}

UnixSocket::~UnixSocket() {
    close();
}

int UnixSocket::bindAndListen(const std::string& socketPath, int backlog) {
    close();
    unlinkPath(socketPath);

    int fd = xCreateSocket();
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    size_t pathLen = socketPath.size();
    if (pathLen >= sizeof(addr.sun_path)) {
        close();
        return -1;
    }
    memcpy(addr.sun_path, socketPath.data(), pathLen);

    if (::bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close();
        return -1;
    }

    if (::listen(fd, backlog) < 0) {
        close();
        return -1;
    }

    m_fd = fd;
    return 0;
}

int UnixSocket::accept(UnixSocket& client) {
    if (m_fd < 0) return -1;

    struct sockaddr_un addr;
    socklen_t addrLen = sizeof(addr);
    int clientFd = ::accept(m_fd, (struct sockaddr*)&addr, &addrLen);
    if (clientFd < 0) {
        return -1;
    }

    client = UnixSocket(clientFd);
    return 0;
}

int UnixSocket::connect(const std::string& socketPath, int timeoutMs) {
    close();

    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    size_t pathLen = socketPath.size();
    if (pathLen >= sizeof(addr.sun_path)) {
        ::close(fd);
        return -1;
    }
    memcpy(addr.sun_path, socketPath.data(), pathLen);

    int rc = ::connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (rc == 0) {
        m_fd = fd;
        return 0;
    }

    if (errno == EINPROGRESS) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        int pollRc = ::poll(&pfd, 1, timeoutMs);
        if (pollRc <= 0) {
            ::close(fd);
            return -1;
        }
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
            ::close(fd);
            return -1;
        }
        m_fd = fd;
        return 0;
    }

    ::close(fd);
    return -1;
}

int UnixSocket::send(const std::string& data) {
    if (m_fd < 0) return -1;
    size_t total = 0;
    while (total < data.size()) {
        ssize_t n = ::send(m_fd, data.data() + total, data.size() - total, MSG_NOSIGNAL);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += static_cast<size_t>(n);
    }
    return static_cast<int>(total);
}

int UnixSocket::recv(std::vector<char>& buf, size_t& received) {
    received = 0;
    if (m_fd < 0) return -1;
    ssize_t n = ::recv(m_fd, buf.data(), buf.size(), 0);
    if (n <= 0) {
        return -1;
    }
    received = static_cast<size_t>(n);
    return 0;
}

int UnixSocket::pollReadable(int timeoutMs) const {
    if (m_fd < 0) return -1;
    struct pollfd pfd;
    pfd.fd = m_fd;
    pfd.events = POLLIN;
    int rc = ::poll(&pfd, 1, timeoutMs);
    if (rc < 0) return -1;
    if (rc == 0) return 0;
    if (pfd.revents & (POLLERR | POLLHUP)) return -1;
    return 1;
}

int UnixSocket::pollWritable(int timeoutMs) const {
    if (m_fd < 0) return -1;
    struct pollfd pfd;
    pfd.fd = m_fd;
    pfd.events = POLLOUT;
    int rc = ::poll(&pfd, 1, timeoutMs);
    if (rc < 0) return -1;
    if (rc == 0) return 0;
    if (pfd.revents & (POLLERR | POLLHUP)) return -1;
    return 1;
}

void UnixSocket::close() {
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

void UnixSocket::unlinkPath(const std::string& socketPath) {
    ::unlink(socketPath.c_str());
}

int UnixSocket::xCreateSocket() {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    return fd;
}

} // namespace a0::ipc
