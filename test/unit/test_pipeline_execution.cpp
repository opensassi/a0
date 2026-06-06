#include "dependency_graph.h"
#include "skills/skills.h"
#include "skills/skill_loader.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace a0::skills;
using namespace a0;

// ---------------------------------------------------------------------------
// Test fixture: creates a SkillManager with mock system handlers that
// record call order and timing for parallelism verification.
// ---------------------------------------------------------------------------

class PipelineExecutionTest : public ::testing::Test {
protected:
    std::string m_skillsDir;
    std::string m_storeDir;
    SkillManager* m_mgr = nullptr;

    struct CallRecord {
        std::string toolName;
        int64_t startUs;
        int64_t endUs;
    };
    std::vector<CallRecord> m_records;
    std::mutex m_recordsMutex;

    void SetUp() override {
        std::string pid = std::to_string(::getpid()) + "_" + std::to_string(rand());
        m_skillsDir = "/tmp/a0_pipe_test_skills_" + pid;
        m_storeDir = "/tmp/a0_pipe_test_store_" + pid;
        fs::remove_all(m_skillsDir);
        fs::remove_all(m_storeDir);
        fs::create_directories(m_skillsDir + "/system");
        fs::create_directories(m_skillsDir + "/local");

        m_mgr = new SkillManager(m_skillsDir, m_storeDir, nullptr);
    }

    void TearDown() override {
        delete m_mgr;
        fs::remove_all(m_skillsDir);
        fs::remove_all(m_storeDir);
    }

    void addMockReader(const std::string& qn, int delayMs = 50) {
        m_mgr->registerHandler(qn, [this, qn, delayMs](const json&, const HandlerContext&) {
            auto rec = recordStart(qn);
            if (delayMs > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            recordEnd(rec);
            return ::a0::HandlerResult{qn + ":ok", {}};
        });
    }

    void addMockWriter(const std::string& qn, int delayMs = 50) {
        m_mgr->registerHandler(qn, [this, qn, delayMs](const json&, const HandlerContext&) {
            auto rec = recordStart(qn);
            if (delayMs > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            recordEnd(rec);
            return ::a0::HandlerResult{qn + ":ok", {}};
        });
    }

    void addMockReadWrite(const std::string& qn, int delayMs = 50) {
        m_mgr->registerHandler(qn, [this, qn, delayMs](const json&, const HandlerContext&) {
            auto rec = recordStart(qn);
            if (delayMs > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            recordEnd(rec);
            return ::a0::HandlerResult{qn + ":ok", {}};
        });
    }

    CallRecord* recordStart(const std::string& name) {
        auto now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        std::lock_guard<std::mutex> lock(m_recordsMutex);
        m_records.push_back({name, now, 0});
        return &m_records.back();
    }

    void recordEnd(CallRecord* rec) {
        auto now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        rec->endUs = now;
    }

    bool overlaps(const CallRecord& a, const CallRecord& b) {
        return a.startUs < b.endUs && b.startUs < a.endUs;
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(PipelineExecutionTest, SingleRunner) {
    addMockReader("system_fs_read");
    std::vector<ToolInvocation> invs = {{"system_fs_read", {}}};
    auto batches = DependencyGraph::buildBatches(invs);
    auto results = DependencyGraph::executeBatches(batches, m_mgr);
    ASSERT_EQ(results.size(), 1u);
    ASSERT_EQ(results[0].outputs.size(), 1u);
    EXPECT_EQ(results[0].outputs[0], "system_fs_read:ok");
    EXPECT_TRUE(results[0].errors.empty());
}

TEST_F(PipelineExecutionTest, AllReadersInSameBatch) {
    addMockReader("system_fs_read", 100);
    addMockReader("system_fs_glob", 100);
    addMockReader("system_fs_grep", 100);

    std::vector<ToolInvocation> invs = {
        {"system_fs_read", {}},
        {"system_fs_glob", {}},
        {"system_fs_grep", {}}
    };
    auto batches = DependencyGraph::buildBatches(invs);
    // Structural: all readers in a single batch
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].size(), 3u);

    auto results = DependencyGraph::executeBatches(batches, m_mgr, 4);
    ASSERT_EQ(results.size(), 1u);
    ASSERT_EQ(results[0].outputs.size(), 3u);
    EXPECT_EQ(results[0].outputs[0], "system_fs_read:ok");
    EXPECT_EQ(results[0].outputs[1], "system_fs_glob:ok");
    EXPECT_EQ(results[0].outputs[2], "system_fs_grep:ok");
}

TEST_F(PipelineExecutionTest, WritersInSeparateBatches) {
    addMockWriter("system_fs_write", 50);
    addMockWriter("system_fs_edit", 50);

    std::vector<ToolInvocation> invs = {
        {"system_fs_write", {}},
        {"system_fs_edit", {}}
    };
    auto batches = DependencyGraph::buildBatches(invs);
    // Structural: each writer gets its own batch
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[0].size(), 1u);
    EXPECT_EQ(batches[1].size(), 1u);
    EXPECT_EQ(batches[0][0].qualifiedName, "system_fs_write");
    EXPECT_EQ(batches[1][0].qualifiedName, "system_fs_edit");

    auto results = DependencyGraph::executeBatches(batches, m_mgr, 4);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].outputs[0], "system_fs_write:ok");
    EXPECT_EQ(results[1].outputs[0], "system_fs_edit:ok");

    // Verify each writer ran sequentially (no overlap between batches)
    ASSERT_EQ(m_records.size(), 2u);
    EXPECT_FALSE(overlaps(m_records[0], m_records[1]))
        << "Expected writers to run in separate batches (no cross-batch overlap)";
}

