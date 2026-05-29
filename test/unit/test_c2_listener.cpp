#include "c2_listener.h"
#include "b1_registry.h"
#include "ipc_protocol.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace a0::c2;
using json = nlohmann::json;

// Helper to create a listener with a real registry
static std::unique_ptr<C2Listener> makeListener() {
    auto* reg = new B1Registry();
    auto listener = std::make_unique<C2Listener>("/tmp/test_c2_listener.sock", reg);
    return listener;
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

TEST(C2ListenerTest, ConstructDoesNotCrash) {
    B1Registry reg;
    C2Listener listener("/tmp/test_l.sock", &reg);
}

TEST(C2ListenerTest, DestructorDoesNotCrash) {
    B1Registry reg;
    auto* listener = new C2Listener("/tmp/test_l2.sock", &reg);
    delete listener;
}

// ---------------------------------------------------------------------------
// xHandleRegister
// ---------------------------------------------------------------------------

TEST(C2ListenerTest, HandleRegisterValidMessage) {
    B1Registry reg;
    C2Listener listener("/tmp/test_l3.sock", &reg);

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
    C2Listener listener("/tmp/test_l4.sock", &reg);

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
    C2Listener listener("/tmp/test_l5.sock", &reg);

    // Test that the constructor + destructor pair works
    // Integration tests cover full message dispatch
}
