#pragma once

#include <filesystem>
#include <string>

namespace ntrak::common {

/// Returns the directory containing the running executable.
std::filesystem::path executableDir();

/// Resolves the full path to a bundled asset file (e.g. "NotoSansMono.ttf").
/// Search order: $APPDIR/usr/share/ntrak/assets/ -> <exe_dir>/assets/
std::filesystem::path assetPath(const std::string& filename);

/// Resolves the full path to the bundled engine configs file.
/// Search order: $APPDIR/usr/share/ntrak/config/engine_configs.json ->
/// <exe_dir>/config/engine_configs.json
std::filesystem::path bundledEngineConfigPath();

/// Resolves the full path to the optional user engine override file.
/// On Linux: $XDG_CONFIG_HOME/ntrak/engine_overrides.json or
/// ~/.config/ntrak/engine_overrides.json.
/// Returns an empty path when no user config location is available.
std::filesystem::path userEngineOverridePath();

/// Resolves the full path to the bundled USER_GUIDE.md manual.
/// Search order: $APPDIR/usr/share/ntrak/docs/ -> <exe_dir>/docs/
std::filesystem::path userGuidePath();

}  // namespace ntrak::common
