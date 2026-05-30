#include "a0_dir.h"
#include "agent_core.h"
#include "persistence/sqlite_store.h"
#include "system_tools.h"
#include "skills/skills.h"
#include "context_manager.h"
#include "deepseek_provider.h"
#include "dependency_resolver.h"
#include "invocation_logger.h"
#include "schema_inference_engine.h"
#include "skill_runner.h"
#include "tool_runner.h"
#include "docker/container_manager.h"
#include "docker/compose_manager.h"
#include "docker/docker_tool_runner.h"
#include "unix_socket.h"
#include "ipc_protocol.h"
#include "stream_registry.h"
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>
#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

static void loadEnvFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        setenv(key.c_str(), val.c_str(), 1);
    }
}

static bool hasFlag(int argc, char* argv[], const std::string& name) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == name) return true;
    }
    return false;
}

static std::string getFlag(int argc, char* argv[],
                            const std::string& name,
                            const std::string& defaultVal) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == name && i + 1 < argc)
            return argv[i + 1];
    }
    const char* env = std::getenv(("A0_" + name.substr(2)).c_str());
    if (env) return env;
    return defaultVal;
}

static void killByPidFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return;
    int pid;
    f >> pid;
    if (pid <= 0) { std::remove(path.c_str()); return; }

    kill(pid, SIGTERM);
    for (int i = 0; i < 10; ++i) {
        usleep(100000);
        if (kill(pid, 0) != 0) {
            std::remove(path.c_str());
            return;
        }
    }
    kill(pid, SIGKILL);
    std::remove(path.c_str());
}

/// Read a c2 process's cmdline to find its --socket argument.
static std::string xC2SocketFromProc(int pid) {
    std::string cmdlinePath = "/proc/" + std::to_string(pid) + "/cmdline";
    std::ifstream f(cmdlinePath, std::ios::binary);
    if (!f) return "";
    std::vector<char> buf(4096, 0);
    f.read(buf.data(), buf.size() - 1);
    f.close();
    // cmdline is NUL-delimited; walk args looking for --socket
    const char* p = buf.data();
    while (p && *p) {
        std::string arg(p);
        if (arg == "--socket" || arg == "-s") {
            p += arg.size() + 1; // skip past NUL
            if (p && *p) return std::string(p);
        }
        p += arg.size() + 1;
        // Safety: don't walk past buffer
        if (p >= buf.data() + buf.size()) break;
    }
    return "";
}

/// Kill all processes with the given comm name using pgrep.
/// For c2 processes, also discover and return the socket paths that need cleanup.
/// Returns the number of processes killed.
static int killByProcessName(const std::string& name,
                              std::vector<std::string>* outSockets = nullptr,
                              int sigterm = SIGTERM) {
    std::string cmd = "pgrep -x " + name + " 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return 0;

    std::vector<int> pids;
    char line[64];
    while (fgets(line, sizeof(line), fp)) {
        int pid = atoi(line);
        if (pid > 0) pids.push_back(pid);
    }
    int rc = pclose(fp);
    (void)rc;

    for (int pid : pids) {
        if (outSockets && name == "c2") {
            std::string sock = xC2SocketFromProc(pid);
            if (!sock.empty()) outSockets->push_back(sock);
        }
    }

    int killed = 0;
    for (int pid : pids) {
        if (kill(pid, sigterm) == 0) ++killed;
    }

    if (killed > 0) {
        usleep(500000);
        for (int pid : pids) {
            if (kill(pid, 0) == 0) {
                kill(pid, SIGKILL);
            }
        }
    }
    return killed;
}

static std::string xSelfDir() {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return ".";
    buf[len] = '\0';
    std::string path(buf);
    auto slash = path.rfind('/');
    return (slash == std::string::npos) ? "." : path.substr(0, slash);
}

static std::string getC2PidPath() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    return xdg ? std::string(xdg) + "/a0-c2.pid" : "/tmp/a0-c2.pid";
}

