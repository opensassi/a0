#include "tui/session_manager.h"
#include "mock/mock_persistence_store.h"
#include <gtest/gtest.h>

using namespace a0::tui;
using namespace a0::persistence;

class SessionManagerTest : public ::testing::Test {
protected:
    MockPersistenceStore store;
    SessionManager mgr{&store};

    void SetUp() override {
        store = MockPersistenceStore{};
    }
};

TEST_F(SessionManagerTest, CreateNewSession) {
    int64_t dbId = mgr.create("test-uuid-1", 1);
    EXPECT_GT(dbId, 0);
    EXPECT_EQ(mgr.currentUuid(), "test-uuid-1");
    EXPECT_EQ(mgr.currentDbId(), dbId);
}

TEST_F(SessionManagerTest, CreateReturnsNegativeOnNullStore) {
    SessionManager nullMgr(nullptr);
    int64_t dbId = nullMgr.create("test-uuid", 1);
    EXPECT_EQ(dbId, -1);
}

TEST_F(SessionManagerTest, ResumeExistingSession) {
    int64_t createdDbId = mgr.create("test-uuid-resume", 1);
    ASSERT_GT(createdDbId, 0);

    int64_t outDbId = 0;
    int rc = mgr.resume("test-uuid-resume", outDbId);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(outDbId, createdDbId);
    EXPECT_EQ(mgr.currentUuid(), "test-uuid-resume");
}

TEST_F(SessionManagerTest, ResumeMissingSession) {
    int64_t outDbId = 0;
    int rc = mgr.resume("nonexistent-uuid", outDbId);
    EXPECT_EQ(rc, -1);
    EXPECT_EQ(mgr.currentUuid(), "");
}

TEST_F(SessionManagerTest, CurrentUuidBeforeCreate) {
    EXPECT_EQ(mgr.currentUuid(), "");
    EXPECT_EQ(mgr.currentDbId(), 0);
}

TEST_F(SessionManagerTest, CurrentUuidAfterCreate) {
    mgr.create("my-uuid", 1);
    EXPECT_EQ(mgr.currentUuid(), "my-uuid");
}

TEST_F(SessionManagerTest, EndCurrentSession) {
    mgr.create("end-test", 1);
    ASSERT_EQ(mgr.currentUuid(), "end-test");
    mgr.endCurrent();
    EXPECT_EQ(mgr.currentUuid(), "");
    EXPECT_EQ(mgr.currentDbId(), 0);
}

TEST_F(SessionManagerTest, EndCurrentWithNoActiveSession) {
    mgr.endCurrent();
    SUCCEED();
}

TEST_F(SessionManagerTest, ListReturnsEmpty) {
    auto sessions = mgr.list(10);
    EXPECT_TRUE(sessions.empty());
}

TEST_F(SessionManagerTest, ListWithLimit) {
    auto sessions = mgr.list(5);
    EXPECT_TRUE(sessions.empty());
}
