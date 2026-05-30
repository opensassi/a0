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
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>
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
        a0::ipc::UnixSocket::unlinkPath(b1SockPath);
        a0::ipc::UnixSocket::unlinkPath(getC2PidPath());
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
    int b1Fd = -1;
    if (!noB1 && runPrompt.empty() && runSkillName.empty()) {
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
            a0::ipc::UnixSocket::unlinkPath(a0Dir + "/b1.sock");
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
        a0::ipc::UnixSocket::unlinkPath(a0Dir + "/b1.sock");
    }

    delete dockerRunner;
    delete composeMgr;
    delete containerMgr;
    return 0;
}
