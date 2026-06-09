#pragma once

#include "shared/agent_interfaces.h"

class SubprocessToolRunner : public ToolRunner {
public:
    json run(const Tool& tool, const json& params) override;
    a0::StreamHandle runStreaming(const Tool& tool,
                                   const json& params,
                                   a0::StreamCallback onChunk) override;
};
