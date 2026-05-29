#pragma once

#include <string>

class SkillRegistry;

namespace a0 {

/// Build the base system prompt for all LLM sessions.
/// Includes agent identity (binary SHA), environment info (uname),
/// and parameterized descriptions of all available system tools.
///
/// Should be called once at startup and cached — the result is
/// immutable for the lifetime of the process.
///
/// \param registry  Loaded SkillRegistry containing system tool JSON definitions
std::string buildBasePrompt(const SkillRegistry* registry);

} // namespace a0
