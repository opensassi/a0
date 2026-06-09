#include "CLI11.hpp"
#include "bootstrap/a0_dir.h"
#include "persistence/sqlite_store.h"
#include "persistence/build_identity.h"
#include "persistence/sqlite_resource_provider.h"
#include "executor/system_handlers.h"
#include "skills/skills.h"
#include "llm/deepseek_provider.h"
#include "core/driven_core.h"

#include "executor/tool_runner.h"
#include "docker/container_manager.h"
#include "docker/compose_manager.h"
#include "docker/docker_tool_runner.h"
#include "executor/docker_security_filter.h"
#include "ipc/unix_socket.h"
#include "ipc/ipc_protocol.h"
#include "executor/stream_registry.h"
#include "shared/hex_session_id.h"
#include "bootstrap/session_context.h"
#include "core/app_core_thread.h"
#include "bootstrap/personas.h"
#include "tui/agent_tui.h"
#include "shared/daemonize.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <vector>
#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <filesystem>

using json = nlohmann::json;

static std::string g_a0LogFile;
static std::string g_a0LogDir;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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

static std::string xC2SocketFromProc(int pid) {
    std::string cmdlinePath = "/proc/" + std::to_string(pid) + "/cmdline";
    std::ifstream f(cmdlinePath, std::ios::binary);
    if (!f) return "";
    std::vector<char> buf(4096, 0);
    f.read(buf.data(), buf.size() - 1);
    f.close();
    const char* p = buf.data();
    while (p && *p) {
        std::string arg(p);
        if (arg == "--socket" || arg == "-s") {
            p += arg.size() + 1;
            if (p && *p) return std::string(p);
        }
        p += arg.size() + 1;
        if (p >= buf.data() + buf.size()) break;
    }
    return "";
}

