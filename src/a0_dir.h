#pragma once

#include <string>

namespace a0 {

/// Ensure the .a0/ directory exists at @p a0Path.
/// Creates it (and parent dirs) if missing.
/// On first creation, if the CWD is a git repository, appends ".a0/" to .gitignore.
///
/// \param a0Path  Path to the .a0/ directory (e.g. "./.a0").
/// \retval 0  Directory was newly created (gitignore may have been updated).
/// \retval 1  Directory already existed.
/// \retval -1 Failed to create directory.
int ensureA0Dir(const std::string& a0Path);

} // namespace a0
