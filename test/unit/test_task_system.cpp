#include "system_handlers.h"
#include "persistence/persistence_store.h"
#include "persistence/sqlite_store.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace a0::persistence;

class TaskStoreTest : public ::testing::Test {
protected:
    std::string m_dbPath;
    SqliteStore* store = nullptr;
    int agentId;
    int64_t sessionId;
    int64_t rootId;

    void SetUp() override {
        m_dbPath = "/tmp/a0_task_test_" + std::to_string(::getpid()) + ".db";
        std::remove(m_dbPath.c_str());
        store = new SqliteStore(m_dbPath);

        BuildFingerprint fp;
        fp.binarySha1 = "test";
        agentId = store->registerAgent(fp);

        sessionId = store->createSession("test-uuid", 0, 0, agentId);
        rootId = store->createSessionRootTask(sessionId);
    }

    void TearDown() override {
        delete store;
        std::remove(m_dbPath.c_str());
    }

    json withSession(const json& params) const {
        auto j = params;
        j["_session_id"] = static_cast<int64_t>(sessionId);
        return j;
    }
};

// ============================================================================
// Task handler tests (via SqliteStore)
// ============================================================================

TEST_F(TaskStoreTest, Handler_AddTask_WithParentZero) {
    auto result = a0::xAddTask(withSession({
        {"parent_task_id", 0},
        {"description", "test task"},
        {"priority", 1}
    }), store);
    EXPECT_TRUE(result.output.find("task_id") != std::string::npos);
    json j = json::parse(result.output);
    EXPECT_TRUE(j.contains("task_id"));
    EXPECT_GT(j["task_id"].get<int64_t>(), 0);
}

TEST_F(TaskStoreTest, Handler_AddTask_WithExplicitParent) {
    auto r1 = a0::xAddTask(withSession({
        {"parent_task_id", 0},
        {"description", "parent"},
        {"priority", 1}
    }), store);
    json j1 = json::parse(r1.output);
    int64_t parentId = j1["task_id"];

    auto r2 = a0::xAddTask(withSession({
        {"parent_task_id", parentId},
        {"description", "child"},
        {"priority", 2}
    }), store);
    json j2 = json::parse(r2.output);
    EXPECT_TRUE(j2.contains("task_id"));
    EXPECT_NE(j2["task_id"].get<int64_t>(), parentId);

    auto tasks = store->listTasks(parentId);
    EXPECT_EQ(tasks.size(), 1u);
    EXPECT_EQ(tasks[0].description, "child");
}

TEST_F(TaskStoreTest, Handler_AddTask_MissingDescription) {
    auto result = a0::xAddTask(withSession({{"parent_task_id", 0}}), store);
    json j = json::parse(result.output);
    EXPECT_TRUE(j.contains("task_id"));
    int64_t taskId = j["task_id"];
    auto task = store->getTask(taskId);
    EXPECT_TRUE(task.description.empty());
}

TEST_F(TaskStoreTest, Handler_AddTask_SetsAllFields) {
    auto result = a0::xAddTask(withSession({
        {"parent_task_id", 0},
        {"description", "full task"},
        {"detailed_plan", "step by step"},
        {"automated_verification", "auto check"},
        {"human_verification", "human check"},
        {"priority", 5}
    }), store);
    json j = json::parse(result.output);
    int64_t taskId = j["task_id"];

    auto task = store->getTask(taskId);
    EXPECT_EQ(task.description, "full task");
    EXPECT_EQ(task.detailedPlan, "step by step");
    EXPECT_EQ(task.automatedVerification, "auto check");
    EXPECT_EQ(task.humanVerification, "human check");
    EXPECT_EQ(task.priority, 5);
}

