#pragma once

#include "handler_results.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace a0::skills { class SkillManager; }
namespace a0::persistence { class PersistenceStore; }

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

// Meta handlers (need SkillManager)
HandlerResult xShowSkills(const json& params, a0::skills::SkillManager* skillMgr);
HandlerResult xShowSkillTools(const json& params, a0::skills::SkillManager* skillMgr);

// Task manager handlers (need PersistenceStore)
HandlerResult xAddTask(const json& params, a0::persistence::PersistenceStore* db);
HandlerResult xRemoveTask(const json& params, a0::persistence::PersistenceStore* db);
HandlerResult xListTasks(const json& params, a0::persistence::PersistenceStore* db);
HandlerResult xSetTaskPriority(const json& params, a0::persistence::PersistenceStore* db);

}
