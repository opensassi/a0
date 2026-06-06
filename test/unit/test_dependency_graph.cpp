#include "dependency_graph.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace a0;

// ===========================================================================
// classifyTool
// ===========================================================================

TEST(DependencyGraphTest, Classify_Reader_FsRead) {
    EXPECT_EQ(DependencyGraph::classifyTool("system-fs-read"), ResourceClass::READER);
}

TEST(DependencyGraphTest, Classify_Reader_FsGlob) {
    EXPECT_EQ(DependencyGraph::classifyTool("system-fs-glob"), ResourceClass::READER);
}

TEST(DependencyGraphTest, Classify_Reader_FsGrep) {
    EXPECT_EQ(DependencyGraph::classifyTool("system-fs-grep"), ResourceClass::READER);
}

TEST(DependencyGraphTest, Classify_Reader_MetaShowSkills) {
    EXPECT_EQ(DependencyGraph::classifyTool("system-meta-show_skills"), ResourceClass::READER);
}

TEST(DependencyGraphTest, Classify_Writer_FsWrite) {
    EXPECT_EQ(DependencyGraph::classifyTool("system-fs-write"), ResourceClass::WRITER);
}

TEST(DependencyGraphTest, Classify_Writer_FsEdit) {
    EXPECT_EQ(DependencyGraph::classifyTool("system-fs-edit"), ResourceClass::WRITER);
}

TEST(DependencyGraphTest, Classify_ReadWrite_Bash) {
    EXPECT_EQ(DependencyGraph::classifyTool("system-bash-bash"), ResourceClass::READ_WRITE);
}

TEST(DependencyGraphTest, Classify_ReadWrite_Git) {
    EXPECT_EQ(DependencyGraph::classifyTool("system-git-status"), ResourceClass::READ_WRITE);
}

TEST(DependencyGraphTest, Classify_ReadWrite_CommandTool) {
    EXPECT_EQ(DependencyGraph::classifyTool("local-system_design-extract_artifacts"),
              ResourceClass::READ_WRITE);
}

TEST(DependencyGraphTest, Classify_ReadWrite_Unknown) {
    EXPECT_EQ(DependencyGraph::classifyTool("nonexistent-tool"), ResourceClass::READ_WRITE);
}

// ===========================================================================
// buildBatches
// ===========================================================================

TEST(DependencyGraphTest, BuildBatches_Empty) {
    auto batches = DependencyGraph::buildBatches({});
    EXPECT_TRUE(batches.empty());
}

TEST(DependencyGraphTest, BuildBatches_SingleReader) {
    std::vector<ToolInvocation> invs = {{"system-fs-read", {{"file_path", "/tmp"}}}};
    auto batches = DependencyGraph::buildBatches(invs);
    ASSERT_EQ(batches.size(), 1u);
    ASSERT_EQ(batches[0].size(), 1u);
    EXPECT_EQ(batches[0][0].qualifiedName, "system-fs-read");
}

TEST(DependencyGraphTest, BuildBatches_SingleWriter) {
    std::vector<ToolInvocation> invs = {{"system-fs-write", {{"file_path", "/tmp/f"}}}};
    auto batches = DependencyGraph::buildBatches(invs);
    ASSERT_EQ(batches.size(), 1u);
    ASSERT_EQ(batches[0].size(), 1u);
    EXPECT_EQ(batches[0][0].qualifiedName, "system-fs-write");
}

TEST(DependencyGraphTest, BuildBatches_ReadersInOneBatch) {
    std::vector<ToolInvocation> invs = {
        {"system-fs-read", {}},
        {"system-fs-glob", {}},
        {"system-fs-grep", {}}
    };
    auto batches = DependencyGraph::buildBatches(invs);
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].size(), 3u);
}

TEST(DependencyGraphTest, BuildBatches_WritersSeparateBatches) {
    std::vector<ToolInvocation> invs = {
        {"system-fs-write", {}},
        {"system-fs-edit", {}}
    };
    auto batches = DependencyGraph::buildBatches(invs);
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[0].size(), 1u);
    EXPECT_EQ(batches[1].size(), 1u);
    EXPECT_EQ(batches[0][0].qualifiedName, "system-fs-write");
    EXPECT_EQ(batches[1][0].qualifiedName, "system-fs-edit");
}

TEST(DependencyGraphTest, BuildBatches_ReadersBeforeWriters) {
    std::vector<ToolInvocation> invs = {
        {"system-fs-read", {}},
        {"system-fs-write", {}},
        {"system-fs-glob", {}}
    };
    auto batches = DependencyGraph::buildBatches(invs);
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[0].size(), 2u);  // readers: read + glob
    EXPECT_EQ(batches[1].size(), 1u);  // writer: write
}

