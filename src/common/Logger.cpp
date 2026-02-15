#include "ntrak/common/Logger.hpp"
#include "ntrak/common/Paths.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>

namespace ntrak::common {
namespace {

std::ofstream logFile;
std::mutex logMutex;

std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

}  // namespace

void Logger::init() {
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(logMutex);
    auto logPath = executableDir() / "ntrak_debug.log";
    logFile.open(logPath, std::ios::out | std::ios::app);
    if (logFile.is_open()) {
        logFile << "\n=== ntrak startup " << timestamp() << " ===\n";
        logFile.flush();
    }
#endif
}

void Logger::log(const std::string& message) {
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFile.is_open()) {
        logFile << "[INFO ] " << timestamp() << " - " << message << '\n';
        logFile.flush();
    }
#else
    (void)message;  // Suppress unused parameter warning
#endif
}

void Logger::logError(const std::string& message) {
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFile.is_open()) {
        logFile << "[ERROR] " << timestamp() << " - " << message << '\n';
        logFile.flush();
    }
#else
    (void)message;  // Suppress unused parameter warning
#endif
}

void Logger::shutdown() {
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFile.is_open()) {
        logFile << "=== ntrak shutdown " << timestamp() << " ===\n\n";
        logFile.close();
    }
#endif
}

}  // namespace ntrak::common
