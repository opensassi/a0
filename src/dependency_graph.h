#pragma once

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace a0 { namespace skills { class SkillManager; struct SkillTool; } }

namespace a0 {

using json = nlohmann::json;

/// How a tool accesses shared state (the filesystem).
enum class ResourceClass {
    READER,      ///< Pure filesystem reads  — safe to run in parallel with other readers
    WRITER,      ///< Pure filesystem writes — must not overlap with readers or other writers
    READ_WRITE   ///< Opaque/unknown access  — treat as write; run after all readers
};

struct ToolInvocation {
    std::string qualifiedName;
    json params;
    int* seq = nullptr;             ///< sequence counter for persistence (nullptr = skip)
    std::string toolCallId;
    int64_t subSessionId = 0;
};

struct BatchResult {
    std::vector<std::string> outputs;   ///< one per invocation, same order as input
    std::vector<std::string> errors;    ///< non-empty for invocations that failed
};

/// Builds safe execution order from a set of tool invocations.
///
/// Reads and writes to the shared filesystem are the only resource constraint.
/// Rules:
///   - READERs have no ordering constraints among themselves
///   - WRITERs must execute one-at-a-time, after all READERs
///   - READ_WRITE (bash, git, command tools) execute one-at-a-time after WRITERs
class DependencyGraph {
public:
    /// Classify a tool by its qualified name (system handler path or command tool).
    static ResourceClass classifyTool(const std::string& qualifiedName);

    /// Build execution batches.
    /// Batch 0 = all READERs (safe to parallelize).
    /// Batch 1+ = one WRITER per batch (serialized).
    /// Batch N+ = one READ_WRITE per batch (serialized).
    /// Empty batches are omitted.
    static std::vector<std::vector<ToolInvocation>> buildBatches(
        const std::vector<ToolInvocation>& invocations);

    /// Execute batches through SkillManager.
    /// READER batch tools are passed to executeToolWithMeta in list order.
    /// WRITER and READ_WRITE batches execute one tool at a time.
    /// \param batches      Batches from buildBatches().
    /// \param skillMgr     Loaded SkillManager with registered handlers + runners.
    /// \param maxParallel  Max workers (currently used for subprocess fan-out; default 4).
    static std::vector<BatchResult> executeBatches(
        const std::vector<std::vector<ToolInvocation>>& batches,
        a0::skills::SkillManager* skillMgr,
        int maxParallel = 4);

private:
    static bool xIsReader(const std::string& qn);
    static bool xIsWriter(const std::string& qn);
    static json xExecuteOne(const ToolInvocation& inv,
                             a0::skills::SkillManager* skillMgr);
};

} // namespace a0
