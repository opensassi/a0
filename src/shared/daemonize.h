#pragma once

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstdlib>
#include <string>

namespace a0 {

/// Close every open file descriptor >= 3.
/// Uses /proc/self/fd for efficiency, falls back to iterating up to OPEN_MAX.
inline void xCloseAllFds() {
    DIR* dir = ::opendir("/proc/self/fd");
    if (dir) {
        int dirFd = ::dirfd(dir);
        struct dirent* entry;
        while ((entry = ::readdir(dir)) != nullptr) {
            int fd = std::atoi(entry->d_name);
            if (fd >= 3 && fd != dirFd)
                ::close(fd);
        }
        ::closedir(dir);
        return;
    }
    int maxFd = static_cast<int>(::sysconf(_SC_OPEN_MAX));
    if (maxFd <= 0) maxFd = 4096;
    for (int fd = 3; fd < maxFd; ++fd)
        ::close(fd);
}

/// Fully detach a forked child from its parent.
/// Call after fork() + setsid(), before exec().
///   - stdin  -> /dev/null
///   - stdout -> logPath (or /dev/null if empty)
///   - stderr -> logPath (or /dev/null if empty)
///   - closes every inherited fd >= 3
inline void xDaemonizeChild(const std::string& logPath) {
    int nullIn = ::open("/dev/null", O_RDONLY);
    if (nullIn >= 0) {
        ::dup2(nullIn, STDIN_FILENO);
        if (nullIn > STDERR_FILENO) ::close(nullIn);
    }

    int fd = -1;
    if (!logPath.empty())
        fd = ::open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        fd = ::open("/dev/null", O_WRONLY);
    if (fd >= 0) {
        ::dup2(fd, STDOUT_FILENO);
        ::dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) ::close(fd);
    }

    xCloseAllFds();
}

} // namespace a0
