#pragma once

#include <string>

namespace ntrak::common {

/// Simple logger that writes diagnostic information to help debug startup issues.
/// On Windows, writes to ntrak_debug.log in the executable directory.
class Logger {
public:
    static void init();
    static void log(const std::string& message);
    static void logError(const std::string& message);
    static void shutdown();

private:
    Logger() = default;
};

}  // namespace ntrak::common
