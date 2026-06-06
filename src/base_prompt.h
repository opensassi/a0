#pragma once

#include <string>

namespace a0 { namespace skills { class SkillManager; } }

namespace a0 {

/// Build the base system prompt for all LLM sessions.
/// Loads the selected persona's prompt.md and substitutes {{VARS}}.
///
/// \param skillMgr     Loaded SkillManager (unused, reserved)
/// \param personaName  Persona name (default: "software-engineer")
std::string buildBasePrompt(const skills::SkillManager* skillMgr,
                             const std::string& personaName = "software-engineer");

} // namespace a0
