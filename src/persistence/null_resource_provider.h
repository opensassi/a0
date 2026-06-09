#pragma once

#include <memory>
#include <string>
#include <cstdint>

#include "shared/resource_provider.h"

namespace a0::persistence {

/// No-op ResourceProvider for testing.
///
/// create() returns a NullWriter:
///   - id() returns 0
///   - append() and close() do nothing
///   - closed() always returns true
///
/// open() returns a NullHandle:
///   - id() returns 0
///   - hasMore() returns false
///   - readNext() returns empty string
///   - read() returns empty string
///   - size() returns 0
class NullResourceProvider : public ResourceProvider {
public:
    NullResourceProvider() = default;
    ~NullResourceProvider() override = default;

    std::unique_ptr<ResourceWriter> create(ResourceType type) override;
    std::unique_ptr<ResourceHandle> open(ResourceType type, int64_t id) override;
};

} // namespace a0::persistence