TEST_F(TaskStoreTest, Handler_RemoveTask_RemovesLeaf) {
    auto r1 = a0::xAddTask(withSession({
        {"parent_task_id", 0},
        {"description", "removable"},
        {"priority", 1}
    }), store);
    json j1 = json::parse(r1.output);
    int64_t taskId = j1["task_id"];

    auto result = a0::xRemoveTask({{"task_id", taskId}}, store);
    json jr = json::parse(result.output);
    EXPECT_TRUE(jr["removed"].get<bool>());

    auto task = store->getTask(taskId);
    EXPECT_EQ(task.id, 0);
}

TEST_F(TaskStoreTest, Handler_RemoveTask_HasChildren) {
    auto r1 = a0::xAddTask(withSession({
        {"parent_task_id", 0},
        {"description", "parent"},
        {"priority", 1}
    }), store);
    json j1 = json::parse(r1.output);
    int64_t parentId = j1["task_id"];

    a0::xAddTask(withSession({
        {"parent_task_id", parentId},
        {"description", "child"},
        {"priority", 1}
    }), store);

    auto result = a0::xRemoveTask({{"task_id", parentId}}, store);
    EXPECT_TRUE(result.output.find("ERROR") != std::string::npos ||
                result.output.find("children") != std::string::npos);
}

TEST_F(TaskStoreTest, Handler_RemoveTask_MissingId) {
    auto result = a0::xRemoveTask(json::object(), store);
    EXPECT_TRUE(result.output.find("ERROR") != std::string::npos);
}

TEST_F(TaskStoreTest, Handler_ListTasks_ReturnsChildren) {
    a0::xAddTask(withSession({
        {"parent_task_id", 0},
        {"description", "t1"},
        {"priority", 1}
    }), store);
    a0::xAddTask(withSession({
        {"parent_task_id", 0},
        {"description", "t2"},
        {"priority", 2}
    }), store);

    auto result = a0::xListTasks(withSession({}), store);
    EXPECT_TRUE(result.output.find("t1") != std::string::npos);
    EXPECT_TRUE(result.output.find("t2") != std::string::npos);
}

TEST_F(TaskStoreTest, Handler_ListTasks_NoTasks) {
    auto result = a0::xListTasks(nlohmann::json{{"_session_id", sessionId}}, store);
    EXPECT_TRUE(result.output.find("no tasks") != std::string::npos ||
                result.output.find("(no tasks)") != std::string::npos);
}

TEST_F(TaskStoreTest, Handler_SetTaskPriority_Updates) {
    auto r1 = a0::xAddTask(withSession({
        {"parent_task_id", 0},
        {"description", "task"},
        {"priority", 1}
    }), store);
    json j1 = json::parse(r1.output);
    int64_t taskId = j1["task_id"];

    auto result = a0::xSetTaskPriority({{"task_id", taskId}, {"priority", 99}}, store);
    json jr = json::parse(result.output);
    EXPECT_TRUE(jr["updated"].get<bool>());

    auto task = store->getTask(taskId);
    EXPECT_EQ(task.priority, 99);
}

TEST_F(TaskStoreTest, Handler_SetTaskPriority_MissingParams) {
    auto result = a0::xSetTaskPriority(json::object(), store);
    EXPECT_TRUE(result.output.find("ERROR") != std::string::npos);
}

// ============================================================================
// SqliteStore task method tests
// ============================================================================

TEST_F(TaskStoreTest, Store_CreateSessionRootTask_NewSession) {
    EXPECT_GT(rootId, 0);
    auto got = store->getSessionRootTask(sessionId);
    EXPECT_EQ(got, rootId);
    auto task = store->getTask(rootId);
    EXPECT_EQ(task.id, rootId);
    EXPECT_EQ(task.rootTaskId, rootId);
    EXPECT_EQ(task.parentTaskId, rootId);
    EXPECT_EQ(task.sessionId, sessionId);
    EXPECT_EQ(task.status, "root");
}

TEST_F(TaskStoreTest, Store_GetSessionRootTask_NoSession) {
    EXPECT_EQ(store->getSessionRootTask(99999), 0);
}

