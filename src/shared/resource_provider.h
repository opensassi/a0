#pragma once

#include <memory>
#include <string>
#include <cstdint>

namespace a0 {

using StreamId = int64_t;
using InvocationId = int64_t;

enum class ResourceType { LlmStream, ToolOutput, TerminalStream, ToolInvocation };

class ResourceHandle {
public:
    virtual ~ResourceHandle() = default;
    virtual int64_t id() const = 0;
    virtual bool hasMore() const = 0;
    virtual std::string readNext() = 0;
    virtual std::string read(int64_t offset, int64_t limit) = 0;
    virtual int64_t size() const = 0;
};

class ResourceWriter {
public:
    virtual ~ResourceWriter() = default;
    virtual int64_t id() const = 0;
    virtual void append(const std::string& data) = 0;
    virtual void close() = 0;
    virtual bool closed() const = 0;
};

class ResourceProvider {
public:
    virtual ~ResourceProvider() = default;
    virtual std::unique_ptr<ResourceWriter> create(ResourceType type) = 0;
    virtual std::unique_ptr<ResourceHandle> open(ResourceType type, int64_t id) = 0;
};

} // namespace a0
