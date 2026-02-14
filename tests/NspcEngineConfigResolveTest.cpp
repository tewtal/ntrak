#include "ntrak/nspc/NspcEngine.hpp"
#include "NspcTestHelpers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>

namespace ntrak::nspc {
namespace {

using test_helpers::writeWord;

class ScopedEnvVar {
public:
    ScopedEnvVar(std::string name, std::string value) : name_(std::move(name)) {
        if (const char* existing = std::getenv(name_.c_str()); existing != nullptr) {
            originalValue_ = std::string(existing);
        }
#ifdef _WIN32
        _putenv_s(name_.c_str(), value.c_str());
#else
        setenv(name_.c_str(), value.c_str(), 1);
#endif
    }

    ~ScopedEnvVar() {
#ifdef _WIN32
        if (originalValue_.has_value()) {
            _putenv_s(name_.c_str(), originalValue_->c_str());
        } else {
            _putenv_s(name_.c_str(), "");
        }
#else
        if (originalValue_.has_value()) {
            setenv(name_.c_str(), originalValue_->c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
#endif
    }

private:
    std::string name_;
    std::optional<std::string> originalValue_;
};

TEST(NspcEngineConfigResolveTest, ParsesDefaultDspTablePointerFromConfigJson) {
    auto configs = loadEngineConfigs();
    ASSERT_TRUE(configs.has_value());

    const auto it = std::find_if(configs->begin(), configs->end(), [](const NspcEngineConfig& config) {
        return config.name == "Super Mario World (AddmusicK)";
    });
    ASSERT_NE(it, configs->end());
    ASSERT_TRUE(it->defaultDspTablePtr.has_value());
    EXPECT_EQ(*it->defaultDspTablePtr, 0x041C);
}

TEST(NspcEngineConfigResolveTest, ParsesEchoBufferPointerFromConfigJson) {
    auto configs = loadEngineConfigs();
    ASSERT_TRUE(configs.has_value());

    const auto it = std::find_if(configs->begin(), configs->end(),
                                 [](const NspcEngineConfig& config) { return config.name == "A Link to the Past"; });
    ASSERT_NE(it, configs->end());
    ASSERT_TRUE(it->echoBufferPtr.has_value());
    EXPECT_EQ(*it->echoBufferPtr, 0x0E62);
}

TEST(NspcEngineConfigResolveTest, ParsesSongTriggerOffsetFromConfigJson) {
    auto configs = loadEngineConfigs();
    ASSERT_TRUE(configs.has_value());

    const auto tmntIt = std::find_if(configs->begin(), configs->end(), [](const NspcEngineConfig& config) {
        return config.name.find("TMNT IV: Turtles in Time") != std::string::npos;
    });
    ASSERT_NE(tmntIt, configs->end());
    EXPECT_EQ(tmntIt->songTriggerOffset, 0x80);

    const auto smwIt = std::find_if(configs->begin(), configs->end(), [](const NspcEngineConfig& config) {
        return config.name == "Super Mario World";
    });
    ASSERT_NE(smwIt, configs->end());
    EXPECT_EQ(smwIt->songTriggerOffset, 0x01);
}

TEST(NspcEngineConfigResolveTest, ParsesEngineExtensionsAndVirtualVcmdMetadata) {
#ifdef _WIN32
    GTEST_SKIP() << "Uses XDG_CONFIG_HOME override to isolate config input.";
#else
    const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / ("ntrak-engine-config-" + nonce);
    const std::filesystem::path configDir = tempRoot / "ntrak";
    std::filesystem::create_directories(configDir);
    ASSERT_TRUE(std::filesystem::is_directory(configDir));

    const std::filesystem::path configPath = configDir / "engine_overrides.json";
    {
        std::ofstream out(configPath);
        ASSERT_TRUE(out.good());
        out << R"([
  {
    "name": "Extension Parse Test Engine",
    "entryPoint": "0x0400",
    "extensionVcmdPrefix": "0xFF",
    "extensions": [
      {
        "name": "Legato Mode",
        "code": { "address": "0x1234", "bytes": "AABBCCDD" },
        "hooks": [
          { "name": "Hook", "address": "0x2000", "bytes": "01" }
        ],
        "vcmds": [
          { "id": "0xFB", "name": "Legato", "parameters": [ { "name": "State" } ] }
        ]
      }
    ]
  }
])";
        ASSERT_TRUE(out.good());
    }

