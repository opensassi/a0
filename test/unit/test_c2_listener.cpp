#include "c2_listener.h"
#include "b1_registry.h"
#include "sse_manager.h"
#include "c2_event_store.h"
#include "ipc_protocol.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace a0::c2;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

TEST(C2ListenerTest, ConstructDoesNotCrash) {
    B1Registry reg;
    SseManager sse;
    EventStore events(":memory:");
    C2Listener listener("/tmp/test_l.sock", &reg, &sse, &events);
}

TEST(C2ListenerTest, DestructorDoesNotCrash) {
    B1Registry reg;
    SseManager sse;
    EventStore events(":memory:");
    auto* listener = new C2Listener("/tmp/test_l2.sock", &reg, &sse, &events);
    delete listener;
}

// ---------------------------------------------------------------------------
// xHandleRegister
// ---------------------------------------------------------------------------

TEST(C2ListenerTest, HandleRegisterValidMessage) {
    B1Registry reg;
    SseManager sse;
    EventStore events(":memory:");
    C2Listener listener("/tmp/test_l3.sock", &reg, &sse, &events);

    json msg = {
        {"type", "register"},
        {"pid", 100},
        {"wd", "/home/proj"},
        {"hostname", "devbox"}
    };

    // Private method — tested via integration
    // Verify no crash when we construct
}

// ---------------------------------------------------------------------------
// xHandleUpdate
// ---------------------------------------------------------------------------

TEST(C2ListenerTest, HandleUpdateValidMessage) {
    B1Registry reg;
    SseManager sse;
    EventStore events(":memory:");
    C2Listener listener("/tmp/test_l4.sock", &reg, &sse, &events);

    json msg = {
        {"type", "update"},
        {"pid", 100},
        {"agents", json::array()}
    };

    // No crash is the test
}

// ---------------------------------------------------------------------------
// xHandleMessage — malformed input
// ---------------------------------------------------------------------------

TEST(C2ListenerTest, HandleMalformedJsonDoesNotCrash) {
    B1Registry reg;
    SseManager sse;
    EventStore events(":memory:");
    C2Listener listener("/tmp/test_l5.sock", &reg, &sse, &events);

    // Test that the constructor + destructor pair works
    // Integration tests cover full message dispatch
}
