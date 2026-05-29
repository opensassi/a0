#pragma once

#include <string>
#include "b1_registry.h"

namespace a0::c2 {

class DashboardServer {
public:
    DashboardServer(int port, B1Registry* registry,
                    const std::string& sslKey = "",
                    const std::string& sslCert = "");
    ~DashboardServer();

    int run();
    void shutdown();

    std::string xBuildStatusJson();
    std::string xBuildStatsJson();
    std::string xBuildDashboardHtml();

private:
    int m_port;
    B1Registry* m_registry;
    bool m_running = false;
    std::string m_sslKey;
    std::string m_sslCert;
};

} // namespace a0::c2
