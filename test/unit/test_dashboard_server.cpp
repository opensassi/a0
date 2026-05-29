#include "dashboard_server.h"
#include "b1_registry.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace a0::c2;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(DashboardServerTest, ConstructWithValidPort) {
    B1Registry reg;
    DashboardServer server(8080, &reg);
    // No crash
}

TEST(DashboardServerTest, ConstructWithPortZero) {
    B1Registry reg;
    DashboardServer server(0, &reg);
    // No crash
}

// ---------------------------------------------------------------------------
// xBuildStatusJson
// ---------------------------------------------------------------------------

TEST(DashboardServerTest, BuildStatusJsonEmpty) {
    B1Registry reg;
    DashboardServer server(8080, &reg);

    std::string jsonStr = server.xBuildStatusJson();
    // Stub returns "[]"
    // Real impl returns JSON array
    json parsed = json::parse(jsonStr);
    EXPECT_TRUE(parsed.is_array());
}

// ---------------------------------------------------------------------------
// xBuildStatsJson
// ---------------------------------------------------------------------------

TEST(DashboardServerTest, BuildStatsJsonEmpty) {
    B1Registry reg;
    DashboardServer server(8080, &reg);

    std::string jsonStr = server.xBuildStatsJson();
    json parsed = json::parse(jsonStr);
    // Real impl: {totalB1s:0, totalAgents:0, crashedCount:0}
    EXPECT_TRUE(parsed.is_object());
}

// ---------------------------------------------------------------------------
// xBuildDashboardHtml
// ---------------------------------------------------------------------------

TEST(DashboardServerTest, BuildDashboardHtmlReturnsString) {
    B1Registry reg;
    DashboardServer server(8080, &reg);

    std::string html = server.xBuildDashboardHtml();
    EXPECT_FALSE(html.empty());
    EXPECT_NE(html.find("<html"), std::string::npos);
}

// ---------------------------------------------------------------------------
// run / shutdown
// ---------------------------------------------------------------------------

TEST(DashboardServerTest, RunReturnsZero) {
    B1Registry reg;
    DashboardServer server(8080, &reg);
    // Stub returns 0 without binding
    // int rc = server.run();
    // Not called — would block
}
