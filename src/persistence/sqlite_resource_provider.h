#pragma once

#include <memory>
#include <string>
#include <cstdint>

#include "shared/resource_provider.h"

namespace a0::persistence {

/// ResourceProvider implementation backed by the shared SQLite database.
///
/// Uses the same sessions.db as PersistenceStore. All resource types
/// (LlmStream, ToolOutput, TerminalStream, ToolInvocation) are stored
/// in the stream + stream_chunk tables.
///
/// Writer operations are dispatched to a dedicated background thread.
/// Reader operations query chunk data directly.
class SqliteResourceProvider : public ResourceProvider {
public:
    /// \param dbPath           Path to the SQLite database file.
    /// \param tokenFlushSize   Bytes before flushing LLM token stream (default 256).
    /// \param toolFlushSize    Bytes before flushing tool output stream (default 4096).
    /// \param outputPreviewSize  Max bytes for outputPreview in ToolEnd (default 4096).
    SqliteResourceProvider(const std::string& dbPath,
                           int64_t tokenFlushSize = 256,
                           int64_t toolFlushSize = 4096,
                           int64_t outputPreviewSize = 4096);
    ~SqliteResourceProvider() override;

    std::unique_ptr<ResourceWriter> create(ResourceType type) override;
    std::unique_ptr<ResourceHandle> open(ResourceType type, int64_t id) override;

    void setTokenFlushSize(int64_t bytes);
    void setToolFlushSize(int64_t bytes);
    void setOutputPreviewSize(int64_t bytes);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::persistence
