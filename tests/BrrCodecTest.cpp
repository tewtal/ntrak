#include "ntrak/nspc/BrrCodec.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace ntrak::nspc {
namespace {

std::vector<int16_t> buildTestWave() {
    std::vector<int16_t> pcm;
    pcm.reserve(96);
    constexpr std::array<int16_t, 12> kCycle = {
        0, 4096, 8192, 12288, 16384, 12288, 8192, 4096, 0, -4096, -8192, -4096,
    };
    for (int i = 0; i < 8; ++i) {
        pcm.insert(pcm.end(), kCycle.begin(), kCycle.end());
    }
    return pcm;
}

}  // namespace

TEST(BrrCodecTest, EncodeProducesValidBlockStream) {
    const auto pcm = buildTestWave();
    auto encoded = encodePcm16ToBrr(pcm);
    ASSERT_TRUE(encoded.has_value());
    ASSERT_FALSE(encoded->bytes.empty());
    EXPECT_EQ(encoded->bytes.size() % 9u, 0u);

    const uint8_t lastHeader = encoded->bytes[encoded->bytes.size() - 9u];
    EXPECT_NE(lastHeader & 0x01u, 0u);
}

TEST(BrrCodecTest, LoopEncodingSetsLoopFlagAndOffset) {
    const auto pcm = buildTestWave();
    BrrEncodeOptions options{};
    options.enableLoop = true;
    options.loopStartSample = 0;

    auto encoded = encodePcm16ToBrr(pcm, options);
    ASSERT_TRUE(encoded.has_value());
    ASSERT_FALSE(encoded->bytes.empty());
    EXPECT_EQ(encoded->loopOffsetBytes % 9u, 0u);

    const uint8_t lastHeader = encoded->bytes[encoded->bytes.size() - 9u];
    EXPECT_NE(lastHeader & 0x02u, 0u);
}

TEST(BrrCodecTest, CanDecodeEncodedData) {
    const auto pcm = buildTestWave();
    auto encoded = encodePcm16ToBrr(pcm);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = decodeBrrToPcm(encoded->bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_FALSE(decoded->empty());
}

}  // namespace ntrak::nspc
