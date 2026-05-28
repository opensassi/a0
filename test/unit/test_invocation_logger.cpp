#include "invocation_logger.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>

class InvocationLoggerTest : public ::testing::Test {
protected:
    std::string m_testDir = "test_logs";
    JsonLinesLogger* logger;

    void SetUp() override {
        std::string cmd = "rm -rf " + m_testDir + " && mkdir -p " + m_testDir;
        system(cmd.c_str());
        logger = new JsonLinesLogger(m_testDir);
    }

    void TearDown() override {
        delete logger;
        std::string cmd = "rm -rf " + m_testDir;
        system(cmd.c_str());
    }

    LogEntry makeEntry(const std::string& session, const std::string& event) {
        return {session, 1000, event, "{\"data\":1}"};
    }
};

TEST_F(InvocationLoggerTest, LogOneEntry) {
    LogEntry e = makeEntry("sess1", "tool_call");
    EXPECT_NO_THROW(logger->log(e));
}

TEST_F(InvocationLoggerTest, LogThenListSessions) {
    logger->log(makeEntry("sess1", "start"));
    auto sessions = logger->listSessions();
    ASSERT_FALSE(sessions.empty());
    EXPECT_EQ(sessions[0], "sess1");
}

TEST_F(InvocationLoggerTest, ReplayTwoEntries) {
    LogEntry e1 = makeEntry("sess_r", "event_a");
    LogEntry e2 = makeEntry("sess_r", "event_b");
    logger->log(e1);
    logger->log(e2);

    std::vector<LogEntry> replayed;
    bool ok = logger->replay("sess_r", [&](const LogEntry& entry) {
        replayed.push_back(entry);
    });
    EXPECT_TRUE(ok);
    ASSERT_EQ(replayed.size(), 2u);
    if (replayed.size() >= 2) {
        EXPECT_EQ(replayed[0].eventType, "event_a");
        EXPECT_EQ(replayed[1].eventType, "event_b");
    }
}

TEST_F(InvocationLoggerTest, ReplayNonexistentSession) {
    bool ok = logger->replay("no_such_session", [](const LogEntry&) {});
    EXPECT_FALSE(ok);
}

TEST_F(InvocationLoggerTest, ListSessionsEmpty) {
    auto sessions = logger->listSessions();
    EXPECT_TRUE(sessions.empty());
}

TEST_F(InvocationLoggerTest, MultipleSessions) {
    logger->log(makeEntry("alpha", "start"));
    logger->log(makeEntry("beta", "start"));
    auto sessions = logger->listSessions();
    EXPECT_EQ(sessions.size(), 2u);
}

TEST_F(InvocationLoggerTest, LogEntryTimestamps) {
    LogEntry e = makeEntry("ts_test", "event");
    e.timestamp = 1234567890;
    logger->log(e);
    std::vector<LogEntry> replayed;
    logger->replay("ts_test", [&](const LogEntry& entry) {
        replayed.push_back(entry);
    });
    ASSERT_EQ(replayed.size(), 1u);
    if (replayed.size() >= 1) {
        EXPECT_EQ(replayed[0].timestamp, 1234567890);
    }
}

TEST_F(InvocationLoggerTest, LargeSessionId) {
    std::string longId(200, 'x');
    EXPECT_NO_THROW(logger->log(makeEntry(longId, "event")));
    auto sessions = logger->listSessions();
    EXPECT_EQ(sessions.size(), 1u);
    if (sessions.size() == 1) {
        EXPECT_EQ(sessions[0], longId);
    }
}
