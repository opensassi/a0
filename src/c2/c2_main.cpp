#include "b1_registry.h"
#include "c2_listener.h"
#include "dashboard_server.h"
#include "sse_manager.h"
#include "c2_event_store.h"
#include "ipc/unix_socket.h"
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <csignal>
#include <thread>
#include <fcntl.h>
#include <unistd.h>

static a0::c2::DashboardServer* g_dashboard = nullptr;
static a0::c2::C2Listener* g_listener = nullptr;
static std::string* g_socketPath = nullptr;
static std::string* g_pidPath = nullptr;
extern std::string g_c2LogFile;

static void handleSignal(int) {
    if (g_dashboard) g_dashboard->shutdown();
    if (g_listener) g_listener->shutdown();
    if (g_socketPath) a0::ipc::UnixSocket::unlinkPath(*g_socketPath);
    if (g_pidPath) std::remove(g_pidPath->c_str());
    _exit(0);
}

static std::string xGetWebRoot(const std::string& cwd) {
#ifdef C2_DEFAULT_WEB_ROOT
    return C2_DEFAULT_WEB_ROOT;
#else
    return cwd + "/.a0/git/opensassi/a0/c2/web";
#endif
}

int main(int argc, char* argv[]) {
    int port = 8080;
    std::string socketPath;
    std::string sslKey;
    std::string sslCert;
    std::string webRoot;

    const char* portEnv = std::getenv("A0_C2_PORT");
    if (portEnv) port = std::stoi(portEnv);

    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    std::string baseDir = xdg ? std::string(xdg) : "/tmp";
    socketPath = baseDir + "/a0-c2.sock";

    // Determine CWD for default web root
    char cwdBuf[4096];
    std::string cwd = (getcwd(cwdBuf, sizeof(cwdBuf))) ? cwdBuf : "";
    webRoot = xGetWebRoot(cwd);

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--socket" && i + 1 < argc) {
            socketPath = argv[++i];
        } else if (arg == "--web-root" && i + 1 < argc) {
            webRoot = argv[++i];
        } else if (arg == "--ssl-key" && i + 1 < argc) {
            sslKey = argv[++i];
        } else if (arg == "--ssl-cert" && i + 1 < argc) {
            sslCert = argv[++i];
        } else if (arg == "--log-file" && i + 1 < argc) {
            g_c2LogFile = argv[++i];
        } else if (arg == "--help") {
            std::cout << "c2 [--port <n>] [--socket <path>] [--web-root <path>] [--ssl-key <file> --ssl-cert <file>] [--log-file <path>]\n";
            return 0;
        }
    }

    // Redirect stdout + stderr to log file if specified
    if (!g_c2LogFile.empty()) {
        int fd = ::open(g_c2LogFile.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            ::dup2(fd, STDOUT_FILENO);
            ::dup2(fd, STDERR_FILENO);
            ::close(fd);
        }
    }

    // Clean up stale socket from previous crash before binding
    a0::ipc::UnixSocket::unlinkPath(socketPath);

    std::string dbPath = socketPath + ".db";
    a0::c2::EventStore eventStore(dbPath);
    a0::c2::SseManager sse;
    a0::c2::B1Registry registry;
    registry.setSseManager(&sse);

    std::string pidPath = baseDir + "/a0-c2.pid";
    std::remove(pidPath.c_str());
    {
        std::ofstream pf(pidPath);
        if (pf) pf << getpid() << std::endl;
    }

    a0::c2::C2Listener listener(socketPath, &registry, &sse, &eventStore);
    a0::c2::DashboardServer dashboard(port, &registry, &sse, &eventStore, &listener, webRoot, sslKey, sslCert);
    g_dashboard = &dashboard;
    g_listener = &listener;
    g_socketPath = &socketPath;
    g_pidPath = &pidPath;

    struct sigaction sa;
    sa.sa_handler = handleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    std::thread listenerThread([&listener]() {
        int rc = listener.run();
        if (rc < 0) {
            std::cerr << "c2: listener failed (" << rc << ")\n";
        }
    });

    std::cerr << "c2: running (port=" << port << " socket=" << socketPath
              << " web-root=" << webRoot;
    if (!sslKey.empty()) std::cerr << " ssl=enabled";
    std::cerr << ")\n";

    int rc = dashboard.run();
    if (rc < 0) {
        std::cerr << "c2: dashboard failed (" << rc << ")\n";
    }

    listener.shutdown();
    listenerThread.join();

    a0::ipc::UnixSocket::unlinkPath(socketPath);
    std::remove(pidPath.c_str());
    return 0;
}
