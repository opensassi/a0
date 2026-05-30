#include "dashboard_server.h"
#include "b1_registry.h"
#include "sse_manager.h"
#include "c2_event_store.h"
#include "c2_listener.h"
#include "ipc_protocol.h"
#include "nlohmann/json.hpp"
#include <App.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/stat.h>
#include <sqlite3.h>
#include <unistd.h>
#include <cstdlib>

namespace a0::c2 {

DashboardServer::DashboardServer(int port, B1Registry* registry, SseManager* sse,
                                  EventStore* events, C2Listener* listener,
                                  const std::string& webRoot,
                                  const std::string& sslKey, const std::string& sslCert)
    : m_port(port)
    , m_registry(registry)
    , m_sse(sse)
    , m_events(events)
    , m_listener(listener)
    , m_webRoot(webRoot)
    , m_sslKey(sslKey)
    , m_sslCert(sslCert)
{
}

DashboardServer::~DashboardServer() {
    shutdown();
}

std::string DashboardServer::xBuildStatusJson() {
    auto b1s = m_registry->listB1s();
    nlohmann::json j = nlohmann::json::array();
    for (const auto& b1 : b1s) {
        nlohmann::json entry;
        entry["pid"] = b1.pid;
        entry["workdir"] = b1.workdir;
        entry["hostname"] = b1.hostname;
        nlohmann::json agents = nlohmann::json::array();
        for (const auto& ag : b1.agents) {
            nlohmann::json a;
            a["pid"] = ag.pid;
            a["session"] = ag.sessionUuid;
            a["state"] = ag.state;
            agents.push_back(a);
        }
        entry["agents"] = agents;
        j.push_back(entry);
    }
    return j.dump(2);
}

std::string DashboardServer::xBuildStatsJson() {
    int totalB1s, totalAgents, crashedCount;
    m_registry->getStats(totalB1s, totalAgents, crashedCount);
    nlohmann::json j;
    j["totalB1s"] = totalB1s;
    j["totalAgents"] = totalAgents;
    j["crashedCount"] = crashedCount;
    return j.dump(2);
}

std::string DashboardServer::xBuildPendingJson() {
    auto pending = m_events->listPending();
    nlohmann::json j = nlohmann::json::array();
    for (const auto& p : pending) {
        nlohmann::json entry;
        entry["session"] = p.session;
        entry["toolCallId"] = p.toolCallId;
        entry["prompt"] = p.prompt;
        entry["context"] = p.context;
        entry["createdAt"] = p.createdAt;
        j.push_back(entry);
    }
    return j.dump(2);
}

std::string DashboardServer::xMimeType(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot);
    if (ext == ".html") return "text/html";
    if (ext == ".js")   return "text/javascript";
    if (ext == ".css")  return "text/css";
    if (ext == ".json") return "application/json";
    if (ext == ".svg")  return "image/svg+xml";
    if (ext == ".png")  return "image/png";
    if (ext == ".ico")  return "image/x-icon";
    return "application/octet-stream";
}

std::string DashboardServer::xReadFile(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return {};
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::string content;
    content.resize(st.st_size);
    f.read(content.data(), st.st_size);
    return content;
}

template<typename Res>
void DashboardServer::xServeStatic(Res* res, const std::string& urlPath) {
    std::string path = urlPath;
    auto qpos = path.find('?');
    if (qpos != std::string::npos) path = path.substr(0, qpos);

    if (path.empty() || path == "/") path = "/index.html";

    std::string filePath = m_webRoot + path;
    std::string content = xReadFile(filePath);

    if (content.empty()) {
        content = xReadFile(m_webRoot + "/index.html");
        if (content.empty()) {
            res->writeStatus("404 Not Found")->end("Not Found");
            return;
        }
        res->writeHeader("Content-Type", "text/html");
    } else {
        res->writeHeader("Content-Type", xMimeType(path));
    }
    res->end(content);
}

