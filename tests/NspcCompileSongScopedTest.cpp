#include "ntrak/nspc/NspcCompile.hpp"
#include "ntrak/nspc/NspcProject.hpp"

#include "NspcTestHelpers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace ntrak::nspc {
namespace {

using test_helpers::buildProjectWithTwoSongsTwoAssets;

NspcEngineConfig baseConfig() {
    NspcEngineConfig config{};
    config.name = "Song scoped compile test";
    config.entryPoint = 0x1234;
    config.sampleHeaders = 0x0200;
    config.instrumentHeaders = 0x0300;
    config.songIndexPointers = 0x0400;
    config.instrumentEntryBytes = 6;
    return config;
}

TEST(NspcCompileSongScopedTest, BuildSongScopedUploadHonorsPreferredAddressesAndWritesIndexPointer) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());

    ASSERT_FALSE(project.songs().empty());
    const int songId = project.songs()[0].songId();
    ASSERT_FALSE(project.songs()[0].patterns().empty());

    NspcSongAddressLayout preferred{};
    preferred.sequenceAddr = 0x6200;
    preferred.patternAddrById[project.songs()[0].patterns()[0].id] = 0x6210;
    project.setSongAddressLayout(songId, preferred);

    auto compileResult = buildSongScopedUpload(project, 0);
    ASSERT_TRUE(compileResult.has_value()) << compileResult.error();

    const NspcSongAddressLayout* layout = project.songAddressLayout(songId);
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(layout->sequenceAddr, 0x6200);
    const auto patternId = project.songs()[0].patterns()[0].id;
    const auto patternIt = layout->patternAddrById.find(patternId);
    ASSERT_NE(patternIt, layout->patternAddrById.end());
    EXPECT_EQ(patternIt->second, 0x6210);

    const auto it = std::find_if(compileResult->upload.chunks.begin(), compileResult->upload.chunks.end(),
                                 [](const NspcUploadChunk& chunk) { return chunk.label == "Song 00 IndexPtr"; });
    ASSERT_NE(it, compileResult->upload.chunks.end());
    ASSERT_EQ(it->bytes.size(), 2u);

    const uint16_t encodedSequenceAddr = static_cast<uint16_t>(it->bytes[0] | (static_cast<uint16_t>(it->bytes[1]) << 8u));
    EXPECT_EQ(encodedSequenceAddr, layout->sequenceAddr);
}

TEST(NspcCompileSongScopedTest, BuildSongScopedUploadCanCompactAndIgnorePreferredAddresses) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());

    ASSERT_FALSE(project.songs().empty());
    const int songId = project.songs()[0].songId();
    ASSERT_FALSE(project.songs()[0].patterns().empty());

    NspcSongAddressLayout preferred{};
    preferred.sequenceAddr = 0x6200;
    preferred.patternAddrById[project.songs()[0].patterns()[0].id] = 0x6210;
    project.setSongAddressLayout(songId, preferred);

    NspcBuildOptions options{};
    options.compactAramLayout = true;
    auto compileResult = buildSongScopedUpload(project, 0, options);
    ASSERT_TRUE(compileResult.has_value()) << compileResult.error();

    const NspcSongAddressLayout* layout = project.songAddressLayout(songId);
    ASSERT_NE(layout, nullptr);
    EXPECT_NE(layout->sequenceAddr, 0x6200);
    const auto patternId = project.songs()[0].patterns()[0].id;
    const auto patternIt = layout->patternAddrById.find(patternId);
    ASSERT_NE(patternIt, layout->patternAddrById.end());
    EXPECT_NE(patternIt->second, 0x6210);

    const auto indexChunkIt = std::find_if(compileResult->upload.chunks.begin(), compileResult->upload.chunks.end(),
                                           [](const NspcUploadChunk& chunk) { return chunk.label == "Song 00 IndexPtr"; });
    ASSERT_NE(indexChunkIt, compileResult->upload.chunks.end());
    ASSERT_EQ(indexChunkIt->bytes.size(), 2u);
    const uint16_t encodedSequenceAddr =
        static_cast<uint16_t>(indexChunkIt->bytes[0] | (static_cast<uint16_t>(indexChunkIt->bytes[1]) << 8u));
    EXPECT_EQ(encodedSequenceAddr, layout->sequenceAddr);
}

TEST(NspcCompileSongScopedTest, BuildSongScopedUploadFailsWhenSequenceExceedsAram) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());

    ASSERT_FALSE(project.songs().empty());
    auto& sequence = project.songs()[0].sequence();
    sequence.clear();

    // 20k jump ops * 4 bytes = 80k bytes (> 64k ARAM)
    for (int i = 0; i < 20000; ++i) {
        sequence.push_back(JumpTimes{.count = 1, .target = SequenceTarget{.index = 0, .addr = 0}});
    }

    auto compileResult = buildSongScopedUpload(project, 0);
    ASSERT_FALSE(compileResult.has_value());
    EXPECT_NE(compileResult.error().find("exceeds ARAM addressable range"), std::string::npos);
}

