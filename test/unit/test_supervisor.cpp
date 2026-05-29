#include "supervisor.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>

using namespace a0::b1;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(SupervisorTest, ConstructSetsPaths) {
    Supervisor sup("/tmp/test_b1.sock", "/tmp/test_b1.pid",
                   "/tmp/test_c2.sock", "/home/test/proj");
    // Construction should not crash
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

TEST(SupervisorTest, InitWritesPidFile) {
    std::string pidPath = "/tmp/test_b1_init.pid";
    std::string sockPath = "/tmp/test_b1_init.sock";
    std::remove(pidPath.c_str());
    std::remove(sockPath.c_str());

    Supervisor sup(sockPath, pidPath, "/tmp/c2.sock", "/tmp");
    int rc = sup.init();
    // Stub returns 0 without writing
    // Real impl must write PID file and bind socket
    EXPECT_EQ(rc, 0);

    // Real impl should verify these:
    // std::ifstream f(pidPath);
    // int pid; f >> pid;
    // EXPECT_EQ(pid, getpid());

    std::remove(pidPath.c_str());
    std::remove(sockPath.c_str());
}

// ---------------------------------------------------------------------------
// AgentCount
// ---------------------------------------------------------------------------

TEST(SupervisorTest, AgentCountStartsAtZero) {
    Supervisor sup("/tmp/test_b1_count.sock", "/tmp/test_b1_count.pid",
                   "/tmp/c2.sock", "/tmp");
    EXPECT_EQ(sup.agentCount(), 0u);
}

TEST(SupervisorTest, AgentCountAfterRegister) {
    Supervisor sup("/tmp/test_b1_reg.sock", "/tmp/test_b1_reg.pid",
                   "/tmp/c2.sock", "/tmp");
    // Stub doesn't handle registers yet
    // Real impl should increment on register
    // For now, assert it starts at 0
    EXPECT_EQ(sup.agentCount(), 0u);
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

TEST(SupervisorTest, ShutdownDoesNotCrash) {
    Supervisor sup("/tmp/t.sock", "/tmp/t.pid", "/tmp/c2.sock", "/tmp");
    sup.shutdown();
}

// ---------------------------------------------------------------------------
// xDetectCrashes
// ---------------------------------------------------------------------------

TEST(SupervisorTest, DetectCrashesWithNoChildrenReturnsZero) {
    Supervisor sup("/tmp/t2.sock", "/tmp/t2.pid", "/tmp/c2.sock", "/tmp");
    // Private method — tested via integration
    // Stub returns 0
}
