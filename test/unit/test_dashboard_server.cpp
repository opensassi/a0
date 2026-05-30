#include "dashboard_server.h"
#include "b1_registry.h"
#include "sse_manager.h"
#include "c2_event_store.h"
#include "c2_listener.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace a0::c2;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(DashboardServerTest, ConstructWithValidPort) {
    B1Registry reg;
    SseManager sse;
    EventStore events(":memory:");
    C2Listener listener("/tmp/test_ds.sock", &reg, &sse, &events);
    DashboardServer server(8080, &reg, &sse, &events, &listener, ".");
    // No crash
}

TEST(DashboardServerTest, ConstructWithPortZero) {
    B1Registry reg;
    SseManager sse;
    EventStore events(":memory:");
    C2Listener listener("/tmp/test_ds2.sock", &reg, &sse, &events);
    DashboardServer server(0, &reg, &sse, &events, &listener, ".");
    // No crash
}

// ---------------------------------------------------------------------------
// run / shutdown
// ---------------------------------------------------------------------------

TEST(DashboardServerTest, ConstructAndDestroy) {
    B1Registry reg;
    SseManager sse;
    EventStore events(":memory:");
    C2Listener listener("/tmp/test_ds.sock", &reg, &sse, &events);
    DashboardServer server(8080, &reg, &sse, &events, &listener, ".");
}
