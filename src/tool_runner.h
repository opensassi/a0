#pragma once

#include "agent_interfaces.h"

class SubprocessToolRunner : public ToolRunner {
public:
    json run(const Tool& tool, const json& params) override;
};