TEST(DependencyGraphTest, BuildBatches_ReadWriteAfterAllWriters) {
    std::vector<ToolInvocation> invs = {
        {"system-fs-read", {}},
        {"system-bash-bash", {}},
        {"system-fs-write", {}},
        {"system-git-status", {}}
    };
    auto batches = DependencyGraph::buildBatches(invs);
    // Batch 0: readers (read)
    // Batch 1: writers (write)
    // Batch 2+: read_write (bash, git-status)
    ASSERT_GE(batches.size(), 3u);
    EXPECT_EQ(batches[0].size(), 1u);
    EXPECT_EQ(batches[0][0].qualifiedName, "system-fs-read");
    EXPECT_EQ(batches[1].size(), 1u);
    EXPECT_EQ(batches[1][0].qualifiedName, "system-fs-write");
    // read-write tools in subsequent batches
    for (size_t i = 2; i < batches.size(); ++i) {
        EXPECT_EQ(batches[i].size(), 1u);
    }
}

TEST(DependencyGraphTest, BuildBatches_MixedPreservesOrderByClass) {
    std::vector<ToolInvocation> invs = {
        {"system-fs-write", {{"file_path", "a"}}},
        {"system-fs-read", {{"file_path", "b"}}},
        {"system-fs-edit", {{"old_string", "x"}}},
        {"system-fs-glob", {{"pattern", "**/*.cpp"}}}
    };
    auto batches = DependencyGraph::buildBatches(invs);
    // Batch 0: readers (read, glob)
    // Batch 1: writer (write)
    // Batch 2: writer (edit)
    ASSERT_EQ(batches.size(), 3u);
    EXPECT_EQ(batches[0].size(), 2u);  // readers together
    EXPECT_EQ(batches[1].size(), 1u);
    EXPECT_EQ(batches[1][0].qualifiedName, "system-fs-write");
    EXPECT_EQ(batches[2].size(), 1u);
    EXPECT_EQ(batches[2][0].qualifiedName, "system-fs-edit");
}

// ===========================================================================
// xExecuteOne (via executeBatches with null skillMgr)
// ===========================================================================

TEST(DependencyGraphTest, ExecuteBatches_NullSkillManager) {
    std::vector<ToolInvocation> invs = {{"system-fs-read", {{"file_path", "/tmp"}}}};
    auto batches = DependencyGraph::buildBatches(invs);
    auto results = DependencyGraph::executeBatches(batches, nullptr, 4);
    ASSERT_EQ(results.size(), 1u);
    ASSERT_EQ(results[0].outputs.size(), 1u);
    EXPECT_TRUE(results[0].outputs[0].find("ERROR:") != std::string::npos);
}

TEST(DependencyGraphTest, ExecuteBatches_EmptyBatches) {
    auto results = DependencyGraph::executeBatches({}, nullptr, 4);
    EXPECT_TRUE(results.empty());
}

// ===========================================================================
// Reader/Writer classification edge cases
// ===========================================================================

TEST(DependencyGraphTest, BuildBatches_ReadersDeduplicated) {
    std::vector<ToolInvocation> invs = {
        {"system-fs-read", {{"a", 1}}},
        {"system-fs-read", {{"a", 2}}},
    };
    auto batches = DependencyGraph::buildBatches(invs);
    // Both readers should be in a single batch
    ASSERT_EQ(batches.size(), 1u);
    ASSERT_EQ(batches[0].size(), 2u);
    EXPECT_EQ(batches[0][0].qualifiedName, "system-fs-read");
    EXPECT_EQ(batches[0][1].qualifiedName, "system-fs-read");
}

TEST(DependencyGraphTest, BuildBatches_WriterAndReadWrite) {
    std::vector<ToolInvocation> invs = {
        {"system-fs-write", {{"file_path", "a"}}},
        {"system-bash-bash", {{"command", "echo"}}}
    };
    auto batches = DependencyGraph::buildBatches(invs);
    // writer batch first, then read_write
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[0][0].qualifiedName, "system-fs-write");
    EXPECT_EQ(batches[1][0].qualifiedName, "system-bash-bash");
}

TEST(DependencyGraphTest, BuildBatches_MetaPrefixes) {
    EXPECT_EQ(DependencyGraph::classifyTool("system-meta-123"), ResourceClass::READER);
    EXPECT_EQ(DependencyGraph::classifyTool("system-meta-show_skills"), ResourceClass::READER);
}
