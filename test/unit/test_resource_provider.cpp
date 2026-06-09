#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>

#include "persistence/sqlite_resource_provider.h"
#include "persistence/null_resource_provider.h"
#include "shared/resource_provider.h"

namespace fs = std::filesystem;

// ============================================================================
// SqliteResourceProvider Tests
// ============================================================================

class SqliteResourceProviderTest : public ::testing::Test {
protected:
    std::string m_dbPath;
    std::unique_ptr<a0::persistence::SqliteResourceProvider> m_provider;

    void SetUp() override {
        m_dbPath = fs::temp_directory_path() / "test_res_provider.db";
        // Remove stale db
        std::error_code ec;
        fs::remove(m_dbPath, ec);
        m_provider = std::make_unique<a0::persistence::SqliteResourceProvider>(m_dbPath);
    }

    void TearDown() override {
        m_provider.reset();
        std::error_code ec;
        fs::remove(m_dbPath, ec);
    }
};

TEST_F(SqliteResourceProviderTest, CreateLlmStreamReturnsWriter) {
    auto writer = m_provider->create(a0::ResourceType::LlmStream);
    ASSERT_NE(writer, nullptr);
    EXPECT_GT(writer->id(), 0);
}

TEST_F(SqliteResourceProviderTest, CreateToolInvocationReturnsWriter) {
    auto writer = m_provider->create(a0::ResourceType::ToolInvocation);
    ASSERT_NE(writer, nullptr);
    EXPECT_GT(writer->id(), 0);
}

TEST_F(SqliteResourceProviderTest, WriteThenReadBack) {
    auto writer = m_provider->create(a0::ResourceType::LlmStream);
    ASSERT_NE(writer, nullptr);
    int64_t id = writer->id();

    writer->append("hello ");
    writer->append("world");
    writer->close();
    EXPECT_TRUE(writer->closed());

    auto handle = m_provider->open(a0::ResourceType::LlmStream, id);
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(handle->id(), id);
    EXPECT_TRUE(handle->hasMore());
    EXPECT_EQ(handle->readNext(), "hello world");
}

TEST_F(SqliteResourceProviderTest, WriteLargeDataCreatesMultipleChunks) {
    m_provider->setTokenFlushSize(10); // flush every 10 bytes
    auto writer = m_provider->create(a0::ResourceType::LlmStream);
    ASSERT_NE(writer, nullptr);
    int64_t id = writer->id();

    std::string data(25, 'x');
    writer->append(data);
    writer->close();

    auto handle = m_provider->open(a0::ResourceType::LlmStream, id);
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(handle->readNext(), data);
}

TEST_F(SqliteResourceProviderTest, OpenNonexistentReturnsEmpty) {
    auto handle = m_provider->open(a0::ResourceType::LlmStream, 99999);
    ASSERT_NE(handle, nullptr);
    EXPECT_FALSE(handle->hasMore());
    EXPECT_EQ(handle->readNext(), "");
    EXPECT_EQ(handle->size(), 0);
}

TEST_F(SqliteResourceProviderTest, ReadWithOffsetAndLimit) {
    auto writer = m_provider->create(a0::ResourceType::ToolInvocation);
    ASSERT_NE(writer, nullptr);
    int64_t id = writer->id();

    writer->append("0123456789ABCDEF");
    writer->close();

    auto handle = m_provider->open(a0::ResourceType::ToolInvocation, id);
    ASSERT_NE(handle, nullptr);

    // Read 5 bytes from offset 5
    EXPECT_EQ(handle->read(5, 5), "56789");
}

TEST_F(SqliteResourceProviderTest, MultipleWritesToDifferentStreams) {
    auto w1 = m_provider->create(a0::ResourceType::LlmStream);
    auto w2 = m_provider->create(a0::ResourceType::ToolInvocation);
    ASSERT_NE(w1, nullptr);
    ASSERT_NE(w2, nullptr);

    int64_t id1 = w1->id();
    int64_t id2 = w2->id();

    EXPECT_NE(id1, id2);

    w1->append("stream1");
    w1->close();
    w2->append("stream2");
    w2->close();

    auto h1 = m_provider->open(a0::ResourceType::LlmStream, id1);
    auto h2 = m_provider->open(a0::ResourceType::ToolInvocation, id2);
    ASSERT_NE(h1, nullptr);
    ASSERT_NE(h2, nullptr);

    EXPECT_EQ(h1->readNext(), "stream1");
    EXPECT_EQ(h2->readNext(), "stream2");
}

TEST_F(SqliteResourceProviderTest, ConfigSettersWork) {
    m_provider->setTokenFlushSize(512);
    m_provider->setToolFlushSize(8192);
    m_provider->setOutputPreviewSize(8192);
    // No getters, but verify no crash and subsequent writes work
    auto writer = m_provider->create(a0::ResourceType::LlmStream);
    ASSERT_NE(writer, nullptr);
    writer->append("test");
    writer->close();
}

// ============================================================================
// NullResourceProvider Tests
// ============================================================================

TEST(NullResourceProviderTest, CreateReturnsNonNullWriter) {
    a0::persistence::NullResourceProvider provider;
    auto writer = provider.create(a0::ResourceType::LlmStream);
    ASSERT_NE(writer, nullptr);
    EXPECT_EQ(writer->id(), 0);
}

TEST(NullResourceProviderTest, WriterIsAlwaysClosed) {
    a0::persistence::NullResourceProvider provider;
    auto writer = provider.create(a0::ResourceType::ToolInvocation);
    EXPECT_TRUE(writer->closed());
}

TEST(NullResourceProviderTest, AppendAndCloseNoop) {
    a0::persistence::NullResourceProvider provider;
    auto writer = provider.create(a0::ResourceType::LlmStream);
    ASSERT_NO_THROW(writer->append("data"));
    ASSERT_NO_THROW(writer->close());
}

TEST(NullResourceProviderTest, OpenReturnsEmptyHandle) {
    a0::persistence::NullResourceProvider provider;
    auto handle = provider.open(a0::ResourceType::LlmStream, 42);
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(handle->id(), 0);
    EXPECT_FALSE(handle->hasMore());
    EXPECT_EQ(handle->readNext(), "");
    EXPECT_EQ(handle->read(0, 10), "");
    EXPECT_EQ(handle->size(), 0);
}

TEST(NullResourceProviderTest, AllResourceTypesReturnValidWriter) {
    a0::persistence::NullResourceProvider provider;
    for (auto type : {a0::ResourceType::LlmStream, a0::ResourceType::ToolOutput,
                       a0::ResourceType::TerminalStream, a0::ResourceType::ToolInvocation}) {
        auto writer = provider.create(type);
        ASSERT_NE(writer, nullptr) << "Failed for type " << static_cast<int>(type);
        EXPECT_TRUE(writer->closed());
    }
}

// ============================================================================
// ResourceProvider Interface Tests
// ============================================================================

TEST(ResourceProviderInterfaceTest, UniquePtrOwnership) {
    a0::persistence::NullResourceProvider provider;
    std::unique_ptr<a0::ResourceWriter> writer = provider.create(a0::ResourceType::LlmStream);
    ASSERT_NE(writer, nullptr);
    // ownership transferred, original unique_ptr invalidated
    auto* raw = writer.release();
    ASSERT_NE(raw, nullptr);
    delete raw;
}