static int killByProcessName(const std::string& name,
                              std::vector<std::string>* outSockets = nullptr) {
    std::string cmd = "pgrep -x " + name + " 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return 0;
    std::vector<int> pids;
    char line[64];
    while (fgets(line, sizeof(line), fp)) {
        int pid = atoi(line);
        if (pid > 0) pids.push_back(pid);
    }
    pclose(fp);
    for (int pid : pids) {
        if (outSockets && name == "c2") {
            std::string sock = xC2SocketFromProc(pid);
            if (!sock.empty()) outSockets->push_back(sock);
        }
    }
    int killed = 0;
    for (int pid : pids) {
        if (kill(pid, SIGTERM) == 0) ++killed;
    }
    if (killed > 0) {
        usleep(500000);
        for (int pid : pids) {
            if (kill(pid, 0) == 0) kill(pid, SIGKILL);
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

static std::string xChildLog(const std::string& parentLog, const std::string& suffix) {
    if (parentLog.empty()) return "";
    auto dot = parentLog.rfind('.');
    return (dot != std::string::npos)
        ? parentLog.substr(0, dot) + "-" + suffix + parentLog.substr(dot)
        : parentLog + "-" + suffix;
}

static std::string xMakeLogPath(const std::string& sessionId, pid_t pid,
                                 const std::string& suffix) {
    if (g_a0LogDir.empty()) return "";
    return g_a0LogDir + "/a0-" + sessionId + "-"
         + std::to_string(pid) + suffix + ".log";
}

static bool xRedirectStderr(const std::string& sessionId, pid_t pid) {
    if (!g_a0LogFile.empty()) {
        int fd = ::open(g_a0LogFile.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) return false;
        ::dup2(fd, STDERR_FILENO); ::close(fd);
        return true;
    }
    if (g_a0LogDir.empty() || sessionId.empty()) return false;

    std::string base = xMakeLogPath(sessionId, pid, "");
    int fd = ::open(base.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd >= 0) {
        ::dup2(fd, STDERR_FILENO); ::close(fd);
        return true;
    }

    if (errno == EEXIST) {
        for (int n = 1; n <= 99; ++n) {
            std::string p = base + "." + std::to_string(n);
            fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
            if (fd >= 0) {
                ::dup2(fd, STDERR_FILENO); ::close(fd);
                return true;
            }
            if (errno != EEXIST) break;
        }
    }
    return false;
}

static void xExportLogEnv(const std::string& sessionId) {
    if (!g_a0LogDir.empty())
        ::setenv("A0_LOG_DIR", g_a0LogDir.c_str(), 1);
    if (!sessionId.empty())
        ::setenv("A0_SESSION_ID", sessionId.c_str(), 1);
}

static std::string xAbsPath(const std::string& path) {
    if (path.empty() || path[0] == '/')
        return path;
    return std::filesystem::absolute(path).string();
}

static std::string getC2PidPath() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    return xdg ? std::string(xdg) + "/a0-c2.pid" : "/tmp/a0-c2.pid";
}

// ---------------------------------------------------------------------------
// Command: kill-all
// ---------------------------------------------------------------------------

static int cmdKillAll(const std::string& a0Dir) {
    std::string b1PidPath = a0Dir + "/b1.pid";
    std::string b1SockPath = a0Dir + "/b1.sock";
    killByPidFile(b1PidPath);
    killByPidFile(getC2PidPath());
    std::vector<std::string> c2Sockets;
    killByProcessName("c2", &c2Sockets);
    killByProcessName("b1");
    a0::ipc::UnixSocket::unlinkPath(b1SockPath);
    a0::ipc::UnixSocket::unlinkPath(getC2PidPath());
    for (const auto& sock : c2Sockets)
        a0::ipc::UnixSocket::unlinkPath(sock);
    return 0;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string epochToIso8601(int64_t epoch) {
    if (epoch <= 0) return "";
    char buf[32];
    time_t t = static_cast<time_t>(epoch);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return {buf};
}

// ---------------------------------------------------------------------------
// Command: session export
// ---------------------------------------------------------------------------

static int cmdSessionExport(const std::string& a0Dir,
                             const std::string& sessionId,
                             const std::string& outputPath,
                             bool outputJson) {
    if (sessionId.empty()) {
        if (outputJson)
            std::cout << "{\"error\":\"--session-id is required\"}\n";
        else
            std::cerr << "a0: --session-id is required for session export" << std::endl;
        return 1;
    }
    a0::persistence::SqliteStore db(a0Dir + "/db/sessions.db");
    int64_t sid = db.findSessionByUuid(sessionId);
    if (sid <= 0) {
        if (outputJson)
            std::cout << "{\"error\":\"session not found\",\"session_id\":\"" << sessionId << "\"}\n";
        else
            std::cerr << "Session not found: " << sessionId << std::endl;
        return 1;
    }
    auto messages = db.loadMessages(sid, -1);
    std::ostream* out = &std::cout;
    std::ofstream outFile;
    if (!outputPath.empty()) {
        outFile.open(outputPath);
        if (!outFile) {
            if (outputJson)
                std::cout << "{\"error\":\"cannot write\",\"path\":\"" << outputPath << "\"}\n";
            else
                std::cerr << "Cannot write: " << outputPath << std::endl;
            return 1;
        }
        out = &outFile;
    }
    // Emit system prompt + tool definitions as first record
    {
        std::string systemPrompt, toolDefs;
        if (db.loadSessionSystemPrompt(sid, systemPrompt, toolDefs) == 0) {
            json meta;
            meta["_meta"] = true;
            meta["system_prompt"] = systemPrompt;
            if (!toolDefs.empty()) {
                try { meta["tool_definitions"] = json::parse(toolDefs); }
                catch (...) { meta["tool_definitions"] = toolDefs; }
            }
            *out << meta.dump() << "\n";
        }
    }
    for (const auto& m : messages) {
        json j;
        j["role"] = m.role;
        j["content"] = m.content;
        j["sub_session_id"] = m.subSessionId;
        j["seq"] = m.seq;
        j["created_at"] = epochToIso8601(m.createdAt);
        if (!m.toolCallsJson.empty()) {
            try { j["tool_calls"] = json::parse(m.toolCallsJson); }
            catch (...) { j["tool_calls"] = m.toolCallsJson; }
        }
        if (!m.toolCallId.empty()) j["tool_call_id"] = m.toolCallId;
        if (!m.name.empty()) j["name"] = m.name;
        if (!m.resultJson.empty()) {
            try { j["result"] = json::parse(m.resultJson); }
            catch (...) { j["result"] = m.resultJson; }
        }
        *out << j.dump() << "\n";
    }
    if (outputJson && !outputPath.empty()) {
        json result;
        result["status"] = "ok";
        result["session_id"] = sessionId;
        result["output"] = outputPath;
        result["message_count"] = static_cast<int>(messages.size());
        std::cout << result.dump() << "\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Command: session list
// ---------------------------------------------------------------------------

static int cmdSessionList(const std::string& a0Dir,
                           int offset, int limit,
                           bool outputJson) {
    a0::persistence::SqliteStore db(a0Dir + "/db/sessions.db");

    int total = 0;
    {
        sqlite3* raw = static_cast<sqlite3*>(db.handle());
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(raw, "SELECT COUNT(*) FROM session", -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) total = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
    }

    auto sessionRows = db.loadSessions(offset + limit);
    std::vector<a0::persistence::PersistenceStore::SessionRow> displayRows;
    for (size_t i = static_cast<size_t>(offset);
         i < sessionRows.size() && displayRows.size() < static_cast<size_t>(limit);
         ++i) {
        displayRows.push_back(sessionRows[i]);
    }

    if (outputJson) {
        json arr = json::array();
        for (const auto& r : displayRows) {
            json j;
            j["uuid"] = r.uuid;
            j["started_at"] = epochToIso8601(r.startedAt);
            if (r.endedAt > 0)
                j["ended_at"] = epochToIso8601(r.endedAt);
            j["message_count"] = r.messageCount;
            arr.push_back(j);
        }
        json out;
        out["total"] = total;
        out["offset"] = offset;
        out["limit"] = limit;
        out["sessions"] = arr;
        std::cout << out.dump() << "\n";
    } else {
        if (displayRows.empty()) {
            std::cout << "No sessions found.\n";
            return 0;
        }
        std::cout << "Recent sessions:\n";
        for (const auto& r : displayRows) {
            std::cout << "  " << r.uuid
                      << "  " << epochToIso8601(r.startedAt)
                      << "  " << r.messageCount << " messages\n";
        }
        std::cout << "(Showing " << displayRows.size() << " of " << total << " total)\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Build minimal agent stack
// ---------------------------------------------------------------------------

static void xRegisterSystemHandlers(a0::skills::SkillManager& mgr) {
    mgr.registerHandler("system_bash_bash", [](const json& p, const a0::skills::HandlerContext&) { return a0::xBash(p); });
    mgr.registerHandler("system_fs_read", [](const json& p, const a0::skills::HandlerContext&) { return a0::xRead(p); });
    mgr.registerHandler("system_fs_glob", [](const json& p, const a0::skills::HandlerContext&) { return a0::xGlob(p); });
    mgr.registerHandler("system_fs_grep", [](const json& p, const a0::skills::HandlerContext&) { return a0::xGrep(p); });
    mgr.registerHandler("system_fs_edit", [](const json& p, const a0::skills::HandlerContext&) { return a0::xEdit(p); });
    mgr.registerHandler("system_fs_write", [](const json& p, const a0::skills::HandlerContext&) { return a0::xWrite(p); });

    mgr.registerHandler("system_git_*", [](const json& p, const a0::skills::HandlerContext& ctx) {
        return a0::xGitCommand(ctx.subcommand, p);
    });

    mgr.registerHandler("system_meta_show-skills", [&mgr](const json& p, const a0::skills::HandlerContext&) {
        return a0::xShowSkills(p, &mgr);
    });
    mgr.registerHandler("system_meta_show-skill-tools", [&mgr](const json& p, const a0::skills::HandlerContext&) {
        return a0::xShowSkillTools(p, &mgr);
    });
}

struct AgentStack {
    a0::persistence::SqliteStore persistence;
    a0::persistence::SqliteResourceProvider resourceProvider;
    a0::skills::SkillManager skillMgr;
    SubprocessToolRunner toolRunner;
    a0::DeepSeekProvider llmProvider;

    a0::docker::DockerContainerManager* containerMgr = nullptr;
    a0::docker::DockerComposeManager* composeMgr = nullptr;
    a0::docker::DockerToolRunnerImpl* dockerRunner = nullptr;
    a0::DockerSecurityFilter dockerFilter;

    AgentStack(const std::string& a0Dir, const std::string& skillsDir,
               const std::string& apiKey, const std::string& mockUrl,
               bool noDocker, bool noContainerPool,
               const std::string& idleTimeoutStr, const std::string& maxIdleStr,
               const std::string& defaultImage, int maxParallel = 4,
               const std::string& externalRepo = "https://github.com/opensassi/a0")
        : persistence(a0Dir + "/db/sessions.db")
        , resourceProvider(a0Dir + "/db/sessions.db")
        , skillMgr(skillsDir, a0Dir + "/store", &persistence)
        , llmProvider(apiKey)
    {
        if (!mockUrl.empty())
            llmProvider.setMockUrl(mockUrl);

        if (!noDocker) {
            int idleTimeout = 300;
            int maxIdle = 10;
            std::string img = defaultImage.empty() ? "ubuntu:22.04" : defaultImage;
            try { idleTimeout = std::stoi(idleTimeoutStr); } catch (...) {}
            try { maxIdle = std::stoi(maxIdleStr); } catch (...) {}
            containerMgr = new a0::docker::DockerContainerManager(idleTimeout, maxIdle, img);
            composeMgr = new a0::docker::DockerComposeManager(idleTimeout);
            dockerRunner = new a0::docker::DockerToolRunnerImpl(containerMgr, composeMgr, !noContainerPool);
        }

        const char* protectedEnv = std::getenv("A0_PROTECTED_CONTAINERS");
        if (protectedEnv) {
            std::string list(protectedEnv);
            size_t pos = 0;
            while ((pos = list.find(',')) != std::string::npos) {
                dockerFilter.protectContainer(list.substr(0, pos));
                list.erase(0, pos + 1);
            }
            if (!list.empty()) dockerFilter.protectContainer(list);
        }

        skillMgr.setToolRunner(&toolRunner);
        skillMgr.setResourceProvider(&resourceProvider);
        if (dockerRunner) skillMgr.setDockerRunner(dockerRunner);
        xRegisterSystemHandlers(skillMgr);

        // Task manager handlers
        auto* taskDb = &persistence;
        skillMgr.registerHandler("system_task-manager_add-task",
            [taskDb](const json& p, const a0::skills::HandlerContext& ctx) {
                json params = p;
                int64_t sid = 0;
                if (ctx.toolState) {
                    auto val = ctx.toolState->get("session_id");
                    if (val.is_number()) sid = val.get<int64_t>();
                }
                params["_session_id"] = sid;
                return a0::xAddTask(params, taskDb);
            });
        skillMgr.registerHandler("system_task-manager_remove-task",
            [taskDb](const json& p, const a0::skills::HandlerContext&) {
                return a0::xRemoveTask(p, taskDb);
            });
        skillMgr.registerHandler("system_task-manager_list-tasks",
            [taskDb](const json& p, const a0::skills::HandlerContext& ctx) {
                json params = p;
                int64_t sid = 0;
                if (ctx.toolState) {
                    auto val = ctx.toolState->get("session_id");
                    if (val.is_number()) sid = val.get<int64_t>();
                }
                params["_session_id"] = sid;
                return a0::xListTasks(params, taskDb);
            });
        skillMgr.registerHandler("system_task-manager_set-task-priority",
            [taskDb](const json& p, const a0::skills::HandlerContext&) {
                return a0::xSetTaskPriority(p, taskDb);
            });
    }

    ~AgentStack() {
        delete dockerRunner;
        delete composeMgr;
        delete containerMgr;
    }
};

// ---------------------------------------------------------------------------
// Helper: ensure an agent row exists in persistence, returns agentId
// ---------------------------------------------------------------------------

static int xRegisterAgent(a0::persistence::PersistenceStore& store) {
    a0::persistence::BuildFingerprint fp;
    fp.binarySha1 = a0::persistence::BuildIdentity::binarySha1();
    a0::persistence::BuildIdentity::detectGit(".", fp);
    return store.registerAgent(fp);
}

// ---------------------------------------------------------------------------
// Command: run (one-shot)
// ---------------------------------------------------------------------------

static int cmdRun(const std::string& a0Dir, const std::string& skillsDir,
                  const std::string& apiKey, const std::string& mockUrl,
                  const std::string& resumeSessionId,
                  const std::string& runPrompt, const std::string& runSkillName,
                  const std::string& runParamsStr,
                  bool noDocker, bool noContainerPool,
                  const std::string& idleTimeoutStr,
                  const std::string& maxIdleStr,
                  const std::string& defaultImage,
                  int maxParallel = 4,
                  const std::string& externalRepo = "https://github.com/opensassi/a0",
                  const std::unordered_map<std::string, std::string>& skillArgs = {},
                  const std::string& personaName = "") {
    AgentStack stack(a0Dir, skillsDir, apiKey, mockUrl, noDocker, noContainerPool,
                     idleTimeoutStr, maxIdleStr, defaultImage, maxParallel, externalRepo);

    // resolve skill args into tool state
    for (const auto& [key, val] : skillArgs)
        stack.skillMgr.toolState().set("args:system-" + key, val);

    // Establish session ID early for log redirect
    std::string sid;
    if (!resumeSessionId.empty())
        sid = resumeSessionId;
    else
        sid = generateHexSessionId();

    if (!g_a0LogFile.empty() || !g_a0LogDir.empty()) {
        if (!xRedirectStderr(sid, getpid())) {
            std::cerr << "FATAL: "
                      << (g_a0LogFile.empty() ? "--log-dir" : "--log-file")
                      << "=" << (g_a0LogFile.empty() ? g_a0LogDir : g_a0LogFile)
                      << " unusable" << std::endl;
            return 1;
        }
    }

    // Load skills
    if (stack.skillMgr.loadAll() != 0) {
        std::cerr << "Failed to initialize skills from: " << skillsDir << std::endl;
        return 1;
    }

    // Load persona skills/tools
    std::vector<std::string> personaSkills, personaTools;
    if (!personaName.empty()) {
        a0::personas::PersonaLoader pl;
        if (pl.loadAll() == 0) {
            auto p = pl.getPersona(personaName);
            if (p) {
                personaSkills = p->manifest.skills;
                personaTools = p->manifest.tools;
            }
        }
    }

    // Create session
    int agentId = xRegisterAgent(stack.persistence);
    int64_t sessionDbId = stack.persistence.createSession(sid, 0, 0, agentId);
    stack.skillMgr.setRecordingSession(sessionDbId);

    // Store session id for task manager handlers and create root task
    stack.skillMgr.toolState().set("session_id", sessionDbId);
    stack.persistence.createSessionRootTask(sessionDbId);

    // Build AppCoreThread
    auto [cmdSender, cmdRcvr] = a0::mpsc::Channel<a0::mpsc::Command>::create();
    auto [evtSender, evtRcvr] = a0::mpsc::Channel<a0::mpsc::AppCoreEvent>::create();

    a0::AppCoreThread coreThread(apiKey, mockUrl.empty() ? "deepseek-v4-flash" : "mock",
                                 &stack.skillMgr, &stack.persistence,
                                 &stack.resourceProvider,
                                 256, 4096, 4096,
                                 personaName, personaSkills, personaTools);
    if (!mockUrl.empty())
        coreThread.setMockUrl(mockUrl);
    coreThread.start(std::move(cmdRcvr), std::move(evtSender));

    cmdSender.send(a0::mpsc::SetSession{sessionDbId, sid});
    cmdSender.send(a0::mpsc::SubmitGoal{runPrompt});

    // Poll for completion
    std::string result;
    while (result.empty()) {
        auto events = evtRcvr.drain();
        for (auto& ev : events) {
            if (std::holds_alternative<a0::mpsc::Complete>(ev))
                result = std::get<a0::mpsc::Complete>(ev).summary;
            else if (std::holds_alternative<a0::mpsc::Error>(ev))
                result = "ERROR: " + std::get<a0::mpsc::Error>(ev).message;
        }
        if (result.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    cmdSender.send(a0::mpsc::Shutdown{});
    coreThread.stop();

    json out = json::object();
    out["status"] = "ok";
    out["session_id"] = sid;
    try {
        json parsed = json::parse(result);
        out["result"] = parsed;
    } catch (...) {
        out["result"] = result;
    }
    std::cout << out.dump() << std::endl;
    return 0;
}

// ---------------------------------------------------------------------------
// Command: tui (interactive, default)
// ---------------------------------------------------------------------------

static int cmdTui(const std::string& a0Dir, const std::string& skillsDir,
                  const std::string& apiKey, const std::string& mockUrl,
                  const std::string& resumeSessionId,
                  bool noDocker, bool noContainerPool, bool noB1,
                  const std::string& idleTimeoutStr,
                  const std::string& maxIdleStr,
                  const std::string& defaultImage,
                  int maxParallel = 4,
                  const std::string& externalRepo = "https://github.com/opensassi/a0",
                  const std::unordered_map<std::string, std::string>& skillArgs = {},
                  const std::string& tuiResumeUuid = "",
                  bool tuiNoPermissions = false,
                  bool tuiTestMode = false,
                  const std::string& personaName = "") {
    AgentStack stack(a0Dir, skillsDir, apiKey, mockUrl, noDocker, noContainerPool,
                     idleTimeoutStr, maxIdleStr, defaultImage, maxParallel, externalRepo);

    for (const auto& [key, val] : skillArgs)
        stack.skillMgr.toolState().set("args:system-" + key, val);

    // Establish session ID early for log redirect
    std::string sid;
    if (!resumeSessionId.empty())
        sid = resumeSessionId;
    else
        sid = generateHexSessionId();

    if (!g_a0LogFile.empty() || !g_a0LogDir.empty()) {
        if (!xRedirectStderr(sid, getpid())) {
            std::cerr << "FATAL: "
                      << (g_a0LogFile.empty() ? "--log-dir" : "--log-file")
                      << "=" << (g_a0LogFile.empty() ? g_a0LogDir : g_a0LogFile)
                      << " unusable" << std::endl;
            return 1;
        }
        xExportLogEnv(sid);
    }

    // Load skills
    if (stack.skillMgr.loadAll() != 0) {
        std::cerr << "Failed to initialize skills from: " << skillsDir << std::endl;
        return 1;
    }

    // Load persona skills/tools
    std::vector<std::string> personaSkills, personaTools;
    if (!personaName.empty()) {
        a0::personas::PersonaLoader pl;
        if (pl.loadAll() == 0) {
            auto p = pl.getPersona(personaName);
            if (p) {
                personaSkills = p->manifest.skills;
                personaTools = p->manifest.tools;
            }
        }
    }

    // Register agent and create session
    int agentId = xRegisterAgent(stack.persistence);
    int64_t sessionDbId = stack.persistence.createSession(sid, 0, 0, agentId);
    stack.skillMgr.setRecordingSession(sessionDbId);
    stack.skillMgr.toolState().set("session_id", sessionDbId);
    stack.persistence.createSessionRootTask(sessionDbId);

    // Capture original CWD
    char cwdBuf[4096];
    std::string initialCwd = ::getcwd(cwdBuf, sizeof(cwdBuf)) ? cwdBuf : ".";

    std::unique_ptr<a0::SessionContext> sessionCtx;
    // For non-resumed sessions, init SessionContext for container prefix etc.
    if (resumeSessionId.empty()) {
        sessionCtx = std::make_unique<a0::SessionContext>(
            initialCwd, a0Dir, sid, sessionDbId, &stack.persistence);
        sessionCtx->init(&stack.skillMgr);

        if (stack.containerMgr) {
            stack.containerMgr->setSessionPrefix(sid.substr(0, 8));
        }
    } else {
        sessionCtx = a0::SessionContext::loadFromDb(
            sessionDbId, a0Dir, &stack.persistence);
        if (sessionCtx) {
            sessionCtx->restore(&stack.skillMgr);
            if (stack.containerMgr && !sessionCtx->gitInfo().currentBranch.empty()) {
                stack.containerMgr->setSessionPrefix(sid.substr(0, 8));
            }
        }
    }

    bool needsB1 = !noB1;
    int b1Fd = -1;
    if (needsB1) {
        std::string b1SockPath = a0Dir + "/b1.sock";
        std::string b1PidPath = a0Dir + "/b1.pid";

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
                std::string b1Log;
                if (!g_a0LogFile.empty())
                    b1Log = xChildLog(g_a0LogFile, "b1");
                else if (!g_a0LogDir.empty() && !sid.empty())
                    b1Log = xMakeLogPath(sid, getpid(), "-b1");
                a0::xDaemonizeChild(b1Log);
                if (!b1Log.empty()) {
                    execlp(b1Path.c_str(), "b1", "--workdir", initialCwd.c_str(),
                           "--a0-dir", a0Dir.c_str(), "--log-file", b1Log.c_str(), nullptr);
                } else {
                    execlp(b1Path.c_str(), "b1", "--workdir", initialCwd.c_str(),
                           "--a0-dir", a0Dir.c_str(), nullptr);
                }
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
            reg.sessionUuid = sid;
            if (a0::ipc::sendMessage(sock, reg) == 0) {
                b1Fd = sock.release();
                std::cerr << "a0: registered with b1" << std::endl;
            }
        }
    }

    // Create MPSC channels
    auto [cmdSender, cmdRcvr] = a0::mpsc::Channel<a0::mpsc::Command>::create();
    auto [evtSender, evtRcvr] = a0::mpsc::Channel<a0::mpsc::AppCoreEvent>::create();

    // Start core thread
    a0::AppCoreThread coreThread(apiKey, mockUrl.empty() ? "deepseek-v4-flash" : "mock",
                                 &stack.skillMgr, &stack.persistence,
                                 &stack.resourceProvider,
                                 256, 4096, 4096,
                                 personaName, personaSkills, personaTools);
    if (!mockUrl.empty())
        coreThread.setMockUrl(mockUrl);

    // Start core thread (no wakeup yet — screen not created)
    coreThread.start(std::move(cmdRcvr), std::move(evtSender), nullptr);

    // Send session to core
    cmdSender.send(a0::mpsc::SetSession{sessionDbId, sid});

    // Launch TUI (no core references)
    a0::tui::AgentTui tui(std::move(cmdSender), std::move(evtRcvr),
                           [&b1Fd]() { return b1Fd >= 0; }, tuiTestMode);

    // Wire wakeup — screen is created inside tui.run(), capture pointer for later
    coreThread.setWakeupFn([tuiPtr = &tui]() {
        if (auto* s = tuiPtr->screenPtr()) s->Post(ftxui::Task{[] {}});
    });

    // Resume session if requested (sends ResumeSession via MPSC)
    if (!tuiResumeUuid.empty())
        tui.cmdSender().send(a0::mpsc::ResumeSession{tuiResumeUuid});

    int rc = tui.run();

    // Cleanup
    tui.cmdSender().send(a0::mpsc::Shutdown{});
    coreThread.stop();
    return rc;
}

// ---------------------------------------------------------------------------
// Command: terminal
// ---------------------------------------------------------------------------

static int cmdTerminal(const std::string& a0Dir, const std::string& terminalId, const std::string& cwd) {
    std::string workdir = cwd;
    if (workdir.empty() || workdir == ".") {
        char buf[4096];
        workdir = getcwd(buf, sizeof(buf)) ? buf : ".";
    } else {
        char resolved[4096];
        if (realpath(workdir.c_str(), resolved))
            workdir = resolved;
    }

    if (chdir(workdir.c_str()) != 0) {
        std::cerr << "a0: terminal: chdir(" << workdir << ") failed: "
                  << strerror(errno) << "\n";
    }

    a0::persistence::SqliteStore persistence(a0Dir + "/db/sessions.db");

    a0::persistence::BuildFingerprint fp;
    fp.binarySha1 = "terminal";
    int agentId = persistence.registerAgent(fp);
    std::string sessionUuid = generateHexSessionId();
    int64_t sessionId = persistence.createSession(sessionUuid, 0, 0, agentId);

    std::string b1SockPath = a0Dir + "/b1.sock";
    std::string b1PidPath = a0Dir + "/b1.pid";
    {
        int existingPid = -1;
        std::ifstream pf(b1PidPath);
        if (pf) pf >> existingPid;
        bool alive = (existingPid > 0 && kill(existingPid, 0) == 0);
        if (!alive) {
            std::remove(b1SockPath.c_str());
            std::string b1Path = xSelfDir() + "/b1";
            pid_t b1Pid = fork();
            if (b1Pid == 0) {
                setsid();
                std::string b1Log;
                if (!g_a0LogFile.empty())
                    b1Log = xChildLog(g_a0LogFile, "b1");
                else if (!g_a0LogDir.empty())
                    b1Log = xMakeLogPath(sessionUuid, getpid(), "-b1");
                a0::xDaemonizeChild(b1Log);
                if (!b1Log.empty()) {
                    execlp(b1Path.c_str(), "b1", "--workdir", workdir.c_str(),
                           "--a0-dir", a0Dir.c_str(), "--log-file", b1Log.c_str(), nullptr);
                } else {
                    execlp(b1Path.c_str(), "b1", "--workdir", workdir.c_str(),
                           "--a0-dir", a0Dir.c_str(), nullptr);
                }
                _exit(127);
            }
            if (b1Pid < 0) {
                std::cerr << "a0: terminal: fork for b1 failed\n";
                return 1;
            }
            for (int i = 0; i < 50; ++i) {
                if (access(b1SockPath.c_str(), F_OK) == 0) break;
                usleep(100000);
            }
        }
    }

    a0::ipc::UnixSocket b1RawSock;
    if (b1RawSock.connect(b1SockPath, 2000) != 0) {
        std::cerr << "a0: terminal: cannot connect to b1\n";
        return 1;
    }
    a0::ipc::BufferedSocket b1Sock(b1RawSock.release());

    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) {
        std::cerr << "a0: terminal: posix_openpt failed\n";
        return 1;
    }
    grantpt(master);
    unlockpt(master);

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

    int64_t streamId = persistence.createStream(sessionId, "",
        "terminal", "host", "", ".", terminalId);

    {
        a0::ipc::Message ready;
        ready.type = a0::ipc::MessageType::TERMINAL_READY;
        ready.streamId = streamId;
        ready.pid = getpid();
        ready.terminalId = terminalId;
        b1Sock.send(ready);
    }

    {
        char buf[4096];
        int seq = 0;
        std::atomic<bool> done{false};

        std::thread inputThread([&]() {
            while (!done) {
                a0::ipc::Message inputMsg;
                int rc = b1Sock.recv(inputMsg, 100);
                if (rc == a0::ipc::RECV_OK
                    && inputMsg.type == a0::ipc::MessageType::STREAM_INPUT
                    && inputMsg.streamId == streamId) {
                    write(master, inputMsg.chunkData.data(), inputMsg.chunkData.size());
                }
            }
        });

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
                    persistence.appendChunk(streamId, seq++, "stdout", data);
                    a0::ipc::Message chunk;
                    chunk.type = a0::ipc::MessageType::STREAM_DATA;
                    chunk.streamId = streamId;
                    chunk.chunkSeq = seq;
                    chunk.chunkDirection = "stdout";
                    chunk.chunkData = data;
                    b1Sock.send(chunk);
                } else {
                    done = true;
                }
            }

            int status;
            pid_t wpid = waitpid(shellPid, &status, WNOHANG);
            if (wpid == shellPid) done = true;
        }

        done = true;
        int exitCode = 0;
        int status;
        if (waitpid(shellPid, &status, 0) > 0) {
            if (WIFEXITED(status)) exitCode = WEXITSTATUS(status);
        }
        persistence.endStream(streamId, exitCode);

        a0::ipc::Message end;
        end.type = a0::ipc::MessageType::STREAM_END;
        end.streamId = streamId;
        end.pid = exitCode;
        b1Sock.send(end);

        if (inputThread.joinable()) inputThread.join();
    }

    close(master);
    return 0;
}

// ---------------------------------------------------------------------------
// Main — CLI11 dispatch
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    CLI::App app{"a0 - agent orchestrator"};

    // Global flags
    std::string a0Dir = "./.a0";
    std::string envFilePath = ".env";
    std::string logFile;
    std::string logDir;
    app.add_option("--a0-dir", a0Dir, ".a0 state root (default ./.a0)");
    app.add_option("--env-file", envFilePath, ".env file path (default ./.env)");
    app.add_option("--log-file", logFile, "Redirect stderr to file");
    app.add_option("--log-dir", logDir, "Log directory -- auto-names a0-<sessionId>-<pid>.log");

    std::string apiKey, mockUrl, skillsDir = "./skills", resumeSessionId;
    std::string personaName = "software-engineer";
    std::string idleTimeoutStr = "300", maxIdleStr = "10", defaultImage = "ubuntu:22.04";
    bool noDocker = false, noContainerPool = false, noB1 = false, outputJson = false;
    app.add_flag("--output-json", outputJson, "Output results as JSON");
    app.add_option("--api-key", apiKey, "DeepSeek API key");
    app.add_option("--mock-api", mockUrl, "Mock API URL");
    app.add_option("--skills-dir", skillsDir, "Skills root directory");
    app.add_option("--resume", resumeSessionId, "Resume session UUID");
    app.add_option("--persona", personaName, "Persona name (default: software-engineer)");
    app.add_flag("--no-docker", noDocker, "Disable Docker integration");
    app.add_flag("--no-container-pool", noContainerPool, "Disable container pooling");
    app.add_flag("--no-b1", noB1, "Skip b1 supervisor launch");
    app.add_option("--container-idle-timeout", idleTimeoutStr, "Container idle timeout (s)");
    app.add_option("--max-idle-containers", maxIdleStr, "Max idle containers");
    int maxParallel = 4;
    app.add_option("--max-parallel", maxParallel, "Max concurrent tool executions (default 4)");
    app.add_option("--default-docker-image", defaultImage, "Default Docker image");
    std::string externalRepo = "https://github.com/opensassi/a0";
    app.add_option("--external-repo", externalRepo, "External a0 repo URL for self-development scripts");
    std::vector<std::string> skillArgsRaw;
    app.add_option("--skill-arg", skillArgsRaw, "Skill argument (repeatable): --skill-arg=playwright-headless=true")->take_all();

    // Subcommand: kill-all
    auto* killCmd = app.add_subcommand("kill-all", "Stop daemon processes");

    // Subcommand: session {export, list}
    std::string exportSessionId, exportOutputPath;
    int sessionListOffset = 0, sessionListLimit = 10;
    auto* sessionCmd = app.add_subcommand("session", "Session operations");
    auto* sessionExportCmd = sessionCmd->add_subcommand("export", "Export session as JSONL");
    sessionExportCmd->add_flag("--output-json", outputJson, "Output results as JSON");
    sessionExportCmd->add_option("--session-id", exportSessionId)->required();
    sessionExportCmd->add_option("--output", exportOutputPath, "Output file (default: stdout)");
    auto* sessionListCmd = sessionCmd->add_subcommand("list", "List recent sessions");
    sessionListCmd->add_flag("--output-json", outputJson, "Output results as JSON");
    sessionListCmd->add_option("--offset", sessionListOffset, "Pagination offset");
    sessionListCmd->add_option("--limit", sessionListLimit, "Max results");

    // Subcommand: run
    std::string runPrompt, runSkillName, runParamsStr = "{}";
    auto* runCmd = app.add_subcommand("run", "Execute a skill or prompt and exit");
    runCmd->add_option("--api-key", apiKey);
    runCmd->add_option("--mock-api", mockUrl);
    runCmd->add_option("--skills-dir", skillsDir);
    runCmd->add_option("--skill", runSkillName, "Qualified skill name");
    runCmd->add_option("--params", runParamsStr, "JSON parameters");
    runCmd->add_flag("--no-container-pool", noContainerPool);
    runCmd->add_option("--container-idle-timeout", idleTimeoutStr);
    runCmd->add_option("--max-idle-containers", maxIdleStr);
    runCmd->add_option("--default-docker-image", defaultImage);
    runCmd->add_option("--resume", resumeSessionId);
    runCmd->add_option("prompt", runPrompt, "Prompt text (positional)");

    // Subcommand: tui
    std::string tuiResumeUuid;
    bool tuiNoPermissions = false;
    bool tuiTestMode = false;
    auto* tuiCmd = app.add_subcommand("tui", "Interactive terminal UI (default)");
    tuiCmd->add_option("--resume", tuiResumeUuid, "Resume session by UUID");
    tuiCmd->add_flag("--no-permissions", tuiNoPermissions, "Auto-approve tool execution");
    tuiCmd->add_flag("--test-mode", tuiTestMode, "Use FixedSize screen for testing");

    // Subcommand: terminal
    std::string terminalId;
    std::string terminalCwd;
    auto* termCmd = app.add_subcommand("terminal", "PTY terminal session");
    termCmd->add_option("--cwd", terminalCwd, "Working directory for the terminal shell");
    termCmd->add_option("--terminal-id", terminalId, "Terminal identifier");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    // Parse skill args into a map
    std::unordered_map<std::string, std::string> skillArgs;
    for (const auto& raw : skillArgsRaw) {
        auto eq = raw.find('=');
        if (eq == std::string::npos) {
            skillArgs[raw] = "true";
        } else {
            skillArgs[raw.substr(0, eq)] = raw.substr(eq + 1);
        }
    }

    // Load env file
    loadEnvFile(envFilePath);

    // Resolve API key from env if not set
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

    // Ensure .a0 directory
    if (!app.got_subcommand(killCmd)) {
        int r = a0::ensureA0Dir(a0Dir, !resumeSessionId.empty());
        if (r < 0) {
            std::cerr << "a0: fatal: could not create " << a0Dir << std::endl;
            return 1;
        }
        if (a0Dir[0] != '/')
            a0Dir = xAbsPath(a0Dir);
    }

    // Resolve log settings
    g_a0LogFile = logFile;
    g_a0LogDir = logDir;
    if (!g_a0LogDir.empty()) {
        g_a0LogDir = xAbsPath(g_a0LogDir);
        ::mkdir(g_a0LogDir.c_str(), 0755);
    } else if (g_a0LogFile.empty()) {
        g_a0LogDir = a0Dir + "/logs";
        ::mkdir(g_a0LogDir.c_str(), 0755);
    }

    // Dispatch by subcommand
    if (app.got_subcommand(killCmd))
        return cmdKillAll(a0Dir);
    if (app.got_subcommand(sessionCmd)) {
        if (sessionCmd->got_subcommand(sessionExportCmd))
            return cmdSessionExport(a0Dir, exportSessionId, exportOutputPath, outputJson);
        if (sessionCmd->got_subcommand(sessionListCmd))
            return cmdSessionList(a0Dir, sessionListOffset, sessionListLimit, outputJson);
        std::cerr << "a0: session requires a subcommand (export|list)" << std::endl;
        return 1;
    }
    if (app.got_subcommand(runCmd))
        return cmdRun(a0Dir, skillsDir, apiKey, mockUrl, resumeSessionId,
                      runPrompt, runSkillName, runParamsStr,
                      noDocker, noContainerPool, idleTimeoutStr, maxIdleStr,
                      defaultImage, maxParallel, externalRepo, skillArgs, personaName);
    if (app.got_subcommand(termCmd))
        return cmdTerminal(a0Dir, terminalId, terminalCwd);
    if (app.got_subcommand(tuiCmd))
        return cmdTui(a0Dir, skillsDir, apiKey, mockUrl, resumeSessionId,
                      noDocker, noContainerPool, noB1,
                      idleTimeoutStr, maxIdleStr, defaultImage, maxParallel, externalRepo, skillArgs,
                      tuiResumeUuid, tuiNoPermissions, tuiTestMode, personaName);

    // Default: tui
    return cmdTui(a0Dir, skillsDir, apiKey, mockUrl, resumeSessionId,
                  noDocker, noContainerPool, noB1,
                  idleTimeoutStr, maxIdleStr, defaultImage, maxParallel, externalRepo, skillArgs,
                  tuiResumeUuid, tuiNoPermissions, tuiTestMode, personaName);
}
