#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace ntrak::common {

/// Renders USER_GUIDE.md to a temporary HTML file and opens it with the
/// system default application.
///
/// Returns the opened HTML path on success.
std::expected<std::filesystem::path, std::string> openUserGuideInDefaultApp();

}  // namespace ntrak::common

