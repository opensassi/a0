#include "context_manager.h"
#include <gtest/gtest.h>

class ContextManagerTest : public ::testing::Test {
protected:
    DefaultContextManager ctx;
};

TEST_F(ContextManagerTest, PushAndSize) {
    EXPECT_EQ(ctx.size(), 0u);
    ctx.push({"user", "hello"});
    EXPECT_EQ(ctx.size(), 1u);
}

TEST_F(ContextManagerTest, PushAndPeek) {
    ctx.push({"assistant", "world"});
    ContextFrame f = ctx.peek();
    EXPECT_EQ(f.role, "assistant");
    EXPECT_EQ(f.content, "world");
}

TEST_F(ContextManagerTest, PushAndPop) {
    ctx.push({"user", "hi"});
    ContextFrame f = ctx.pop();
    EXPECT_EQ(f.role, "user");
    EXPECT_EQ(f.content, "hi");
    EXPECT_EQ(ctx.size(), 0u);
}

TEST_F(ContextManagerTest, PopEmptyThrows) {
    EXPECT_THROW(ctx.pop(), std::out_of_range);
}

TEST_F(ContextManagerTest, PeekEmptyThrows) {
    EXPECT_THROW(ctx.peek(), std::out_of_range);
}

TEST_F(ContextManagerTest, FifoOrder) {
    ctx.push({"user", "first"});
    ctx.push({"assistant", "second"});
    EXPECT_EQ(ctx.pop().content, "second");
    EXPECT_EQ(ctx.pop().content, "first");
}

TEST_F(ContextManagerTest, ClearEmpties) {
    ctx.push({"user", "a"});
    ctx.push({"user", "b"});
    ctx.clear();
    EXPECT_EQ(ctx.size(), 0u);
}

TEST_F(ContextManagerTest, SnapshotTwoFrames) {
    ctx.push({"user", "q1"});
    ctx.push({"assistant", "a1"});
    auto snap = ctx.snapshot();
    ASSERT_EQ(snap.size(), 2u);
    EXPECT_EQ(snap[0].role, "user");
    EXPECT_EQ(snap[1].role, "assistant");
}

TEST_F(ContextManagerTest, MaxDepthDoesNotCrash) {
    for (size_t i = 0; i < 1001; ++i) {
        ctx.push({"user", "x"});
    }
    EXPECT_GE(ctx.size(), 1000u);
    EXPECT_LE(ctx.size(), 1000u);
}

TEST_F(ContextManagerTest, SnapshotAfterClear) {
    ctx.push({"user", "x"});
    ctx.clear();
    auto snap = ctx.snapshot();
    EXPECT_TRUE(snap.empty());
}
