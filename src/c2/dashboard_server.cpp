#include "dashboard_server.h"
#include "nlohmann/json.hpp"
#include <App.h>

namespace a0::c2 {

DashboardServer::DashboardServer(int port, B1Registry* registry,
                                  const std::string& sslKey,
                                  const std::string& sslCert)
    : m_port(port)
    , m_registry(registry)
    , m_sslKey(sslKey)
    , m_sslCert(sslCert)
{
}

DashboardServer::~DashboardServer() {
    shutdown();
}

int DashboardServer::run() {
    if (m_port <= 0) return -1;
    m_running = true;

    auto setupRoutes = [this](auto* app) {
        app->template get("/api/status", [this](auto* res, auto* /*req*/) {
            res->writeHeader("Content-Type", "application/json");
            res->end(xBuildStatusJson());
        });
        app->template get("/api/stats", [this](auto* res, auto* /*req*/) {
            res->writeHeader("Content-Type", "application/json");
            res->end(xBuildStatsJson());
        });
        app->template get("/", [this](auto* res, auto* /*req*/) {
            res->writeHeader("Content-Type", "text/html");
            res->end(xBuildDashboardHtml());
        });
    };

    if (!m_sslKey.empty() && !m_sslCert.empty()) {
        uWS::SocketContextOptions sslOpts;
        sslOpts.key_file_name = m_sslKey.c_str();
        sslOpts.cert_file_name = m_sslCert.c_str();

        uWS::SSLApp(sslOpts)
            .get("/api/status", [this](auto* res, auto* /*req*/) {
                res->writeHeader("Content-Type", "application/json");
                res->end(xBuildStatusJson());
            })
            .get("/api/stats", [this](auto* res, auto* /*req*/) {
                res->writeHeader("Content-Type", "application/json");
                res->end(xBuildStatsJson());
            })
            .get("/", [this](auto* res, auto* /*req*/) {
                res->writeHeader("Content-Type", "text/html");
                res->end(xBuildDashboardHtml());
            })
            .listen(m_port, [this](auto* token) {
                if (!token) m_running = false;
            })
            .run();
    } else {
        uWS::App()
            .get("/api/status", [this](auto* res, auto* /*req*/) {
                res->writeHeader("Content-Type", "application/json");
                res->end(xBuildStatusJson());
            })
            .get("/api/stats", [this](auto* res, auto* /*req*/) {
                res->writeHeader("Content-Type", "application/json");
                res->end(xBuildStatsJson());
            })
            .get("/", [this](auto* res, auto* /*req*/) {
                res->writeHeader("Content-Type", "text/html");
                res->end(xBuildDashboardHtml());
            })
            .listen(m_port, [this](auto* token) {
                if (!token) m_running = false;
            })
            .run();
    }

    return m_running ? 0 : -1;
}

void DashboardServer::shutdown() {
    m_running = false;
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

std::string DashboardServer::xBuildDashboardHtml() {
    return R"(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>a0 Agent Dashboard</title>
<style>
body { font-family: -apple-system, BlinkMacSystemFont, sans-serif; margin: 20px; background: #f5f5f5; }
h1 { color: #333; }
.stats { display: flex; gap: 20px; margin: 20px 0; }
.stat-card { background: #fff; padding: 15px; border-radius: 8px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); flex: 1; text-align: center; }
.stat-card h2 { margin: 0; font-size: 14px; color: #666; }
.stat-card .value { font-size: 32px; font-weight: bold; color: #333; }
table { width: 100%; border-collapse: collapse; background: #fff; border-radius: 8px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
th, td { padding: 10px; text-align: left; border-bottom: 1px solid #eee; }
th { background: #fafafa; font-weight: 600; }
.crashed { color: #d32f2f; font-weight: bold; }
.running { color: #388e3c; }
</style>
</head>
<body>
<h1>a0 Agent Dashboard</h1>
<div class="stats">
<div class="stat-card"><h2>Supervisors</h2><div class="value" id="stat-b1s">0</div></div>
<div class="stat-card"><h2>Agents</h2><div class="value" id="stat-agents">0</div></div>
<div class="stat-card"><h2>Crashed</h2><div class="value" id="stat-crashed">0</div></div>
</div>
<table>
<thead><tr><th>Supervisor</th><th>Workdir</th><th>Hostname</th><th>Agents</th></tr></thead>
<tbody id="status-body"></tbody>
</table>
<script>
async function refresh() {
try {
const status = await (await fetch('/api/status')).json();
const stats = await (await fetch('/api/stats')).json();
document.getElementById('stat-b1s').textContent = stats.totalB1s;
document.getElementById('stat-agents').textContent = stats.totalAgents;
document.getElementById('stat-crashed').textContent = stats.crashedCount;
const tbody = document.getElementById('status-body');
tbody.innerHTML = '';
for (const b1 of status) {
const row = tbody.insertRow();
row.insertCell().textContent = 'b1 (' + b1.pid + ')';
row.insertCell().textContent = b1.workdir;
row.insertCell().textContent = b1.hostname;
const agentCell = row.insertCell();
if (b1.agents.length === 0) {
agentCell.textContent = '(none)';
} else {
const ul = document.createElement('ul');
ul.style.margin = '0';
ul.style.padding = '0';
ul.style.listStyle = 'none';
for (const a of b1.agents) {
const li = document.createElement('li');
li.textContent = 'a0 (' + a.pid + ') ';
const span = document.createElement('span');
span.textContent = a.state;
span.className = a.state === 'crashed' ? 'crashed' : 'running';
li.appendChild(span);
ul.appendChild(li);
}
agentCell.appendChild(ul);
}
}
}
} catch(e) {}
}
refresh();
setInterval(refresh, 3000);
</script>
</body>
</html>)";
}

} // namespace a0::c2
