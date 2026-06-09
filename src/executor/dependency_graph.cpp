#include "executor/dependency_graph.h"

#include "executor/command_runner.h"
#include "skills/skills.h"
#include <algorithm>
#include <iostream>
#include <unordered_set>

namespace a0 {

// ---------------------------------------------------------------------------
// Built-in classification table
// ---------------------------------------------------------------------------

static const std::unordered_set<std::string> s_readerPrefixes = {
    "system_fs_read",
    "system_fs_glob",
    "system_fs_grep",
    "system_meta_",
};
static const std::unordered_set<std::string> s_writerPrefixes = {
    "system_fs_write",
    "system_fs_edit",
};

static bool xStartsWithAny(const std::string& s,
                            const std::unordered_set<std::string>& prefixes) {
    for (const auto& p : prefixes) {
        if (s == p || s.find(p) == 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// classifyTool
// ---------------------------------------------------------------------------

ResourceClass DependencyGraph::classifyTool(const std::string& qualifiedName)
{
    if (xStartsWithAny(qualifiedName, s_readerPrefixes)) {
        return ResourceClass::READER;
    }
    if (xStartsWithAny(qualifiedName, s_writerPrefixes)) {
        return ResourceClass::WRITER;
    }
    return ResourceClass::READ_WRITE;
}

// ---------------------------------------------------------------------------
// buildBatches
// ---------------------------------------------------------------------------

std::vector<std::vector<ToolInvocation>> DependencyGraph::buildBatches(
    const std::vector<ToolInvocation>& invocations)
{
    std::vector<ToolInvocation> readers, writers, readWrites;
    for (const auto& inv : invocations) {
        switch (classifyTool(inv.qualifiedName)) {
            case ResourceClass::READER:     readers.push_back(inv); break;
            case ResourceClass::WRITER:     writers.push_back(inv); break;
            case ResourceClass::READ_WRITE: readWrites.push_back(inv); break;
        }
    }

    std::vector<std::vector<ToolInvocation>> batches;

    // Batch 0: all readers (safe to parallelize within the batch)
    if (!readers.empty()) {
        batches.push_back(std::move(readers));
    }

    // Each writer gets its own batch (serialized)
    for (auto& w : writers) {
        std::vector<ToolInvocation> batch;
        batch.push_back(std::move(w));
        batches.push_back(std::move(batch));
    }

    // Each read-write tool gets its own batch (serialized, after writers)
    for (auto& rw : readWrites) {
        std::vector<ToolInvocation> batch;
        batch.push_back(std::move(rw));
        batches.push_back(std::move(batch));
    }

    return batches;
}

// ---------------------------------------------------------------------------
// xExecuteOne
// ---------------------------------------------------------------------------

json DependencyGraph::xExecuteOne(const ToolInvocation& inv,
                                   a0::skills::SkillManager* skillMgr)
{
    if (!skillMgr) {
        return json("ERROR: no SkillManager available");
    }
    auto hr = skillMgr->executeToolWithMeta(inv.qualifiedName, inv.params,
                                             inv.seq, inv.toolCallId,
                                             inv.subSessionId);
    return json(hr.output);
}

// ---------------------------------------------------------------------------
// executeBatches
// ---------------------------------------------------------------------------

std::vector<BatchResult> DependencyGraph::executeBatches(
    const std::vector<std::vector<ToolInvocation>>& batches,
    a0::skills::SkillManager* skillMgr,
    int maxParallel)
{
    std::vector<BatchResult> results;
    results.reserve(batches.size());

    for (const auto& batch : batches) {
        BatchResult br;
        br.outputs.reserve(batch.size());
        br.errors.reserve(batch.size());

        // Collect subprocess commands when the entire batch is READER
        // and consists of command (non-system) tools. For system tools,
        // run sequentially (they're fast C++ handlers).
        bool allPureReaders = true;
        std::vector<std::string> subprocessCommands;
        std::vector<size_t> subprocessIndices;

        if (!batch.empty() && classifyTool(batch[0].qualifiedName) == ResourceClass::READER) {
            // Check if all tools in the batch are command tools
            for (size_t i = 0; i < batch.size(); ++i) {
                a0::skills::SkillTool toolDef;
                bool found = skillMgr &&
                    skillMgr->getTool(batch[i].qualifiedName, toolDef) == 0;
                if (found && !toolDef.systemTool) {
                    // Command tool — collect for parallel execution
                    std::string cmd = toolDef.command;
                    if (!cmd.empty()) {
                        subprocessCommands.push_back(cmd);
                        subprocessIndices.push_back(i);
                    }
                }
            }
            allPureReaders = subprocessCommands.size() == batch.size();
        } else {
            allPureReaders = false;
        }

        if (allPureReaders && subprocessCommands.size() > 1) {
            // Parallel execution via CommandRunner::runAll
            auto cmdResults = CommandRunner::runAll(
                subprocessCommands, 30, maxParallel);
            for (size_t i = 0; i < batch.size(); ++i) {
                // Find index in subprocess results
                auto it = std::find(subprocessIndices.begin(),
                                    subprocessIndices.end(), i);
                if (it != subprocessIndices.end()) {
                    size_t idx = std::distance(subprocessIndices.begin(), it);
                    if (idx < cmdResults.size()) {
                        if (cmdResults[idx].timedOut) {
                            br.errors.push_back("ERROR: timeout");
                            br.outputs.push_back("");
                        } else if (cmdResults[idx].exitCode != 0) {
                            br.errors.push_back("ERROR: exit code " +
                                std::to_string(cmdResults[idx].exitCode));
                            br.outputs.push_back(cmdResults[idx].stdout);
                        } else {
                            br.outputs.push_back(cmdResults[idx].stdout);
                        }
                    }
                }
            }
        } else {
            // Sequential execution
            for (const auto& inv : batch) {
                try {
                    json result = xExecuteOne(inv, skillMgr);
                    std::string s = result.is_string() ? result.get<std::string>()
                                                       : result.dump();
                    if (s.find("ERROR:") == 0) {
                        br.errors.push_back(s);
                    }
                    br.outputs.push_back(s);
                } catch (const std::exception& e) {
                    br.errors.push_back(std::string("ERROR: ") + e.what());
                    br.outputs.push_back("");
                }
            }
        }

        results.push_back(std::move(br));
    }

    return results;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool DependencyGraph::xIsReader(const std::string& qn)
{
    return classifyTool(qn) == ResourceClass::READER;
}

bool DependencyGraph::xIsWriter(const std::string& qn)
{
    return classifyTool(qn) == ResourceClass::WRITER;
}

} // namespace a0
