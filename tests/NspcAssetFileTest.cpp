#include "ntrak/nspc/NspcAssetFile.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <string_view>
#include <vector>

namespace ntrak::nspc {
namespace {
using json = nlohmann::json;

std::filesystem::path uniqueTempPath(std::string_view stem, std::string_view ext) {
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    return tempDir / std::format("{}-{}.{}", stem, tick, ext);
}

std::vector<uint8_t> makeValidBrr(bool loopEnabled = false) {
    std::vector<uint8_t> bytes(18, 0);
    bytes[0] = 0x00;
    bytes[9] = loopEnabled ? 0x03 : 0x01;
    return bytes;
}

}  // namespace

TEST(NspcAssetFileTest, SaveAndLoadBrrRoundTrip) {
    const auto path = uniqueTempPath("asset-brr-roundtrip", "brr");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };
    cleanup();

    const auto original = makeValidBrr();
    auto saveResult = saveBrrFile(path, original);
    ASSERT_TRUE(saveResult.has_value()) << saveResult.error();

    auto loadResult = loadBrrFile(path);
    cleanup();
    ASSERT_TRUE(loadResult.has_value()) << loadResult.error();
    EXPECT_EQ(*loadResult, original);
}

TEST(NspcAssetFileTest, LoadBrrRejectsMissingEndFlag) {
    const auto path = uniqueTempPath("asset-brr-invalid", "brr");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };
    cleanup();

    std::vector<uint8_t> invalid(9, 0);
    invalid[0] = 0x00;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.good());
    out.write(reinterpret_cast<const char*>(invalid.data()), static_cast<std::streamsize>(invalid.size()));
    out.close();

    auto loadResult = loadBrrFile(path);
    cleanup();
    ASSERT_FALSE(loadResult.has_value());
}

TEST(NspcAssetFileTest, SaveAndLoadNtiRoundTrip) {
    const auto path = uniqueTempPath("asset-nti-roundtrip", "nti");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };
    cleanup();

    NspcInstrument instrument{};
    instrument.id = 4;
    instrument.name = "Lead";
    instrument.sampleIndex = 7;
    instrument.adsr1 = 0x8F;
    instrument.adsr2 = 0xE0;
    instrument.gain = 0x7F;
    instrument.basePitchMult = 0x01;
    instrument.fracPitchMult = 0x23;
    instrument.contentOrigin = NspcContentOrigin::UserProvided;

    BrrSample sample{};
    sample.id = 7;
    sample.name = "LeadSample";
    sample.data = makeValidBrr(true);
    sample.originalAddr = 0x5000;
    sample.originalLoopAddr = 0x5009;
    sample.contentOrigin = NspcContentOrigin::UserProvided;

    auto saveResult = saveNtiFile(path, instrument, sample);
    ASSERT_TRUE(saveResult.has_value()) << saveResult.error();

    auto loadResult = loadNtiFile(path);
    cleanup();
    ASSERT_TRUE(loadResult.has_value()) << loadResult.error();

    EXPECT_EQ(loadResult->instrument.id, instrument.id);
    EXPECT_EQ(loadResult->instrument.name, instrument.name);
    EXPECT_EQ(loadResult->instrument.sampleIndex, instrument.sampleIndex);
    EXPECT_EQ(loadResult->instrument.adsr1, instrument.adsr1);
    EXPECT_EQ(loadResult->instrument.adsr2, instrument.adsr2);
    EXPECT_EQ(loadResult->instrument.gain, instrument.gain);
    EXPECT_EQ(loadResult->instrument.basePitchMult, instrument.basePitchMult);
    EXPECT_EQ(loadResult->instrument.fracPitchMult, instrument.fracPitchMult);

    EXPECT_EQ(loadResult->sample.id, sample.id);
    EXPECT_EQ(loadResult->sample.name, sample.name);
    EXPECT_EQ(loadResult->sample.data, sample.data);
    EXPECT_TRUE(loadResult->loopEnabled);
    EXPECT_EQ(loadResult->loopOffsetBytes, 9u);
}

TEST(NspcAssetFileTest, LoadNtiRejectsInvalidSamplePayload) {
    const auto path = uniqueTempPath("asset-nti-invalid", "nti");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };
    cleanup();

    const json root{
        {"format", "ntrak_instrument"},
        {"formatVersion", 1},
        {"instrument",
         {
             {"id", 1},
             {"name", "Bad"},
             {"sampleIndex", 1},
             {"adsr1", 0x8F},
             {"adsr2", 0xE0},
             {"gain", 0x7F},
             {"basePitchMult", 0x01},
             {"fracPitchMult", 0x00},
         }},
        {"sample",
         {
             {"id", 1},
             {"name", "BadSample"},
             {"loopEnabled", false},
             {"loopOffsetBytes", 0},
             {"dataEncoding", "base64"},
             {"data", "AA=="},
         }},
    };

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.good());
    out << root.dump(2);
    out.close();

    auto loadResult = loadNtiFile(path);
    cleanup();
    ASSERT_FALSE(loadResult.has_value());
}

}  // namespace ntrak::nspc