    const ScopedEnvVar scopedXdgConfigHome("XDG_CONFIG_HOME", tempRoot.string());

    auto configs = loadEngineConfigs();

    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);

    ASSERT_TRUE(configs.has_value());

    const auto it = std::find_if(configs->begin(), configs->end(), [](const NspcEngineConfig& config) {
        return config.name == "Extension Parse Test Engine";
    });
    ASSERT_NE(it, configs->end());

    EXPECT_EQ(it->extensionVcmdPrefix, 0xFF);
    ASSERT_FALSE(it->extensions.empty());

    const auto* extension = findEngineExtension(*it, "Legato Mode");
    ASSERT_NE(extension, nullptr);
    EXPECT_TRUE(extension->enabled);
    EXPECT_FALSE(extension->patches.empty());
    ASSERT_FALSE(extension->vcmds.empty());
    EXPECT_EQ(extension->vcmds.front().id, 0xFB);
    EXPECT_EQ(extension->vcmds.front().paramCount, 1);
#endif
}

TEST(NspcEngineConfigResolveTest, ParsesPlaybackHookCountFromConfigJson) {
#ifdef _WIN32
    GTEST_SKIP() << "Uses XDG_CONFIG_HOME override to isolate config input.";
#else
    const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / ("ntrak-engine-config-" + nonce);
    const std::filesystem::path configDir = tempRoot / "ntrak";
    std::filesystem::create_directories(configDir);
    ASSERT_TRUE(std::filesystem::is_directory(configDir));

    const std::filesystem::path configPath = configDir / "engine_overrides.json";
    {
        std::ofstream out(configPath);
        ASSERT_TRUE(out.good());
        out << R"([
  {
    "name": "Count Parse Test Engine",
    "entryPoint": "0x0400",
    "playbackHooks": {
      "tickTrigger": { "op": "execute", "address": "0x1234", "count": 8 },
      "patternTrigger": { "op": "execute", "address": "0x5678" }
    }
  }
])";
        ASSERT_TRUE(out.good());
    }

    const ScopedEnvVar scopedXdgConfigHome("XDG_CONFIG_HOME", tempRoot.string());
    auto configs = loadEngineConfigs();

    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);

    ASSERT_TRUE(configs.has_value());
    const auto it = std::find_if(configs->begin(), configs->end(), [](const NspcEngineConfig& config) {
        return config.name == "Count Parse Test Engine";
    });
    ASSERT_NE(it, configs->end());
    const NspcEngineConfig& engine = *it;
    ASSERT_TRUE(engine.playbackHooks.has_value());
    ASSERT_TRUE(engine.playbackHooks->tickTrigger.has_value());
    EXPECT_EQ(engine.playbackHooks->tickTrigger->count, 8u);
    ASSERT_TRUE(engine.playbackHooks->patternTrigger.has_value());
    EXPECT_EQ(engine.playbackHooks->patternTrigger->count, 1u);
#endif
}

