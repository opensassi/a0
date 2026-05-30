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