TEST_F(PipelineExecutionTest, ReadersBeforeWriters_Ordering) {
    addMockReader("system_fs_read", 30);
    addMockWriter("system_fs_write", 30);

    std::vector<ToolInvocation> invs = {
        {"system_fs_read", {}},
        {"system_fs_write", {}}
    };
    auto batches = DependencyGraph::buildBatches(invs);
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[0][0].qualifiedName, "system_fs_read");
    EXPECT_EQ(batches[1][0].qualifiedName, "system_fs_write");

    auto results = DependencyGraph::executeBatches(batches, m_mgr, 4);

    // Reader finishes before writer starts
    ASSERT_EQ(m_records.size(), 2u);
    EXPECT_LE(m_records[0].endUs, m_records[1].startUs)
        << "Expected reader batch to complete before writer batch starts";
}

TEST_F(PipelineExecutionTest, ReadWriteAfterReadersAndWriters_Ordering) {
    addMockReader("system_fs_read", 20);
    addMockWriter("system_fs_write", 20);
    addMockReadWrite("system_bash_bash", 20);

    std::vector<ToolInvocation> invs = {
        {"system_fs_read", {}},
        {"system_fs_write", {}},
        {"system_bash_bash", {}}
    };
    auto batches = DependencyGraph::buildBatches(invs);
    ASSERT_EQ(batches.size(), 3u);

    auto results = DependencyGraph::executeBatches(batches, m_mgr, 4);

    ASSERT_GE(results.size(), 3u);
    ASSERT_EQ(m_records.size(), 3u);
    // Reader completes → writer completes → read-write completes
    EXPECT_LE(m_records[0].endUs, m_records[1].startUs);
    EXPECT_LE(m_records[1].endUs, m_records[2].startUs);
}

TEST_F(PipelineExecutionTest, ErrorInReaderDoesNotBlockOtherReaders) {
    m_mgr->registerHandler("system_fs_read", [](const json&, const HandlerContext&) {
        return ::a0::HandlerResult{"ERROR: read failed", {}};
    });
    addMockReader("system_fs_glob", 20);

    std::vector<ToolInvocation> invs = {
        {"system_fs_read", {}},
        {"system_fs_glob", {}}
    };
    auto batches = DependencyGraph::buildBatches(invs);
    auto results = DependencyGraph::executeBatches(batches, m_mgr, 4);

    ASSERT_EQ(results.size(), 1u);
    ASSERT_EQ(results[0].outputs.size(), 2u);
    // First result has error
    EXPECT_EQ(results[0].outputs[0], "ERROR: read failed");
    ASSERT_FALSE(results[0].errors.empty());
    // Second result is fine (not blocked)
    EXPECT_EQ(results[0].outputs[1], "system_fs_glob:ok");
}

TEST_F(PipelineExecutionTest, ToolsInReaderBatchReturnInOrder) {
    addMockReader("system_fs_read", 100);
    addMockReader("system_fs_glob", 20);
    addMockReader("system_fs_grep", 50);

    std::vector<ToolInvocation> invs = {
        {"system_fs_read", {}},
        {"system_fs_glob", {}},
        {"system_fs_grep", {}}
    };
    auto batches = DependencyGraph::buildBatches(invs);
    auto results = DependencyGraph::executeBatches(batches, m_mgr, 4);

    ASSERT_EQ(results.size(), 1u);
    ASSERT_EQ(results[0].outputs.size(), 3u);
    // Outputs must be in invocation order, not completion order
    EXPECT_EQ(results[0].outputs[0], "system_fs_read:ok");
    EXPECT_EQ(results[0].outputs[1], "system_fs_glob:ok");
    EXPECT_EQ(results[0].outputs[2], "system_fs_grep:ok");
}

TEST_F(PipelineExecutionTest, MixedReadersAndWriters) {
    addMockReader("system_fs_read", 20);
    addMockReader("system_fs_glob", 20);
    addMockWriter("system_fs_write", 20);
    addMockWriter("system_fs_edit", 20);
    addMockReader("system_fs_grep", 20);

    std::vector<ToolInvocation> invs = {
        {"system_fs_read", {}},
        {"system_fs_write", {}},
        {"system_fs_glob", {}},
        {"system_fs_edit", {}},
        {"system_fs_grep", {}}
    };
    auto batches = DependencyGraph::buildBatches(invs);
    // Batch 0: readers (read, glob, grep)
    // Batch 1: writer (write)
    // Batch 2: writer (edit)
    ASSERT_EQ(batches.size(), 3u);
    EXPECT_EQ(batches[0].size(), 3u);
    EXPECT_EQ(batches[1].size(), 1u);
    EXPECT_EQ(batches[2].size(), 1u);

    auto results = DependencyGraph::executeBatches(batches, m_mgr, 4);
    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].outputs.size(), 3u);
    EXPECT_EQ(results[1].outputs.size(), 1u);
    EXPECT_EQ(results[2].outputs.size(), 1u);

    // Verify ordering: All readers finish before first writer starts.
    // m_records tracks execution order: readers first, then writers.
    ASSERT_EQ(m_records.size(), 5u);
    // First 3 records are readers, last 2 are writers
    // All reader end times must be <= first writer start time
    for (int i = 0; i < 3; ++i) {
        EXPECT_LE(m_records[i].endUs, m_records[3].startUs)
            << "Reader '" << m_records[i].toolName << "' finished after writer started";
    }
    // First writer finishes before second writer starts
    EXPECT_LE(m_records[3].endUs, m_records[4].startUs);
}
