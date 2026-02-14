#include "ntrak/nspc/NspcEditor.hpp"
#include "ntrak/nspc/NspcProject.hpp"
#include "NspcTestHelpers.hpp"

#include <gtest/gtest.h>

#include <array>

namespace ntrak::nspc {
namespace {

using test_helpers::buildProjectWithTwoSongsTwoAssets;
using test_helpers::writeBrrBlock;
using test_helpers::writeWord;

NspcEngineConfig baseConfig() {
    NspcEngineConfig config{};
    config.name = "Content origin test";
    config.sampleHeaders = 0x0200;
    config.instrumentHeaders = 0x0300;
    config.songIndexPointers = 0x0400;
    config.instrumentEntryBytes = 6;
    return config;
}

}  // namespace

TEST(NspcContentOriginTest, ImportedContentDefaultsToEngineProvidedWhenNoDefaultsSpecified) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());

    ASSERT_EQ(project.songs().size(), 2u);
    ASSERT_EQ(project.instruments().size(), 2u);
    ASSERT_EQ(project.samples().size(), 2u);

    EXPECT_TRUE(project.songs()[0].isEngineProvided());
    EXPECT_TRUE(project.songs()[1].isEngineProvided());
    EXPECT_EQ(project.instruments()[0].contentOrigin, NspcContentOrigin::EngineProvided);
    EXPECT_EQ(project.instruments()[1].contentOrigin, NspcContentOrigin::EngineProvided);
    EXPECT_EQ(project.samples()[0].contentOrigin, NspcContentOrigin::EngineProvided);
    EXPECT_EQ(project.samples()[1].contentOrigin, NspcContentOrigin::EngineProvided);
}

TEST(NspcContentOriginTest, EngineConfigDefaultsClassifyIdsAsEngineOrUserProvided) {
    NspcEngineConfig config = baseConfig();
    config.defaultEngineProvidedSongIds = {0};
    config.defaultEngineProvidedInstrumentIds = {0};
    config.defaultEngineProvidedSampleIds = {0};
    config.hasDefaultEngineProvidedSongs = true;
    config.hasDefaultEngineProvidedInstruments = true;
    config.hasDefaultEngineProvidedSamples = true;

    NspcProject project = buildProjectWithTwoSongsTwoAssets(std::move(config));
    ASSERT_EQ(project.songs().size(), 2u);
    ASSERT_EQ(project.instruments().size(), 2u);
    ASSERT_EQ(project.samples().size(), 2u);

    EXPECT_TRUE(project.songs()[0].isEngineProvided());
    EXPECT_TRUE(project.songs()[1].isUserProvided());
    EXPECT_EQ(project.instruments()[0].contentOrigin, NspcContentOrigin::EngineProvided);
    EXPECT_EQ(project.instruments()[1].contentOrigin, NspcContentOrigin::UserProvided);
    EXPECT_EQ(project.samples()[0].contentOrigin, NspcContentOrigin::EngineProvided);
    EXPECT_EQ(project.samples()[1].contentOrigin, NspcContentOrigin::UserProvided);
}

TEST(NspcContentOriginTest, NewAndDuplicatedSongsAreUserProvided) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());
    ASSERT_EQ(project.songs().size(), 2u);
    EXPECT_TRUE(project.songs()[0].isEngineProvided());
    EXPECT_TRUE(project.songs()[1].isEngineProvided());

    const auto added = project.addEmptySong();
    ASSERT_TRUE(added.has_value());
    EXPECT_TRUE(project.songs()[*added].isUserProvided());

    const auto duplicated = project.duplicateSong(0);
    ASSERT_TRUE(duplicated.has_value());
    EXPECT_TRUE(project.songs()[*duplicated].isUserProvided());
}

