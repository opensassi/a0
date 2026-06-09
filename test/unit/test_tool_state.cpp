#include "executor/tool_state.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using json = nlohmann::json;

TEST(ToolStateTest, SetAndGet) {
    ToolState ts;
    ts.set("key1", json("value1"));
    EXPECT_EQ(ts.get("key1"), json("value1"));
}

TEST(ToolStateTest, GetMissingReturnsNull) {
    ToolState ts;
    EXPECT_TRUE(ts.get("nonexistent").is_null());
}

TEST(ToolStateTest, HasReturnsTrue) {
    ToolState ts;
    ts.set("key", json(42));
    EXPECT_TRUE(ts.has("key"));
}

TEST(ToolStateTest, HasReturnsFalse) {
    ToolState ts;
    EXPECT_FALSE(ts.has("missing"));
}

TEST(ToolStateTest, OverwriteValue) {
    ToolState ts;
    ts.set("key", json("first"));
    ts.set("key", json("second"));
    EXPECT_EQ(ts.get("key"), json("second"));
}

TEST(ToolStateTest, RemoveKey) {
    ToolState ts;
    ts.set("key", json("value"));
    ts.remove("key");
    EXPECT_FALSE(ts.has("key"));
}

TEST(ToolStateTest, ClearAll) {
    ToolState ts;
    ts.set("a", json(1));
    ts.set("b", json(2));
    ts.clear();
    EXPECT_FALSE(ts.has("a"));
    EXPECT_FALSE(ts.has("b"));
}

TEST(ToolStateTest, ComplexJsonValues) {
    ToolState ts;
    json obj = {{"name", "test"}, {"count", 3}, {"tags", {"a", "b"}}};
    ts.set("obj", obj);
    json got = ts.get("obj");
    EXPECT_EQ(got["name"], "test");
    EXPECT_EQ(got["count"], 3);
    EXPECT_EQ(got["tags"].size(), 2u);
}

TEST(ToolStateTest, ThreadSafety) {
    ToolState ts;
    std::vector<std::thread> threads;
    const int N = 20;
    for (int i = 0; i < N; ++i) {
        threads.push_back(std::thread([&ts, i]() {
            ts.set("key" + std::to_string(i), json(i));
            ts.get("key" + std::to_string(i));
            ts.has("key" + std::to_string(i));
        }));
    }
    for (auto& t : threads)
        t.join();
    // All keys should be present
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(ts.get("key" + std::to_string(i)), json(i));
    }
}

TEST(ToolStateTest, RemoveNonexistent) {
    ToolState ts;
    ts.remove("nothing");
    SUCCEED();
}

TEST(ToolStateTest, ClearEmpty) {
    ToolState ts;
    ts.clear();
    SUCCEED();
}
