#pragma once

#include <string>

namespace empower {

// Absolute path to the directory containing the running executable.
// Used to locate sibling files (the sentry-crash daemon, the external crash
// reporter, bundled assets) regardless of the current working directory.
std::string executable_dir();

// Joins a directory and a filename with the platform path separator.
std::string path_join(const std::string& dir, const std::string& name);

} // namespace empower
