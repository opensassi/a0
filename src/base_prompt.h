#pragma once

#include <string>

namespace a0 { namespace skills { class SkillManager; } }

namespace a0 {

/// Build the base system prompt for all LLM sessions.
/// Includes agent identity (binary SHA), environment info (uname),
/// and parameterized descriptions of all available system tools.
///
/// Should be called once at startup and cached — the result is
/// immutable for the lifetime of the process.
///
/// \param skillMgr  Loaded SkillManager for tool definition lookups
std::string buildBasePrompt(const skills::SkillManager* skillMgr);

} // namespace a0