TEST(NspcCompileSongScopedTest, BuildSongScopedUploadEncodesExtensionVcmdAsRawEngineOpcode) {
    NspcEngineConfig config = baseConfig();
    config.extensions.push_back(NspcEngineExtension{
        .name = "Legato Mode",
        .description = "Test extension",
        .enabledByDefault = true,
        .enabled = true,
        .patches =
            {
                NspcEnginePatchWrite{
                    .name = "Patch A",
                    .address = 0x56E2,
                    .bytes = {0xE8, 0xFF, 0xD5},
                },
            },
        .vcmds =
            {
                NspcEngineExtensionVcmd{
                    .id = 0xFB,
                    .name = "Legato",
                    .description = "state",
                    .paramCount = 1,
                },
            },
    });

    NspcProject project = buildProjectWithTwoSongsTwoAssets(std::move(config));
    ASSERT_FALSE(project.songs().empty());
    auto& song = project.songs()[0];

    song.sequence().clear();
    song.sequence().push_back(PlayPattern{
        .patternId = 0,
        .trackTableAddr = 0,
    });
    song.sequence().push_back(EndSequence{});

    song.patterns().clear();
    song.patterns().push_back(NspcPattern{
        .id = 0,
        .channelTrackIds = std::array<int, 8>{0, -1, -1, -1, -1, -1, -1, -1},
        .trackTableAddr = 0,
    });

    song.subroutines().clear();
    song.tracks().clear();
    NspcTrack track{};
    track.id = 0;
    track.originalAddr = 0;
    track.events.push_back(NspcEventEntry{
        .id = 1,
        .event = NspcEvent{Duration{
            .ticks = 1,
            .quantization = std::nullopt,
            .velocity = std::nullopt,
        }},
        .originalAddr = std::nullopt,
    });
    track.events.push_back(NspcEventEntry{
        .id = 2,
        .event = NspcEvent{Vcmd{VcmdExtension{
            .id = 0xFB,
            .params = {0x01, 0x00, 0x00, 0x00},
            .paramCount = 1,
        }}},
        .originalAddr = std::nullopt,
    });
    track.events.push_back(NspcEventEntry{
        .id = 3,
        .event = NspcEvent{Note{.pitch = 0}},
        .originalAddr = std::nullopt,
    });
    track.events.push_back(NspcEventEntry{
        .id = 4,
        .event = NspcEvent{End{}},
        .originalAddr = std::nullopt,
    });
    song.tracks().push_back(std::move(track));

    auto compileResult = buildSongScopedUpload(project, 0);
    ASSERT_TRUE(compileResult.has_value()) << compileResult.error();

    const auto trackIt = std::find_if(compileResult->upload.chunks.begin(), compileResult->upload.chunks.end(),
                                      [](const NspcUploadChunk& chunk) { return chunk.label == "Track 00"; });
    ASSERT_NE(trackIt, compileResult->upload.chunks.end());
    const std::vector<uint8_t> expectedTrackBytes = {0x01, 0xFB, 0x01, 0x80, 0x00};
    EXPECT_EQ(trackIt->bytes, expectedTrackBytes);

    const auto patchIt = std::find_if(
        compileResult->upload.chunks.begin(), compileResult->upload.chunks.end(),
        [](const NspcUploadChunk& chunk) { return chunk.label.find("Ext Legato Mode Patch A") != std::string::npos; });
    EXPECT_NE(patchIt, compileResult->upload.chunks.end());
}

TEST(NspcCompileSongScopedTest, ParseTrackTreatsExtensionIdAsOverrideWhenEnabled) {
    NspcEngineConfig config = baseConfig();
    config.songIndexPointers = 0x0200;
    config.extensions.push_back(NspcEngineExtension{
        .name = "Legato Mode",
        .description = "Test extension",
        .enabledByDefault = true,
        .enabled = true,
        .patches =
            {
                NspcEnginePatchWrite{
                    .name = "Patch A",
                    .address = 0x56E2,
                    .bytes = {0xE8, 0xFF, 0xD5},
                },
            },
        .vcmds =
            {
                NspcEngineExtensionVcmd{
                    .id = 0xFB,
                    .name = "Legato",
                    .description = "state",
                    .paramCount = 1,
                },
            },
    });

    std::array<std::uint8_t, 0x10000> aram{};
    test_helpers::writeWord(aram, 0x0200, 0x0300);
    test_helpers::writeWord(aram, 0x0300, 0x0400);
    test_helpers::writeWord(aram, 0x0302, 0x0000);
    test_helpers::writeWord(aram, 0x0400, 0x0500);
    for (int ch = 1; ch < 8; ++ch) {
        test_helpers::writeWord(aram, static_cast<uint16_t>(0x0400 + ch * 2), 0x0000);
    }
    const std::array<std::uint8_t, 5> trackBytes = {
        0x08, 0xFB, 0x01, 0x80, 0x00,
    };
    std::copy(trackBytes.begin(), trackBytes.end(), aram.begin() + 0x0500);

    NspcProject project(std::move(config), std::move(aram));
    ASSERT_EQ(project.songs().size(), 1u);
    ASSERT_EQ(project.songs().front().tracks().size(), 1u);

    const auto& events = project.songs().front().tracks().front().events;
    ASSERT_EQ(events.size(), 4u);
    ASSERT_TRUE(std::holds_alternative<Duration>(events[0].event));
    ASSERT_TRUE(std::holds_alternative<Vcmd>(events[1].event));
    ASSERT_TRUE(std::holds_alternative<Note>(events[2].event));
    ASSERT_TRUE(std::holds_alternative<End>(events[3].event));

    const auto& vcmd = std::get<Vcmd>(events[1].event);
    ASSERT_TRUE(std::holds_alternative<VcmdExtension>(vcmd.vcmd));
    const auto& extension = std::get<VcmdExtension>(vcmd.vcmd);
    EXPECT_EQ(extension.id, 0xFB);
    EXPECT_EQ(extension.paramCount, 1);
    EXPECT_EQ(extension.params[0], 0x01);
}

}  // namespace
}  // namespace ntrak::nspc
