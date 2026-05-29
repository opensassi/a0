#include "supervisor.h"
#include <iostream>
#include <csignal>
#include <cstdlib>

static a0::b1::Supervisor* g_supervisor = nullptr;

static void handleSignal(int sig) {
    (void)sig;
    if (g_supervisor) {
        g_supervisor->shutdown();
    }
}

int main(int argc, char* argv[]) {
    std::string workdir = ".";
    std::string a0Dir = workdir + "/.a0";
    bool noC2 = false;
    std::string c2Socket;

    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg) {
        c2Socket = std::string(xdg) + "/a0-c2.sock";
    } else {
        c2Socket = "/tmp/a0-c2.sock";
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--workdir" && i + 1 < argc) {
            workdir = argv[++i];
        } else if (arg == "--a0-dir" && i + 1 < argc) {
            a0Dir = argv[++i];
        } else if (arg == "--no-c2") {
            noC2 = true;
        } else if (arg == "--c2-socket" && i + 1 < argc) {
            c2Socket = argv[++i];
        } else if (arg == "--help") {
            std::cout << "b1 --workdir <path> [--a0-dir <path>] [--no-c2] [--c2-socket <path>]\n";
            return 0;
        }
    }

    std::string sockPath = a0Dir + "/b1.sock";
    std::string pidPath = a0Dir + "/b1.pid";

    if (noC2) {
        c2Socket.clear();
    }

    a0::b1::Supervisor supervisor(sockPath, pidPath, c2Socket, workdir);
    g_supervisor = &supervisor;

    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

    int rc = supervisor.init();
    if (rc < 0) {
        std::cerr << "b1: init failed (" << rc << ")\n";
        return 1;
    }

    std::cerr << "b1: running (workdir=" << workdir << ")\n";
    supervisor.run();

    return 0;
}
