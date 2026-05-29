#include "b1_registry.h"
#include "c2_listener.h"
#include "dashboard_server.h"
#include "unix_socket.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <thread>

static a0::c2::B1Registry* g_registry = nullptr;
static a0::c2::C2Listener* g_listener = nullptr;
static a0::c2::DashboardServer* g_dashboard = nullptr;

static void handleSignal(int sig) {
    (void)sig;
    if (g_dashboard) g_dashboard->shutdown();
    if (g_listener) g_listener->shutdown();
}

int main(int argc, char* argv[]) {
    int port = 8080;
    std::string socketPath;
    std::string sslKey;
    std::string sslCert;

    const char* portEnv = std::getenv("A0_C2_PORT");
    if (portEnv) {
        port = std::stoi(portEnv);
    }
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg) {
        socketPath = std::string(xdg) + "/a0-c2.sock";
    } else {
        socketPath = "/tmp/a0-c2.sock";
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--socket" && i + 1 < argc) {
            socketPath = argv[++i];
        } else if (arg == "--ssl-key" && i + 1 < argc) {
            sslKey = argv[++i];
        } else if (arg == "--ssl-cert" && i + 1 < argc) {
            sslCert = argv[++i];
        } else if (arg == "--help") {
            std::cout << "c2 [--port <n>] [--socket <path>] [--ssl-key <file> --ssl-cert <file>]\n";
            return 0;
        }
    }

    a0::c2::B1Registry registry;
    a0::c2::C2Listener listener(socketPath, &registry);
    a0::c2::DashboardServer dashboard(port, &registry, sslKey, sslCert);

    g_registry = &registry;
    g_listener = &listener;
    g_dashboard = &dashboard;

    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

    std::thread listenerThread([&listener]() {
        int rc = listener.run();
        if (rc < 0) {
            std::cerr << "c2: listener failed (" << rc << ")\n";
        }
    });

    std::cerr << "c2: running (port=" << port << " socket=" << socketPath;
    if (!sslKey.empty()) std::cerr << " ssl=enabled";
    std::cerr << ")\n";

    int rc = dashboard.run();
    if (rc < 0) {
        std::cerr << "c2: dashboard failed (" << rc << ")\n";
    }

    listener.shutdown();
    listenerThread.join();

    a0::ipc::UnixSocket::unlinkPath(socketPath);
    return 0;
}