int main(int argc, char* argv[]) {
    std::string envFilePath = ".env";
    std::string skillsDir = "./skills";
    std::string apiKey;
    std::string mockUrl;
    std::string resumeSessionId;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--env-file" && i + 1 < argc) {
            envFilePath = argv[++i];
        }
    }
    loadEnvFile(envFilePath);

    std::string a0Dir = "./.a0";
    std::string runPrompt;
    std::string runSkillName;
    std::string runParamsStr = "{}";
    std::string exportSessionId;
    bool noB1 = false;
    bool noContainerPool = false;
    bool killAll = false;
    bool terminalMode = false;
    std::string terminalId;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--env-file" && i + 1 < argc) {
            ++i;
        } else if (arg == "--a0-dir" && i + 1 < argc)
            a0Dir = argv[++i];
        else if (arg == "--skills-dir" && i + 1 < argc)
            skillsDir = argv[++i];
        else if (arg == "--api-key" && i + 1 < argc)
            apiKey = argv[++i];
        else if (arg == "--mock-api" && i + 1 < argc)
            mockUrl = argv[++i];
        else if (arg == "--resume" && i + 1 < argc)
            resumeSessionId = argv[++i];
        else if (arg == "--run" && i + 1 < argc)
            runPrompt = argv[++i];
        else if (arg == "--prompt" && i + 1 < argc)
            runParamsStr = json{{"prompt", std::string(argv[++i])}}.dump();
        else if (arg == "--params" && i + 1 < argc)
            runParamsStr = argv[++i];
        else if (arg == "--skill" && i + 1 < argc)
            runSkillName = argv[++i];
        else if (arg == "--export-session" && i + 1 < argc)
            exportSessionId = argv[++i];
        else if (arg == "--no-b1")
            noB1 = true;
        else if (arg == "--no-container-pool")
            noContainerPool = true;
        else if (arg == "--kill-all")
            killAll = true;
        else if (arg == "--terminal")
            terminalMode = true;
        else if (arg == "--terminal-id" && i + 1 < argc)
            terminalId = argv[++i];
        else if (arg == "--cwd" && i + 1 < argc) {
            // Change to the requested directory before terminal init
            const char* dir = argv[++i];
            if (chdir(dir) != 0) {
                std::cerr << "a0: warning: could not chdir to " << dir << std::endl;
            }
        }
    }

    if (apiKey.empty()) {
        const char* envKey = std::getenv("DEEPSEEK_API_KEY");
        if (envKey) apiKey = envKey;
    }
    if (apiKey.empty()) {
        const char* home = std::getenv("HOME");
        if (home) {
            loadEnvFile(std::string(home) + "/.deepseek.env");
            const char* envKey = std::getenv("DEEPSEEK_API_KEY");
            if (envKey) apiKey = envKey;
        }
    }

    // Ensure .a0/ directory exists (non-committed agent artifacts root)
    int a0Created = a0::ensureA0Dir(a0Dir);
    if (a0Created < 0) {
        std::cerr << "a0: fatal: could not create " << a0Dir << std::endl;
        return 1;
    }

    if (killAll) {
        std::string b1PidPath = a0Dir + "/b1.pid";
        std::string b1SockPath = a0Dir + "/b1.sock";
        killByPidFile(b1PidPath);
        killByPidFile(getC2PidPath());
        // Scan /proc for orphaned c2 processes and discover their sockets
        std::vector<std::string> c2Sockets;
        killByProcessName("c2", &c2Sockets);
        killByProcessName("b1");
        a0::ipc::UnixSocket::unlinkPath(b1SockPath);
        a0::ipc::UnixSocket::unlinkPath(getC2PidPath());
        for (const auto& sock : c2Sockets) {
            a0::ipc::UnixSocket::unlinkPath(sock);
        }
        return 0;
    }

    // --export-session: dump session log as JSON to stdout and exit
    if (!exportSessionId.empty()) {
        JsonLinesLogger logger("logs");
        std::string outPath = a0Dir + "/" + exportSessionId + ".json";
        if (logger.exportSession(exportSessionId, outPath)) {
            std::cout << "Exported session " << exportSessionId << " to " << outPath << std::endl;
        } else {
            std::cerr << "Session not found: " << exportSessionId << std::endl;
            return 1;
        }
        return 0;
    }

    a0::skills::SkillManager skillMgr(skillsDir, a0Dir + "/store", a0Dir + "/logs");
    SubprocessToolRunner toolRunner;
    DeepSeekProvider provider(apiKey);
    if (!mockUrl.empty())
        provider.setMockUrl(mockUrl);

    DefaultContextManager context;
    JsonLinesLogger logger;
    DefaultDependencyResolver depResolver(&skillMgr);
    DefaultSchemaInferenceEngine inferenceEngine(&provider);

    a0::docker::DockerContainerManager* containerMgr = nullptr;
    a0::docker::DockerComposeManager* composeMgr = nullptr;
    a0::docker::DockerToolRunnerImpl* dockerRunner = nullptr;

    bool noDocker = hasFlag(argc, argv, "--no-docker");
    if (!noDocker) {
        int idleTimeout = 300;
        int maxIdle = 10;
        std::string defaultImage = "ubuntu:22.04";

        std::string timeoutStr = getFlag(argc, argv, "--container-idle-timeout", "300");
        std::string maxIdleStr = getFlag(argc, argv, "--max-idle-containers", "10");
        defaultImage = getFlag(argc, argv, "--default-docker-image", "ubuntu:22.04");

        try { idleTimeout = std::stoi(timeoutStr); } catch (...) {}
        try { maxIdle = std::stoi(maxIdleStr); } catch (...) {}

        containerMgr = new a0::docker::DockerContainerManager(idleTimeout, maxIdle, defaultImage);
        composeMgr = new a0::docker::DockerComposeManager(idleTimeout);
        dockerRunner = new a0::docker::DockerToolRunnerImpl(containerMgr, composeMgr, !noContainerPool);
    }

    a0::SystemToolRegistry systemTools;

    // Persistence store (SQLite under .a0/db/)
    a0::persistence::SqliteStore persistence(a0Dir + "/db/sessions.db");

    DefaultSkillRunner skillRunner(&toolRunner, &provider, &skillMgr, &depResolver,
                                    &systemTools, dockerRunner, composeMgr);
    skillRunner.setSkillsDir(skillsDir);

    DefaultAgentCore core(&toolRunner, &skillRunner,
                          &provider, &context, &logger,
                          &depResolver, &inferenceEngine,
                          &systemTools, &skillMgr,
                          &persistence, dockerRunner, composeMgr);

    if (!resumeSessionId.empty()) {
        core.resumeSession(resumeSessionId);
    }

    if (!core.init(skillsDir)) {
        std::cerr << "Failed to initialize skills from: " << skillsDir << std::endl;
        return 1;
    }

    // ---- b1 auto-launch ----
    bool needsB1 = !noB1 && (terminalMode || (runPrompt.empty() && runSkillName.empty()));
    int b1Fd = -1;
    if (needsB1) {
        std::string b1SockPath = a0Dir + "/b1.sock";
        std::string b1PidPath = a0Dir + "/b1.pid";
        std::string cwd = ".";

        int existingPid = -1;
        std::ifstream pf(b1PidPath);
        if (pf) pf >> existingPid;
        bool alive = (existingPid > 0 && kill(existingPid, 0) == 0);

        if (!alive) {
            std::remove(b1SockPath.c_str());
            std::string b1Path = xSelfDir() + "/b1";
            pid_t pid = fork();
            if (pid == 0) {
                setsid();
                execlp(b1Path.c_str(), "b1", "--workdir", cwd.c_str(),
                       "--a0-dir", a0Dir.c_str(), nullptr);
                _exit(127);
            }
            for (int i = 0; i < 50; ++i) {
                if (access(b1SockPath.c_str(), F_OK) == 0) break;
                usleep(100000);
            }
        }

        a0::ipc::UnixSocket sock;
        if (sock.connect(b1SockPath, 2000) == 0) {
            a0::ipc::Message reg;
            reg.type = a0::ipc::MessageType::REGISTER;
            reg.pid = getpid();
            reg.sessionUuid = core.currentSessionId();
            if (a0::ipc::sendMessage(sock, reg) == 0) {
                b1Fd = sock.release();
                std::cerr << "a0: registered with b1" << std::endl;
            }
        }
    }

    // ---- terminal mode ----
    if (terminalMode && b1Fd >= 0) {
        // Create persistence store
        a0::persistence::SqliteStore persistence(a0Dir + "/db/sessions.db");

        // Create a session for this terminal
        a0::persistence::BuildFingerprint fp;
        fp.binarySha1 = "terminal";
        int agentId = persistence.registerAgent(fp);
        int64_t sessionId = persistence.createSession(0, 0, agentId);

        // PTY allocation
        int master = posix_openpt(O_RDWR | O_NOCTTY);
        if (master < 0) {
            std::cerr << "a0: terminal: posix_openpt failed\n";
            return 1;
        }
        grantpt(master);
        unlockpt(master);

        // Fork shell
        pid_t shellPid = fork();
        if (shellPid == 0) {
            setsid();
            int slave = open(ptsname(master), O_RDWR);
            if (slave < 0) _exit(1);
            dup2(slave, STDIN_FILENO);
            dup2(slave, STDOUT_FILENO);
            dup2(slave, STDERR_FILENO);
            if (slave > 2) close(slave);
            close(master);
            const char* shell = getenv("SHELL");
            if (!shell) shell = "/bin/bash";
            execl(shell, shell, "--login", nullptr);
            _exit(127);
        }

        if (shellPid < 0) {
            std::cerr << "a0: terminal: fork failed\n";
            return 1;
        }

        // Create stream record with terminalId for c2 DB discovery
        int64_t streamId = persistence.createStream(sessionId, "",
            "terminal", "host", "", ".", terminalId);

        // Send TERMINAL_READY via b1 socket
        {
            a0::ipc::UnixSocket b1Sock(b1Fd);
            a0::ipc::Message ready;
            ready.type = a0::ipc::MessageType::TERMINAL_READY;
            ready.streamId = streamId;
            ready.pid = getpid();
            ready.sessionUuid = "";
            ready.terminalId = terminalId;
            a0::ipc::sendMessage(b1Sock, ready);
            b1Sock.release();
        }

        // Streaming loop: read PTY master, persist + IPC
        {
            a0::ipc::UnixSocket b1Sock(b1Fd);
            char buf[4096];
            int seq = 0;
            std::atomic<bool> done{false};

            // Reader thread for PTY input from b1 (STREAM_INPUT)
            std::thread inputThread([&]() {
                while (!done) {
                    a0::ipc::Message inputMsg;
                    int rc = a0::ipc::recvMessage(b1Sock, inputMsg, 100);
                    if (rc == 0 && inputMsg.type == a0::ipc::MessageType::STREAM_INPUT
                        && inputMsg.streamId == streamId) {
                        write(master, inputMsg.chunkData.data(),
                              inputMsg.chunkData.size());
                    }
                    // rc == -2 (timeout) is normal — no input available
                    // rc < -2 is an actual error
                }
            });

            // Main thread: read PTY output and send via IPC
            fd_set rdfs;
            struct timeval tv;
            while (!done) {
                FD_ZERO(&rdfs);
                FD_SET(master, &rdfs);
                tv.tv_sec = 0;
                tv.tv_usec = 100000;

                if (select(master + 1, &rdfs, nullptr, nullptr, &tv) < 0) break;
                if (FD_ISSET(master, &rdfs)) {
                    ssize_t n = read(master, buf, sizeof(buf));
                    if (n > 0) {
                        std::string data(buf, static_cast<size_t>(n));
                        // Persist chunk
                        persistence.appendChunk(streamId, seq++, "stdout", data);
                        // Send via IPC
                        a0::ipc::Message chunk;
                        chunk.type = a0::ipc::MessageType::STREAM_DATA;
                        chunk.streamId = streamId;
                        chunk.chunkSeq = seq;
                        chunk.chunkDirection = "stdout";
                        chunk.chunkData = data;
                        a0::ipc::sendMessage(b1Sock, chunk);
                    } else {
                        done = true;
                    }
                }

                // Check if shell has exited
                int status;
                pid_t wpid = waitpid(shellPid, &status, WNOHANG);
                if (wpid == shellPid) {
                    done = true;
                }
            }

            done = true;
            int exitCode = 0;
            int status;
            if (waitpid(shellPid, &status, 0) > 0) {
                if (WIFEXITED(status)) exitCode = WEXITSTATUS(status);
            }
            persistence.endStream(streamId, exitCode);

            // Send STREAM_END
            a0::ipc::Message end;
            end.type = a0::ipc::MessageType::STREAM_END;
            end.streamId = streamId;
            end.pid = exitCode;
            a0::ipc::sendMessage(b1Sock, end);

            if (inputThread.joinable()) inputThread.join();
            b1Sock.release();
        }

        close(master);
        return 0;
    }

    // ---- --run mode (non-interactive) ----
    if (!runPrompt.empty() || !runSkillName.empty()) {
        json params;
        try { params = json::parse(runParamsStr); } catch (...) { params = json::object(); }
        if (!params.contains("prompt") && !runPrompt.empty()) {
            params["prompt"] = runPrompt;
        }

        std::string skillName = runSkillName;
        if (skillName.empty() && runPrompt.find(':') != std::string::npos) {
            // If --run text contains ':', try as qualified prompt name
            Prompt sp;
            if (skillMgr.getPrompt(runPrompt, sp) == 0) {
                skillName = runPrompt;
            }
        }

        json result;
        if (!skillName.empty()) {
            result = core.runSkill(skillName, params);
        } else {
            result = core.processGoal(runPrompt);
        }
        std::cout << result.dump() << std::endl;

        if (killAll) {
            killByPidFile(a0Dir + "/b1.pid");
            killByPidFile(getC2PidPath());
            std::vector<std::string> c2Socks;
            killByProcessName("c2", &c2Socks);
            killByProcessName("b1");
            a0::ipc::UnixSocket::unlinkPath(a0Dir + "/b1.sock");
            a0::ipc::UnixSocket::unlinkPath(getC2PidPath());
            for (const auto& s : c2Socks) a0::ipc::UnixSocket::unlinkPath(s);
        }
        delete dockerRunner;
        delete composeMgr;
        delete containerMgr;
        return 0;
    }

    // ---- interactive REPL ----
    core.run();

    if (killAll) {
        killByPidFile(a0Dir + "/b1.pid");
        killByPidFile(getC2PidPath());
        std::vector<std::string> c2Socks;
        killByProcessName("c2", &c2Socks);
        killByProcessName("b1");
        a0::ipc::UnixSocket::unlinkPath(a0Dir + "/b1.sock");
        a0::ipc::UnixSocket::unlinkPath(getC2PidPath());
        for (const auto& s : c2Socks) a0::ipc::UnixSocket::unlinkPath(s);
    }

    delete dockerRunner;
    delete composeMgr;
    delete containerMgr;
    return 0;
}
