#include "ntrak/nspc/NspcEngine.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace ntrak::nspc {
namespace {

#ifdef _WIN32
void setEnvVar(const char* key, const char* value) {
    _putenv_s(key, value);
}
void unsetEnvVar(const char* key) {
    _putenv_s(key, "");
}
#else
void setEnvVar(const char* key, const char* value) {
    setenv(key, value, 1);
}
void unsetEnvVar(const char* key) {
    unsetenv(key);
}
#endif

class ScopedEnvVar {
public:
    ScopedEnvVar(std::string key, std::string value) : key_(std::move(key)) {
        if (const char* current = std::getenv(key_.c_str()); current != nullptr) {
            hadOldValue_ = true;
            oldValue_ = current;
        }
        setEnvVar(key_.c_str(), value.c_str());
    }

    ~ScopedEnvVar() {
        if (hadOldValue_) {
            setEnvVar(key_.c_str(), oldValue_.c_str());
        } else {
            unsetEnvVar(key_.c_str());
        }
    }

private:
    std::string key_;
    std::string oldValue_;
    bool hadOldValue_ = false;
};

TEST(NspcEngineConfigLoadFailureTest, MalformedJsonReturnsNullopt) {
    const auto base = std::filesystem::temp_directory_path() / std::filesystem::path("ntrak-enginecfg-badjson");
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base / "ntrak", ec);
    ASSERT_FALSE(ec);

    {
        std::ofstream out(base / "ntrak" / "engine_overrides.json", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        out << "{ not valid json";
    }

    ScopedEnvVar env("XDG_CONFIG_HOME", base.string());
    const auto loaded = loadEngineConfigs();
    EXPECT_FALSE(loaded.has_value());

    std::filesystem::remove_all(base, ec);
}

TEST(NspcEngineConfigLoadFailureTest, NonArrayRootReturnsNullopt) {
    const auto base = std::filesystem::temp_directory_path() / std::filesystem::path("ntrak-enginecfg-badroot");
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base / "ntrak", ec);
    ASSERT_FALSE(ec);

    {
        std::ofstream out(base / "ntrak" / "engine_overrides.json", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        out << "{\"name\":\"not-an-array\"}";
    }

    ScopedEnvVar env("XDG_CONFIG_HOME", base.string());
    const auto loaded = loadEngineConfigs();
    EXPECT_FALSE(loaded.has_value());

    std::filesystem::remove_all(base, ec);
}

}  // namespace
}  // namespace ntrak::nspc
