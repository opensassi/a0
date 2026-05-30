#pragma once

#include <string>
#include <libusockets.h>

namespace a0::c2 {

class B1Registry;
class SseManager;
class EventStore;
class C2Listener;

class DashboardServer {
public:
    DashboardServer(int port, B1Registry* registry, SseManager* sse,
                    EventStore* events, C2Listener* listener,
                    const std::string& webRoot,
                    const std::string& sslKey = "",
                    const std::string& sslCert = "");
    ~DashboardServer();

    int run();
    void shutdown();

private:
    int m_port;
    B1Registry* m_registry;
    SseManager* m_sse;
    EventStore* m_events;
    C2Listener* m_listener;
    std::string m_webRoot;
    std::string m_sslKey;
    std::string m_sslCert;
    bool m_running = false;
    bool m_shutdownRequested = false;
    struct us_listen_socket_t* m_listenToken = nullptr;

    template<typename App>
    void xSetupRoutes(App* app);

    std::string xBuildStatusJson();
    std::string xBuildStatsJson();
    std::string xBuildPendingJson();

    template<typename Res>
    void xServeStatic(Res* res, const std::string& urlPath);

    static std::string xMimeType(const std::string& path);
    static std::string xReadFile(const std::string& path);
};

} // namespace a0::c2