TEST(NspcEngineConfigResolveTest, AppliesNestedOverridesToBundledEngineConfig) {
#ifdef _WIN32
    GTEST_SKIP() << "Uses XDG_CONFIG_HOME override to isolate config input.";
#else
    const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / ("ntrak-engine-config-" + nonce);
    const std::filesystem::path configDir = tempRoot / "ntrak";
    std::filesystem::create_directories(configDir);
    ASSERT_TRUE(std::filesystem::is_directory(configDir));

    const std::filesystem::path configPath = configDir / "engine_overrides.json";
    {
        std::ofstream out(configPath);
        ASSERT_TRUE(out.good());
        out << R"([
  {
    "name": "A Link to the Past",
    "playbackHooks": {
      "tickTrigger": { "count": 5 }
    },
    "extensions": [
      {
        "name": "Override Test Extension",
        "patches": [
          { "name": "Patch", "address": "0x3FE0", "bytes": "AA" }
        ],
        "vcmds": [
          { "id": "0xFE", "name": "Override VCMD", "paramCount": 1 }
        ]
      }
    ]
  }
])";
        ASSERT_TRUE(out.good());
    }

    const ScopedEnvVar scopedXdgConfigHome("XDG_CONFIG_HOME", tempRoot.string());
    auto configs = loadEngineConfigs();

    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);

    ASSERT_TRUE(configs.has_value());
    const auto it = std::find_if(configs->begin(), configs->end(),
                                 [](const NspcEngineConfig& config) { return config.name == "A Link to the Past"; });
    ASSERT_NE(it, configs->end());

    ASSERT_TRUE(it->playbackHooks.has_value());
    ASSERT_TRUE(it->playbackHooks->tickTrigger.has_value());
    EXPECT_EQ(it->playbackHooks->tickTrigger->address, 0x08C2);
    EXPECT_EQ(it->playbackHooks->tickTrigger->count, 5u);

    const auto* extension = findEngineExtension(*it, "Override Test Extension");
    ASSERT_NE(extension, nullptr);
    ASSERT_EQ(extension->patches.size(), 1u);
    ASSERT_EQ(extension->vcmds.size(), 1u);
#endif
}

TEST(NspcEngineConfigResolveTest, AppliesOverrideByEngineIdBeforeName) {
#ifdef _WIN32
    GTEST_SKIP() << "Uses XDG_CONFIG_HOME override to isolate config input.";
#else
    auto baselineConfigs = loadEngineConfigs();
    ASSERT_TRUE(baselineConfigs.has_value());
    const size_t baselineCount = baselineConfigs->size();

    const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / ("ntrak-engine-config-" + nonce);
    const std::filesystem::path configDir = tempRoot / "ntrak";
    std::filesystem::create_directories(configDir);
    ASSERT_TRUE(std::filesystem::is_directory(configDir));

    const std::filesystem::path configPath = configDir / "engine_overrides.json";
    {
        std::ofstream out(configPath);
        ASSERT_TRUE(out.good());
        out << R"json([
  {
    "id": "zelda_alttp",
    "name": "A Link to the Past (Custom Name)",
    "playbackHooks": {
      "tickTrigger": { "count": 9 }
    }
  }
])json";
        ASSERT_TRUE(out.good());
    }

    const ScopedEnvVar scopedXdgConfigHome("XDG_CONFIG_HOME", tempRoot.string());
    auto configs = loadEngineConfigs();

    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);

    ASSERT_TRUE(configs.has_value());
    EXPECT_EQ(configs->size(), baselineCount);

    const auto it = std::find_if(configs->begin(), configs->end(),
                                 [](const NspcEngineConfig& config) { return config.id == "zelda_alttp"; });
    ASSERT_NE(it, configs->end());
    EXPECT_EQ(it->name, "A Link to the Past (Custom Name)");
    ASSERT_TRUE(it->playbackHooks.has_value());
    ASSERT_TRUE(it->playbackHooks->tickTrigger.has_value());
    EXPECT_EQ(it->playbackHooks->tickTrigger->count, 9u);
#endif
}