TEST(NspcContentOriginTest, SongParsingSkipsZeroHolesButStopsAtInvalidEntry) {
    NspcEngineConfig config = baseConfig();
    std::array<std::uint8_t, 0x10000> aram{};

    writeWord(aram, 0x0200, 0x0500);
    writeWord(aram, 0x0202, 0x0500);
    writeWord(aram, 0x0204, 0x0509);
    writeWord(aram, 0x0206, 0x0509);
    writeBrrBlock(aram, 0x0500, 0x01);
    writeBrrBlock(aram, 0x0509, 0x01);

    aram[0x0300] = 0x00;
    aram[0x0301] = 0x8F;
    aram[0x0302] = 0xE0;
    aram[0x0303] = 0x7F;
    aram[0x0304] = 0x01;
    aram[0x0305] = 0x00;

    aram[0x0306] = 0x01;
    aram[0x0307] = 0x8F;
    aram[0x0308] = 0xE0;
    aram[0x0309] = 0x7F;
    aram[0x030A] = 0x01;
    aram[0x030B] = 0x00;

    writeWord(aram, 0x0400, 0x0600);  // Valid song pointer.
    writeWord(aram, 0x0402, 0x0000);  // Sparse hole.
    writeWord(aram, 0x0404, 0x0610);  // Valid song pointer after hole.
    writeWord(aram, 0x0406, 0x1234);  // Invalid pointer should terminate scan.

    writeWord(aram, 0x0600, 0x0700);
    writeWord(aram, 0x0602, 0x0000);
    writeWord(aram, 0x0610, 0x0710);
    writeWord(aram, 0x0612, 0x0000);

    // Pattern table and first track for song 0.
    for (int channel = 0; channel < 8; ++channel) {
        writeWord(aram, static_cast<std::uint16_t>(0x0700 + channel * 2), (channel == 0) ? 0x0720 : 0x0000);
    }
    aram[0x0720] = 0x24;  // Duration
    aram[0x0721] = 0x80;  // Note
    aram[0x0722] = 0x00;  // End

    // Pattern table and first track for song 2.
    for (int channel = 0; channel < 8; ++channel) {
        writeWord(aram, static_cast<std::uint16_t>(0x0710 + channel * 2), (channel == 0) ? 0x0730 : 0x0000);
    }
    aram[0x0730] = 0x18;  // Duration
    aram[0x0731] = 0x81;  // Note
    aram[0x0732] = 0x00;  // End

    NspcProject project(std::move(config), std::move(aram));

    ASSERT_EQ(project.songs().size(), 2u);
    EXPECT_EQ(project.songs()[0].songId(), 0);
    EXPECT_EQ(project.songs()[1].songId(), 2);
}

TEST(NspcContentOriginTest, SongEditsArePromotedToUserProvided) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());
    ASSERT_FALSE(project.songs().empty());
    auto& song = project.songs().front();
    ASSERT_TRUE(song.isEngineProvided());

    NspcEditor editor;
    const bool changed = editor.setRowEvent(song, NspcEditorLocation{
                                                      .patternId = 0,
                                                      .channel = 0,
                                                      .row = 0,
                                                  },
                                            Note{.pitch = 24});
    ASSERT_TRUE(changed);
    EXPECT_TRUE(song.isUserProvided());
}

TEST(NspcContentOriginTest, ProjectCanManuallySetContentOrigin) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());

    ASSERT_TRUE(project.setSongContentOrigin(0, NspcContentOrigin::UserProvided));
    ASSERT_TRUE(project.setInstrumentContentOrigin(0, NspcContentOrigin::UserProvided));
    ASSERT_TRUE(project.setSampleContentOrigin(0, NspcContentOrigin::UserProvided));

    EXPECT_TRUE(project.songs()[0].isUserProvided());
    EXPECT_EQ(project.instruments()[0].contentOrigin, NspcContentOrigin::UserProvided);
    EXPECT_EQ(project.samples()[0].contentOrigin, NspcContentOrigin::UserProvided);

    EXPECT_FALSE(project.setSongContentOrigin(99, NspcContentOrigin::EngineProvided));
    EXPECT_FALSE(project.setInstrumentContentOrigin(99, NspcContentOrigin::EngineProvided));
    EXPECT_FALSE(project.setSampleContentOrigin(99, NspcContentOrigin::EngineProvided));
}

