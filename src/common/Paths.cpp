#include "ntrak/common/Paths.hpp"

#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>

#include <climits>
#endif

namespace ntrak::common {
namespace {

#ifdef _WIN32
std::filesystem::path getExecutablePath() {
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buf);
}
#else
std::filesystem::path getExecutablePath() {
    char buf[PATH_MAX];
    const ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) {
        return {};
    }
    buf[len] = '\0';
    return std::filesystem::path(buf);
}
#endif

/// Returns true if the file exists at the given path.
bool fileExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

/// Returns the bundled data directory, which is where read-only resources
/// (assets, default config) are installed.
///
/// Search order:
/// 1. $APPDIR/usr/share/ntrak/  (AppImage on Linux)
/// 2. <exe_dir>/                (development builds, Windows)
std::filesystem::path bundledDataDir() {
#ifndef _WIN32
    if (const char* appDir = std::getenv("APPDIR"); appDir != nullptr) {
        auto candidate = std::filesystem::path(appDir) / "usr" / "share" / "ntrak";
        std::error_code ec;
        if (std::filesystem::is_directory(candidate, ec) && !ec) {
            return candidate;
        }
    }
#endif
    return executableDir();
}

#ifndef _WIN32
/// Returns the user config directory for ntrak on Linux.
/// Uses $XDG_CONFIG_HOME/ntrak/ or falls back to ~/.config/ntrak/.
std::filesystem::path userConfigDir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
        return std::filesystem::path(xdg) / "ntrak";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path(home) / ".config" / "ntrak";
    }
    return {};
}
#endif

}  // namespace

std::filesystem::path executableDir() {
    static const std::filesystem::path dir = getExecutablePath().parent_path();
    return dir;
}

std::filesystem::path assetPath(const std::string& filename) {
    const auto dataDir = bundledDataDir();
    auto candidate = dataDir / "assets" / filename;
    if (fileExists(candidate)) {
        return candidate;
    }

    // Fallback: try exe-relative directly (in case bundledDataDir returned
    // something other than executableDir, e.g. an APPDIR that doesn't have the file).
    auto exeRelative = executableDir() / "assets" / filename;
    if (fileExists(exeRelative)) {
        return exeRelative;
    }

    // Return the primary candidate path even if it doesn't exist, so callers
    // get a meaningful error about what path was tried.
    return candidate;
}

std::filesystem::path bundledEngineConfigPath() {
    constexpr const char* kConfigFilename = "engine_configs.json";

    // Find the bundled config.
    const auto dataDir = bundledDataDir();
    auto bundledConfig = dataDir / "config" / kConfigFilename;

    if (!fileExists(bundledConfig)) {
        // Fallback: try exe-relative.
        auto exeRelative = executableDir() / "config" / kConfigFilename;
        if (fileExists(exeRelative)) {
            bundledConfig = exeRelative;
        }
    }

    return bundledConfig;
}

std::filesystem::path userEngineOverridePath() {
#ifdef _WIN32
    return {};
#else
    const auto userDir = userConfigDir();
    if (userDir.empty()) {
        return {};
    }
    return userDir / "engine_overrides.json";
#endif
}

std::filesystem::path userGuidePath() {
    constexpr const char* kGuideFilename = "USER_GUIDE.md";
    const auto dataDir = bundledDataDir();
    auto bundledGuide = dataDir / "docs" / kGuideFilename;
    if (fileExists(bundledGuide)) {
        return bundledGuide;
    }

    auto exeRelative = executableDir() / "docs" / kGuideFilename;
    if (fileExists(exeRelative)) {
        return exeRelative;
    }

    return bundledGuide;
}

}  // namespace ntrak::common
