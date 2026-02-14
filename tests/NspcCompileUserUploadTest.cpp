#include "ntrak/nspc/NspcCompile.hpp"
#include "ntrak/nspc/NspcProject.hpp"

#include "NspcTestHelpers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace ntrak::nspc {
namespace {

using test_helpers::buildProjectWithTwoSongsTwoAssets;

NspcEngineConfig baseConfig() {
    NspcEngineConfig config{};
    config.name = "User upload compile test";
    config.entryPoint = 0x1234;
    config.sampleHeaders = 0x0200;
    config.instrumentHeaders = 0x0300;
    config.songIndexPointers = 0x0400;
    config.instrumentEntryBytes = 6;
    return config;
}

void markAllUserProvided(NspcProject& project) {
    for (size_t i = 0; i < project.songs().size(); ++i) {
        ASSERT_TRUE(project.setSongContentOrigin(i, NspcContentOrigin::UserProvided));
    }
    for (const auto& instrument : project.instruments()) {
        ASSERT_TRUE(project.setInstrumentContentOrigin(instrument.id, NspcContentOrigin::UserProvided));
    }
    for (const auto& sample : project.samples()) {
        ASSERT_TRUE(project.setSampleContentOrigin(sample.id, NspcContentOrigin::UserProvided));
    }
}

TEST(NspcCompileUserUploadTest, BuildUserContentUploadAllowsAliasedSampleBrrData) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());
    markAllUserProvided(project);

    ASSERT_GE(project.samples().size(), 2u);
    auto& samples = project.samples();
    samples[1].originalAddr = samples[0].originalAddr;
    samples[1].originalLoopAddr = samples[0].originalLoopAddr;
    samples[1].data = samples[0].data;

    auto upload = buildUserContentUpload(project);
    ASSERT_TRUE(upload.has_value()) << upload.error();

    const int brrChunkCount = static_cast<int>(std::count_if(
        upload->chunks.begin(), upload->chunks.end(),
        [](const NspcUploadChunk& chunk) { return chunk.label.find(" BRR") != std::string::npos; }));
    EXPECT_EQ(brrChunkCount, 1);
}

TEST(NspcCompileUserUploadTest, BuildUserContentUploadRejectsOverlappingSampleBrrRanges) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());
    markAllUserProvided(project);

    ASSERT_GE(project.samples().size(), 2u);
    auto& samples = project.samples();
    samples[1].originalAddr = static_cast<uint16_t>(samples[0].originalAddr + 1u);
    if (!samples[1].data.empty()) {
        samples[1].data[0] ^= 0x10;
    }

    auto upload = buildUserContentUpload(project);
    ASSERT_FALSE(upload.has_value());
    EXPECT_NE(upload.error().find("overlaps user sample"), std::string::npos);
}

TEST(NspcCompileUserUploadTest, BuildUserContentUploadRejectsInstrumentTableOutOfBounds) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());
    markAllUserProvided(project);

    ASSERT_FALSE(project.instruments().empty());
    project.instruments()[0].id = 20000;

    auto upload = buildUserContentUpload(project);
    ASSERT_FALSE(upload.has_value());
    EXPECT_NE(upload.error().find("table write"), std::string::npos);
}

TEST(NspcCompileUserUploadTest, BuildUserContentUploadIncludesEnabledEngineExtensionPatches) {
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

    auto upload = buildUserContentUpload(project);
    ASSERT_TRUE(upload.has_value()) << upload.error();

    const auto extChunkCount = std::count_if(upload->chunks.begin(), upload->chunks.end(), [](const NspcUploadChunk& chunk) {
        return chunk.label.find("Ext Legato Mode Patch A") != std::string::npos;
    });
    EXPECT_EQ(extChunkCount, 1);
}

}  // namespace
}  // namespace ntrak::nspc