TEST(NspcContentOriginTest, SampleParsingStopsAtNextDirectoryStartBoundary) {
    NspcEngineConfig config = baseConfig();
    std::array<std::uint8_t, 0x10000> aram{};

    // Entry 00 points to 0x0500, but that stream's END block is at 0x0509.
    // Entry 01 starts at 0x0509, so entry 00 should be rejected as overlapping.
    writeWord(aram, 0x0200, 0x0500);
    writeWord(aram, 0x0202, 0x0500);
    writeWord(aram, 0x0204, 0x0509);
    writeWord(aram, 0x0206, 0x0509);

    writeBrrBlock(aram, 0x0500, 0x00);  // Not END
    writeBrrBlock(aram, 0x0509, 0x01);  // END

    NspcProject project(std::move(config), std::move(aram));

    ASSERT_EQ(project.samples().size(), 1u);
    EXPECT_EQ(project.samples()[0].id, 1);
    EXPECT_EQ(project.samples()[0].originalAddr, 0x0509);
}

TEST(NspcContentOriginTest, SampleParsingAllowsAliasedDirectoryEntriesWithSameStart) {
    NspcEngineConfig config = baseConfig();
    std::array<std::uint8_t, 0x10000> aram{};

    writeWord(aram, 0x0200, 0x0500);
    writeWord(aram, 0x0202, 0x0500);
    writeWord(aram, 0x0204, 0x0500);
    writeWord(aram, 0x0206, 0x0500);
    writeBrrBlock(aram, 0x0500, 0x01);  // END

    NspcProject project(std::move(config), std::move(aram));

    ASSERT_EQ(project.samples().size(), 2u);
    EXPECT_EQ(project.samples()[0].id, 0);
    EXPECT_EQ(project.samples()[1].id, 1);
    EXPECT_EQ(project.samples()[0].originalAddr, 0x0500);
    EXPECT_EQ(project.samples()[1].originalAddr, 0x0500);
}

TEST(NspcContentOriginTest, SampleParsingKeepsReferencedOverlappingSample) {
    NspcEngineConfig config = baseConfig();
    std::array<std::uint8_t, 0x10000> aram{};

    // Entry 00 needs to read through 0x0509 to find END.
    // Entry 01 starts at 0x0509. Keep both when entry 00 is instrument-referenced.
    writeWord(aram, 0x0200, 0x0500);
    writeWord(aram, 0x0202, 0x0500);
    writeWord(aram, 0x0204, 0x0509);
    writeWord(aram, 0x0206, 0x0509);
    writeBrrBlock(aram, 0x0500, 0x00);  // Not END
    writeBrrBlock(aram, 0x0509, 0x01);  // END

    // Instrument 00 references sample 00.
    aram[0x0300] = 0x00;
    aram[0x0301] = 0x8F;
    aram[0x0302] = 0xE0;
    aram[0x0303] = 0x7F;
    aram[0x0304] = 0x01;
    aram[0x0305] = 0x00;

    NspcProject project(std::move(config), std::move(aram));

    ASSERT_EQ(project.samples().size(), 2u);
    const auto sample0It = std::find_if(project.samples().begin(), project.samples().end(),
                                        [](const BrrSample& sample) { return sample.id == 0; });
    const auto sample1It = std::find_if(project.samples().begin(), project.samples().end(),
                                        [](const BrrSample& sample) { return sample.id == 1; });
    ASSERT_NE(sample0It, project.samples().end());
    ASSERT_NE(sample1It, project.samples().end());
}

TEST(NspcContentOriginTest, InstrumentParsingSupportsSparseTableEntries) {
    NspcEngineConfig config = baseConfig();
    std::array<std::uint8_t, 0x10000> aram{};

    // One valid sample at entry 1B.
    writeWord(aram, static_cast<uint16_t>(0x0200 + 0x1B * 4), 0x0510);
    writeWord(aram, static_cast<uint16_t>(0x0200 + 0x1B * 4 + 2), 0x0510);
    writeBrrBlock(aram, 0x0510, 0x01);

    // Instrument table has a zero hole first, then a valid instrument at 1B.
    const uint16_t instAddr = static_cast<uint16_t>(0x0300 + 0x1B * 6);
    aram[instAddr + 0] = 0x1B;
    aram[instAddr + 1] = 0x8F;
    aram[instAddr + 2] = 0xE0;
    aram[instAddr + 3] = 0x7F;
    aram[instAddr + 4] = 0x01;
    aram[instAddr + 5] = 0x00;

    NspcProject project(std::move(config), std::move(aram));

    const auto instrumentIt = std::find_if(project.instruments().begin(), project.instruments().end(),
                                           [](const NspcInstrument& inst) { return inst.id == 0x1B; });
    ASSERT_NE(instrumentIt, project.instruments().end());
    EXPECT_EQ(instrumentIt->sampleIndex, 0x1B);

    const auto sampleIt = std::find_if(project.samples().begin(), project.samples().end(),
                                       [](const BrrSample& sample) { return sample.id == 0x1B; });
    ASSERT_NE(sampleIt, project.samples().end());
}

