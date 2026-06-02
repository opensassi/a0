#pragma once

#include "handler_results.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class InferenceProvider;

namespace a0::skills { class SkillManager; }

namespace a0 {

// Core handlers
HandlerResult xBash(const json& params);
HandlerResult xRead(const json& params);
HandlerResult xGlob(const json& params);
HandlerResult xGrep(const json& params);
HandlerResult xEdit(const json& params);
HandlerResult xWrite(const json& params);

// Git handler
HandlerResult xGitCommand(const std::string& subcommand, const json& params);

// Meta handlers (need SkillManager / InferenceProvider)
HandlerResult xShowSkills(const json& params, a0::skills::SkillManager* skillMgr);
HandlerResult xShowSkillTools(const json& params, a0::skills::SkillManager* skillMgr);
HandlerResult xToolsForPrompt(const json& params,
                              a0::skills::SkillManager* skillMgr,
                              InferenceProvider* provider);

}
