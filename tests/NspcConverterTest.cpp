#include "ntrak/nspc/NspcConverter.hpp"
#include "NspcTestHelpers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

namespace ntrak::nspc {
namespace {

NspcEngineConfig baseConfig() {
    NspcEngineConfig config{};
    config.name = "Converter test";
    config.entryPoint = 0x1234;
    config.sampleHeaders = 0x0200;
    config.instrumentHeaders = 0x0300;
    config.songIndexPointers = 0x0400;
    config.instrumentEntryBytes = 6;
    return config;
}

bool rangesOverlap(uint32_t aFrom, uint32_t aTo, uint32_t bFrom, uint32_t bTo) {
    return aFrom < bTo && bFrom < aTo;
}

TEST(NspcConverterTest, PortSongNeverAllocatesSampleDataIntoEngineTablesAfterDeletingInstruments) {
    NspcEngineConfig config = baseConfig();
    NspcProject source = test_helpers::buildProjectWithTwoSongsTwoAssets(config);
    NspcProject target = test_helpers::buildProjectWithTwoSongsTwoAssets(config);

    SongPortRequest request{};
    request.sourceSongIndex = 0;
    request.targetSongIndex = -1;
    request.instrumentsToDelete = {0, 1};

    InstrumentMapping mapping{};
    mapping.sourceInstrumentId = 0;
    mapping.action = InstrumentMapping::Action::Copy;
    mapping.sampleAction = InstrumentMapping::SampleAction::CopyNew;
    request.instrumentMappings.push_back(mapping);

    const SongPortResult result = portSong(source, target, request);
    ASSERT_TRUE(result.success) << result.error;

    const uint32_t instrumentTableFrom = config.instrumentHeaders;
    const uint32_t instrumentTableTo =
        instrumentTableFrom + 64u * static_cast<uint32_t>(std::clamp<uint8_t>(config.instrumentEntryBytes, 5, 6));
    const uint32_t sampleDirectoryFrom = config.sampleHeaders;
    const uint32_t sampleDirectoryTo = sampleDirectoryFrom + 64u * 4u;
    const uint32_t songIndexFrom = config.songIndexPointers;
    const uint32_t songIndexTo = songIndexFrom + 256u * 2u;

    bool foundUserSample = false;
    for (const auto& sample : target.samples()) {
        if (sample.contentOrigin != NspcContentOrigin::UserProvided || sample.originalAddr == 0 || sample.data.empty()) {
            continue;
        }
        foundUserSample = true;
        const uint32_t sampleFrom = sample.originalAddr;
        const uint32_t sampleTo = sampleFrom + static_cast<uint32_t>(sample.data.size());
        EXPECT_FALSE(rangesOverlap(sampleFrom, sampleTo, instrumentTableFrom, instrumentTableTo));
        EXPECT_FALSE(rangesOverlap(sampleFrom, sampleTo, sampleDirectoryFrom, sampleDirectoryTo));
        EXPECT_FALSE(rangesOverlap(sampleFrom, sampleTo, songIndexFrom, songIndexTo));
    }

    EXPECT_TRUE(foundUserSample);
}

}  // namespace
}  // namespace ntrak::nspc