TEST(NspcContentOriginTest, InstrumentParsingStopsAtSampleIndexTerminator) {
    NspcEngineConfig config = baseConfig();
    std::array<std::uint8_t, 0x10000> aram{};

    // Two valid sample entries.
    writeWord(aram, 0x0200, 0x0500);
    writeWord(aram, 0x0202, 0x0500);
    writeWord(aram, 0x0204, 0x0510);
    writeWord(aram, 0x0206, 0x0510);
    writeBrrBlock(aram, 0x0500, 0x01);
    writeBrrBlock(aram, 0x0510, 0x01);

    // Instrument 00 is valid.
    aram[0x0300] = 0x00;
    aram[0x0301] = 0x8F;
    aram[0x0302] = 0xE0;
    aram[0x0303] = 0x7F;
    aram[0x0304] = 0x01;
    aram[0x0305] = 0x00;

    // Instrument 01 is a terminator marker.
    aram[0x0306] = 0xFF;

    // Instrument 02 would look valid if parser did not stop at 0xFF.
    aram[0x030C] = 0x01;
    aram[0x030D] = 0x8F;
    aram[0x030E] = 0xE0;
    aram[0x030F] = 0x7F;
    aram[0x0310] = 0x01;
    aram[0x0311] = 0x00;

    NspcProject project(std::move(config), std::move(aram));

    const auto validIt = std::find_if(project.instruments().begin(), project.instruments().end(),
                                      [](const NspcInstrument& inst) { return inst.id == 0; });
    ASSERT_NE(validIt, project.instruments().end());

    const auto garbageTailIt = std::find_if(project.instruments().begin(), project.instruments().end(),
                                            [](const NspcInstrument& inst) { return inst.id == 2; });
    EXPECT_EQ(garbageTailIt, project.instruments().end());
}

TEST(NspcContentOriginTest, ReferencedSampleWithExtendedBrrRangeIsImported) {
    NspcEngineConfig config = baseConfig();
    std::array<std::uint8_t, 0x10000> aram{};

    // Sample directory entry 04 points to BRR with range nibble 0xD.
    writeWord(aram, static_cast<uint16_t>(0x0200 + 0x04 * 4), 0x0540);
    writeWord(aram, static_cast<uint16_t>(0x0200 + 0x04 * 4 + 2), 0x0540);
    aram[0x0540] = 0xD1;  // range=0xD, filter=0, END=1

    // Instrument 04 references sample 04.
    const uint16_t instAddr = static_cast<uint16_t>(0x0300 + 0x04 * 6);
    aram[instAddr + 0] = 0x04;
    aram[instAddr + 1] = 0x8F;
    aram[instAddr + 2] = 0xE0;
    aram[instAddr + 3] = 0x7F;
    aram[instAddr + 4] = 0x01;
    aram[instAddr + 5] = 0x00;

    NspcProject project(std::move(config), std::move(aram));

    const auto sampleIt =
        std::find_if(project.samples().begin(), project.samples().end(), [](const BrrSample& sample) { return sample.id == 0x04; });
    ASSERT_NE(sampleIt, project.samples().end());

    const auto instrumentIt = std::find_if(project.instruments().begin(), project.instruments().end(),
                                           [](const NspcInstrument& inst) { return inst.id == 0x04; });
    ASSERT_NE(instrumentIt, project.instruments().end());
    EXPECT_EQ(instrumentIt->sampleIndex, 0x04);
}

}  // namespace ntrak::nspc