TEST(NspcEngineConfigResolveTest, ResolvesSampleDirectoryAndEchoFromDefaultDspTable) {
    NspcEngineConfig config{};
    config.defaultDspTablePtr = 0x0100;
    config.echoBufferLen = 0x2000;

    std::array<std::uint8_t, 0x10000> aram{};
    writeWord(aram, 0x0100, 0x0200);

    constexpr std::array<std::uint8_t, 12> kValues = {
        0x7F, 0x7F, 0x00, 0x00, 0x2F, 0x00, 0x00, 0x00, 0x00, 0x2F, 0x88, 0x00,
    };
    constexpr std::array<std::uint8_t, 12> kRegs = {
        0x0C, 0x1C, 0x2C, 0x3C, 0x6C, 0x0D, 0x2D, 0x3D, 0x4D, 0x5D, 0x6D, 0x7D,
    };

    std::copy(kValues.begin(), kValues.end(), aram.begin() + 0x0200);
    std::copy(kRegs.begin(), kRegs.end(), aram.begin() + 0x0200 + kValues.size());

    const NspcEngineConfig resolved =
        resolveEngineConfigPointers(config, std::span<const std::uint8_t>(aram.data(), aram.size()));
    EXPECT_EQ(resolved.sampleHeaders, 0x2F00);
    EXPECT_EQ(resolved.echoBuffer, 0x8800);
}

TEST(NspcEngineConfigResolveTest, ResolvesEchoBufferFromExplicitPointer) {
    NspcEngineConfig config{};
    config.echoBufferPtr = 0x0120;
    config.echoBufferLen = 0x1000;

    std::array<std::uint8_t, 0x10000> aram{};
    aram[0x0120] = 0x40;

    const NspcEngineConfig resolved =
        resolveEngineConfigPointers(config, std::span<const std::uint8_t>(aram.data(), aram.size()));
    EXPECT_EQ(resolved.echoBuffer, 0x4000);
}

TEST(NspcEngineConfigResolveTest, EchoBufferPointerOverridesDefaultDspTableValue) {
    NspcEngineConfig config{};
    config.defaultDspTablePtr = 0x0100;
    config.echoBufferPtr = 0x0010;
    config.echoBufferLen = 0x2000;

    std::array<std::uint8_t, 0x10000> aram{};
    aram[0x0010] = 0x70;
    writeWord(aram, 0x0100, 0x0200);

    constexpr std::array<std::uint8_t, 12> kValues = {
        0x7F, 0x7F, 0x00, 0x00, 0x2F, 0x00, 0x00, 0x00, 0x00, 0x2F, 0x88, 0x00,
    };
    constexpr std::array<std::uint8_t, 12> kRegs = {
        0x0C, 0x1C, 0x2C, 0x3C, 0x6C, 0x0D, 0x2D, 0x3D, 0x4D, 0x5D, 0x6D, 0x7D,
    };
    std::copy(kValues.begin(), kValues.end(), aram.begin() + 0x0200);
    std::copy(kRegs.begin(), kRegs.end(), aram.begin() + 0x0200 + kValues.size());

    const NspcEngineConfig resolved =
        resolveEngineConfigPointers(config, std::span<const std::uint8_t>(aram.data(), aram.size()));
    EXPECT_EQ(resolved.echoBuffer, 0x7000);
}

TEST(NspcEngineConfigResolveTest, DefaultDspTableSampleDirectoryOverridesStaticPointer) {
    NspcEngineConfig config{};
    config.sampleHeaderPtr = 0x0010;
    config.defaultDspTablePtr = 0x0100;

    std::array<std::uint8_t, 0x10000> aram{};
    aram[0x0010] = 0x12;
    writeWord(aram, 0x0100, 0x0200);

    constexpr std::array<std::uint8_t, 12> kValues = {
        0x7F, 0x7F, 0x00, 0x00, 0x2F, 0x00, 0x00, 0x00, 0x00, 0x2F, 0x88, 0x00,
    };
    constexpr std::array<std::uint8_t, 12> kRegs = {
        0x0C, 0x1C, 0x2C, 0x3C, 0x6C, 0x0D, 0x2D, 0x3D, 0x4D, 0x5D, 0x6D, 0x7D,
    };
    std::copy(kValues.begin(), kValues.end(), aram.begin() + 0x0200);
    std::copy(kRegs.begin(), kRegs.end(), aram.begin() + 0x0200 + kValues.size());

    const NspcEngineConfig resolved =
        resolveEngineConfigPointers(config, std::span<const std::uint8_t>(aram.data(), aram.size()));
    EXPECT_EQ(resolved.sampleHeaders, 0x2F00);
}

}  // namespace
}  // namespace ntrak::nspc