TEST_F(TaskStoreTest, Store_AddTask_ChildTask) {
    Task child;
    child.rootTaskId = rootId;
    child.parentTaskId = rootId;
    child.sessionId = sessionId;
    child.description = "child task";
    child.priority = 1;
    child.status = "pending";

    int64_t childId = store->addTask(child);
    EXPECT_GT(childId, 0);
    EXPECT_NE(childId, rootId);
}

TEST_F(TaskStoreTest, Store_AddTask_AllFieldsRoundTrip) {
    Task task;
    task.rootTaskId = rootId;
    task.parentTaskId = rootId;
    task.sessionId = sessionId;
    task.description = "full desc";
    task.detailedPlan = "plan details";
    task.automatedVerification = "auto v";
    task.humanVerification = "human v";
    task.priority = 7;
    task.status = "in_progress";

    int64_t id = store->addTask(task);
    ASSERT_GT(id, 0);

    Task got = store->getTask(id);
    EXPECT_EQ(got.id, id);
    EXPECT_EQ(got.rootTaskId, rootId);
    EXPECT_EQ(got.parentTaskId, rootId);
    EXPECT_EQ(got.sessionId, sessionId);
    EXPECT_EQ(got.description, "full desc");
    EXPECT_EQ(got.detailedPlan, "plan details");
    EXPECT_EQ(got.automatedVerification, "auto v");
    EXPECT_EQ(got.humanVerification, "human v");
    EXPECT_EQ(got.priority, 7);
    EXPECT_EQ(got.status, "in_progress");
}

TEST_F(TaskStoreTest, Store_RemoveTask_Leaf) {
    Task child;
    child.rootTaskId = rootId;
    child.parentTaskId = rootId;
    child.sessionId = sessionId;
    child.description = "leaf";
    int64_t childId = store->addTask(child);

    EXPECT_EQ(store->removeTask(childId), 0);
    Task got = store->getTask(childId);
    EXPECT_EQ(got.id, 0);
}

TEST_F(TaskStoreTest, Store_RemoveTask_WithChildren) {
    Task child;
    child.rootTaskId = rootId;
    child.parentTaskId = rootId;
    child.sessionId = sessionId;
    child.description = "child";
    store->addTask(child);

    EXPECT_EQ(store->removeTask(rootId), -1);
    Task got = store->getTask(rootId);
    EXPECT_NE(got.id, 0);
}

TEST_F(TaskStoreTest, Store_ListTasks_Children) {
    Task t1, t2;
    t1.rootTaskId = rootId; t1.parentTaskId = rootId;
    t1.sessionId = sessionId; t1.description = "first"; t1.priority = 2;
    t2.rootTaskId = rootId; t2.parentTaskId = rootId;
    t2.sessionId = sessionId; t2.description = "second"; t2.priority = 1;

    int64_t id1 = store->addTask(t1);
    int64_t id2 = store->addTask(t2);

    auto tasks = store->listTasks(rootId);
    EXPECT_EQ(tasks.size(), 2u);
    EXPECT_EQ(tasks[0].description, "second");
    EXPECT_EQ(tasks[1].description, "first");
}

TEST_F(TaskStoreTest, Store_ListTasks_NoChildren) {
    auto tasks = store->listTasks(rootId);
    EXPECT_TRUE(tasks.empty());
}

TEST_F(TaskStoreTest, Store_UpdateTaskPriority) {
    Task t;
    t.rootTaskId = rootId;
    t.parentTaskId = rootId;
    t.sessionId = sessionId;
    t.description = "prioritize me";
    t.priority = 1;

    int64_t id = store->addTask(t);
    EXPECT_EQ(store->updateTaskPriority(id, 99), 0);
    auto task = store->getTask(id);
    EXPECT_EQ(task.priority, 99);
}

TEST_F(TaskStoreTest, Store_GetTask_NotFound) {
    Task t = store->getTask(99999);
    EXPECT_EQ(t.id, 0);
}