template<typename App>
void DashboardServer::xSetupRoutes(App* app) {
    // GET /api/status
    app->template get("/api/status", [this](auto* res, auto* /*req*/) {
        res->writeHeader("Content-Type", "application/json");
        res->end(xBuildStatusJson());
    });

    // GET /api/stats
    app->template get("/api/stats", [this](auto* res, auto* /*req*/) {
        res->writeHeader("Content-Type", "application/json");
        res->end(xBuildStatsJson());
    });

    // GET /api/events/pending
    app->template get("/api/events/pending", [this](auto* res, auto* /*req*/) {
        res->writeHeader("Content-Type", "application/json");
        res->end(xBuildPendingJson());
    });

    // GET /api/events (SSE stream)
    app->template get("/api/events", [this](auto* res, auto* /*req*/) {
        res->writeHeader("Content-Type", "text/event-stream");
        res->writeHeader("Cache-Control", "no-cache");
        res->writeHeader("Connection", "keep-alive");

        auto sendFn = [res](const std::string& data) {
            res->write(std::string_view(data));
        };
        int id = m_sse->addClient(std::move(sendFn));
        res->onAborted([this, id]() {
            m_sse->removeClient(id);
        });
    });

    // POST /api/ping
    app->template post("/api/ping", [this](auto* res, auto* /*req*/) {
        std::string buffer;
        res->onAborted([res]() {});
        res->onData([this, res, buffer = std::move(buffer)](std::string_view data, bool last) mutable {
            buffer += data;
            if (!last) return;

            nlohmann::json echo;
            try { echo = nlohmann::json::parse(buffer); } catch (...) {}
            nlohmann::json pong;
            pong["echo"] = echo;
            pong["serverTime"] = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            m_sse->broadcast("pong", pong.dump());
            res->writeHeader("Content-Type", "application/json");
            res->end(pong.dump());
        });
    });

    // GET /api/b1/:pid
    app->template get("/api/b1/:pid", [this](auto* res, auto* req) {
        std::string pidStr(req->getParameter(0));
        int pid = std::stoi(pidStr);
        auto b1s = m_registry->listB1s();
        for (const auto& b1 : b1s) {
            if (b1.pid == pid) {
                nlohmann::json entry;
                entry["pid"] = b1.pid;
                entry["workdir"] = b1.workdir;
                entry["hostname"] = b1.hostname;
                nlohmann::json agents = nlohmann::json::array();
                for (const auto& ag : b1.agents) {
                    nlohmann::json a;
                    a["pid"] = ag.pid;
                    a["session"] = ag.sessionUuid;
                    a["state"] = ag.state;
                    agents.push_back(a);
                }
                entry["agents"] = agents;
                res->writeHeader("Content-Type", "application/json");
                res->end(entry.dump(2));
                return;
            }
        }
        res->writeStatus("404 Not Found")->end("{\"error\":\"b1 not found\"}");
    });

    // GET /api/b1/:pid/agents
    app->template get("/api/b1/:pid/agents", [this](auto* res, auto* req) {
        std::string pidStr(req->getParameter(0));
        int pid = std::stoi(pidStr);
        auto b1s = m_registry->listB1s();
        for (const auto& b1 : b1s) {
            if (b1.pid == pid) {
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& ag : b1.agents) {
                    nlohmann::json a;
                    a["pid"] = ag.pid;
                    a["session"] = ag.sessionUuid;
                    a["state"] = ag.state;
                    arr.push_back(a);
                }
                res->writeHeader("Content-Type", "application/json");
                res->end(arr.dump(2));
                return;
            }
        }
        res->writeStatus("404")->end("[]");
    });

    // GET /api/agent/:uuid
    app->template get("/api/agent/:uuid", [this](auto* res, auto* req) {
        std::string uuid(req->getParameter(0));
        auto b1s = m_registry->listB1s();
        for (const auto& b1 : b1s) {
            for (const auto& ag : b1.agents) {
                if (ag.sessionUuid == uuid) {
                    nlohmann::json j;
                    j["session"] = ag.sessionUuid;
                    j["pid"] = ag.pid;
                    j["state"] = ag.state;
                    j["b1Pid"] = b1.pid;
                    j["b1Workdir"] = b1.workdir;
                    j["hostname"] = b1.hostname;
                    res->writeHeader("Content-Type", "application/json");
                    res->end(j.dump(2));
                    return;
                }
            }
        }
        res->writeStatus("404 Not Found")->end("{\"error\":\"agent not found\"}");
    });

    // POST /api/agent/:uuid/messages
    app->template post("/api/agent/:uuid/messages", [this](auto* res, auto* req) {
        std::string uuid(req->getParameter(0));
        std::string buffer;
        res->onAborted([res]() {});
        res->onData([this, res, uuid, buffer = std::move(buffer)](std::string_view data, bool last) mutable {
            buffer += data;
            if (!last) return;

            nlohmann::json body;
            try { body = nlohmann::json::parse(buffer); } catch (...) {
                res->writeStatus("400")->end("{\"error\":\"invalid json\"}");
                return;
            }

            std::string role = body.value("role", "");
            if (role.empty()) {
                res->writeStatus("400")->end("{\"error\":\"missing role\"}");
                return;
            }

            // If this is a tool response for a user_prompt
            if (role == "tool" && body.contains("tool_call_id")) {
                std::string tcId = body["tool_call_id"];
                int rc = m_events->resolvePrompt(uuid, tcId);
                if (rc == 0) {
                    if (m_sse) {
                        nlohmann::json ev;
                        ev["session"] = uuid;
                        ev["toolCallId"] = tcId;
                        m_sse->broadcast("prompt_resolved", ev.dump());
                    }
                    // Find which b1 manages this agent and send prompt_reply
                    auto b1s = m_registry->listB1s();
                    int b1Pid = -1;
                    for (const auto& b1 : b1s) {
                        for (const auto& ag : b1.agents) {
                            if (ag.sessionUuid == uuid) { b1Pid = b1.pid; break; }
                        }
                        if (b1Pid > 0) break;
                    }
                    if (b1Pid > 0) {
                        ipc::Message reply;
                        reply.type = ipc::MessageType::PROMPT_REPLY;
                        reply.sessionUuid = uuid;
                        reply.toolCallId = tcId;
                        m_listener->sendToB1(b1Pid, reply);
                    }
                }
            }

            nlohmann::json resp;
            resp["status"] = "ok";
            resp["role"] = role;
            res->writeHeader("Content-Type", "application/json");
            res->end(resp.dump());
        });
    });

    // DELETE /api/agent/:uuid/prompt/:toolCallId (dismiss without answer)
    app->template del("/api/agent/:uuid/prompt/:toolCallId", [this](auto* res, auto* req) {
        std::string uuid(req->getParameter(0));
        std::string tcId(req->getParameter(1));
        m_events->dismissPrompt(uuid, tcId);
        if (m_sse) {
            nlohmann::json ev;
            ev["session"] = uuid;
            ev["toolCallId"] = tcId;
            m_sse->broadcast("prompt_dismissed", ev.dump());
        }
        res->writeHeader("Content-Type", "application/json");
        res->end("{\"status\":\"dismissed\"}");
    });

    // POST /api/terminal/open
    app->template post("/api/terminal/open", [this](auto* res, auto* /*req*/) {
        std::string buffer;
        res->onAborted([res]() {});
        res->onData([this, res, buffer = std::move(buffer)](std::string_view data, bool last) mutable {
            buffer += data;
            if (!last) return;

            nlohmann::json body;
            try { body = nlohmann::json::parse(buffer); } catch (...) {
                res->writeStatus("400")->end("{\"error\":\"invalid json\"}");
                return;
            }

            std::string rawCwd = body.value("cwd", ".");
            std::string contextType = body.value("contextType", "host");
            std::string contextId = body.value("contextId", "");

            // Resolve cwd to absolute path and validate it exists
            std::string cwd = rawCwd.empty() ? "." : rawCwd;
            char rp[4096];
            if (!realpath(cwd.c_str(), rp)) {
                nlohmann::json err;
                err["error"] = "directory not found";
                err["cwd"] = cwd;
                res->writeStatus("400")->end(err.dump());
                return;
            }
            cwd = rp;

            // Generate terminal ID
            std::string terminalId = "term_" + std::to_string(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());

            // Look for a b1 already watching this directory
            auto b1s = m_registry->listB1s();
            int matchedB1 = -1;
            for (const auto& b1 : b1s) {
                char b1Cwd[4096];
                if (realpath(b1.workdir.c_str(), b1Cwd) && cwd == b1Cwd) {
                    matchedB1 = b1.pid;
                    break;
                }
            }

            if (matchedB1 > 0) {
                // Send TERMINAL_OPEN to the matching b1
                ipc::Message msg;
                msg.type = ipc::MessageType::TERMINAL_OPEN;
                msg.terminalId = terminalId;
                msg.cwd = cwd;
                msg.contextType = contextType;
                msg.contextId = contextId;
                m_listener->sendToB1(matchedB1, msg);
            } else {
                // No b1 for this directory — launch a0 --terminal directly
                char selfBuf[4096];
                std::string a0Path;
                ssize_t slen = readlink("/proc/self/exe", selfBuf, sizeof(selfBuf) - 1);
                if (slen > 0) {
                    selfBuf[slen] = '\0';
                    std::string self(selfBuf);
                    auto slash = self.rfind('/');
                    if (slash != std::string::npos)
                        a0Path = self.substr(0, slash) + "/a0";
                }
                if (!a0Path.empty()) {
                    pid_t child = fork();
                    if (child == 0) {
                        setsid();
                        std::string a0Dir = cwd + "/.a0";
                        execlp(a0Path.c_str(), "a0", "--terminal", "--cwd", cwd.c_str(),
                               "--a0-dir", a0Dir.c_str(), "--terminal-id", terminalId.c_str(),
                               nullptr);
                        _exit(127);
                    }
                    // Record for status polling
                    m_directTerminals[terminalId] = cwd;
                }
            }

            nlohmann::json resp;
            resp["terminalId"] = terminalId;
            res->writeHeader("Content-Type", "application/json");
            res->end(resp.dump());
        });
    });

    // POST /api/stream/:id/input
    app->template post("/api/stream/:id/input", [this](auto* res, auto* req) {
        std::string streamIdStr(req->getParameter(0));
        int64_t streamId = std::stoll(streamIdStr);

        std::string buffer;
        res->onAborted([res]() {});
        res->onData([this, res, streamId, buffer = std::move(buffer)](std::string_view data, bool last) mutable {
            buffer += data;
            if (!last) return;

            nlohmann::json body;
            try { body = nlohmann::json::parse(buffer); } catch (...) {
                res->writeStatus("400")->end("{\"error\":\"invalid json\"}");
                return;
            }

            std::string input = body.value("data", "");
            if (input.empty()) {
                res->writeStatus("400")->end("{\"error\":\"missing data\"}");
                return;
            }

            // Find the b1 that owns this stream and send STREAM_INPUT
            auto b1s = m_registry->listB1s();
            if (!b1s.empty()) {
                ipc::Message msg;
                msg.type = ipc::MessageType::STREAM_INPUT;
                msg.streamId = streamId;
                msg.chunkData = input;
                m_listener->sendToB1(b1s[0].pid, msg);
            }

            res->writeHeader("Content-Type", "application/json");
            res->end("{\"status\":\"ok\"}");
        });
    });

    // GET /api/session/:uuid/streams
    app->template get("/api/session/:uuid/streams", [this](auto* res, auto* req) {
        std::string uuid(req->getParameter(0));

        // Find the b1 for this session to get the DB path
        auto b1s = m_registry->listB1s();
        std::string dbPath;
        for (const auto& b1 : b1s) {
            for (const auto& ag : b1.agents) {
                if (ag.sessionUuid == uuid) {
                    dbPath = b1.workdir + "/.a0/db/sessions.db";
                    break;
                }
            }
            if (!dbPath.empty()) break;
        }

        if (dbPath.empty()) {
            res->writeHeader("Content-Type", "application/json");
            res->end("[]");
            return;
        }

        // Read from SQLite directly
        sqlite3* db = nullptr;
        if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
            if (db) sqlite3_close(db);
            res->writeHeader("Content-Type", "application/json");
            res->end("[]");
            return;
        }

        // Find session id by uuid
        sqlite3_stmt* stmt;
        int64_t sessionId = 0;
        const char* sql = "SELECT id FROM session WHERE uuid = ?";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                sessionId = sqlite3_column_int64(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }

        nlohmann::json arr = nlohmann::json::array();
        if (sessionId > 0) {
            sql = "SELECT id, session_id, tool_call_id, name, context_type,"
                  " context_id, cwd, created_at, ended_at, exit_code"
                  " FROM stream WHERE session_id = ? ORDER BY created_at";
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, sessionId);
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    nlohmann::json s;
                    s["id"] = sqlite3_column_int64(stmt, 0);
                    s["sessionId"] = sqlite3_column_int64(stmt, 1);
                    if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)))
                        s["toolCallId"] = p;
                    if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)))
                        s["name"] = p;
                    if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)))
                        s["contextType"] = p;
                    if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)))
                        s["contextId"] = p;
                    if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)))
                        s["cwd"] = p;
                    s["createdAt"] = sqlite3_column_int64(stmt, 7);
                    s["endedAt"] = sqlite3_column_type(stmt, 8) == SQLITE_NULL ? 0 : sqlite3_column_int64(stmt, 8);
                    s["exitCode"] = sqlite3_column_type(stmt, 9) == SQLITE_NULL ? -1 : sqlite3_column_int(stmt, 9);
                    arr.push_back(s);
                }
                sqlite3_finalize(stmt);
            }
        }

        sqlite3_close(db);
        res->writeHeader("Content-Type", "application/json");
        res->end(arr.dump(2));
    });

    // GET /api/stream/:id/chunks
    app->template get("/api/stream/:id/chunks", [this](auto* res, auto* req) {
        std::string streamIdStr(req->getParameter(0));
        int64_t streamId = std::stoll(streamIdStr);

        // Find any b1 to resolve DB path from
        auto b1s = m_registry->listB1s();
        std::string dbPath;
        for (const auto& b1 : b1s) {
            dbPath = b1.workdir + "/.a0/db/sessions.db";
            break;
        }

        if (dbPath.empty()) {
            res->writeHeader("Content-Type", "application/json");
            res->end("[]");
            return;
        }

        sqlite3* db = nullptr;
        if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
            if (db) sqlite3_close(db);
            res->writeHeader("Content-Type", "application/json");
            res->end("[]");
            return;
        }

        nlohmann::json arr = nlohmann::json::array();
        sqlite3_stmt* stmt;
        const char* sql = "SELECT id, stream_id, seq, direction, data, timestamp"
            " FROM stream_chunk WHERE stream_id = ? ORDER BY seq LIMIT 1000";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, streamId);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                nlohmann::json c;
                c["id"] = sqlite3_column_int64(stmt, 0);
                c["streamId"] = sqlite3_column_int64(stmt, 1);
                c["seq"] = sqlite3_column_int(stmt, 2);
                if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)))
                    c["direction"] = p;
                if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)))
                    c["data"] = p;
                c["timestamp"] = sqlite3_column_int64(stmt, 5);
                arr.push_back(c);
            }
            sqlite3_finalize(stmt);
        }

        sqlite3_close(db);
        res->writeHeader("Content-Type", "application/json");
        res->end(arr.dump(2));
    });

    // GET /api/terminal/status/:terminalId — polls DB for terminal stream
    app->template get("/api/terminal/status/:terminalId", [this](auto* res, auto* req) {
        std::string terminalId(req->getParameter(0));
        nlohmann::json result;
        result["status"] = "waiting";

        // Try direct-launched terminals first
        std::vector<std::string> paths;
        auto it = m_directTerminals.find(terminalId);
        if (it != m_directTerminals.end()) {
            paths.push_back(it->second + "/.a0/db/sessions.db");
        }
        // Then try all registered b1s
        for (const auto& b1 : m_registry->listB1s()) {
            paths.push_back(b1.workdir + "/.a0/db/sessions.db");
        }

        for (const auto& dbPath : paths) {
            sqlite3* db = nullptr;
            if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
                if (db) sqlite3_close(db);
                continue;
            }
            sqlite3_stmt* stmt;
            const char* sql = "SELECT id FROM stream"
                " WHERE terminal_id = ? AND terminal_id IS NOT NULL LIMIT 1";
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, terminalId.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    result["status"] = "ready";
                    result["streamId"] = sqlite3_column_int64(stmt, 0);
                    sqlite3_finalize(stmt);
                    sqlite3_close(db);
                    res->writeHeader("Content-Type", "application/json");
                    res->end(result.dump());
                    return;
                }
                sqlite3_finalize(stmt);
            }
            sqlite3_close(db);
        }

        res->writeHeader("Content-Type", "application/json");
        res->end(result.dump());
    });

    // Static file serving (catch-all)
    app->template get("/*", [this](auto* res, auto* req) {
        xServeStatic(res, std::string(req->getUrl()));
    });
}

int DashboardServer::run() {
    if (m_port <= 0) return -1;
    m_running = true;

    if (!m_sslKey.empty() && !m_sslCert.empty()) {
        uWS::SocketContextOptions sslOpts;
        sslOpts.key_file_name = m_sslKey.c_str();
        sslOpts.cert_file_name = m_sslCert.c_str();
        auto app = uWS::SSLApp(sslOpts);
        xSetupRoutes(&app);
        app.listen(m_port, [this](auto* token) {
            if (!token) { m_running = false; return; }
            m_listenToken = token;
        }).run();
    } else {
        auto app = uWS::App();
        xSetupRoutes(&app);
        app.listen(m_port, [this](auto* token) {
            if (!token) { m_running = false; return; }
            m_listenToken = token;
        }).run();
    }

    return m_shutdownRequested ? 0 : (m_running ? 0 : -1);
}

void DashboardServer::shutdown() {
    m_running = false;
    m_shutdownRequested = true;
    if (m_listenToken) {
        us_listen_socket_close(0, m_listenToken);
        m_listenToken = nullptr;
    }
}

} // namespace a0::c2
