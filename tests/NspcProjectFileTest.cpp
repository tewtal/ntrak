#include "ntrak/nspc/NspcCompile.hpp"
#include "ntrak/nspc/NspcProject.hpp"
#include "ntrak/nspc/NspcProjectFile.hpp"
#include "NspcTestHelpers.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace ntrak::nspc {
namespace {
using json = nlohmann::json;

using test_helpers::buildProjectWithTwoSongsTwoAssets;
using test_helpers::writeBrrBlock;
using test_helpers::writeWord;

NspcEngineConfig baseConfig() {
    NspcEngineConfig config{};
    config.name = "Project file test";
    config.entryPoint = 0x1234;
    config.sampleHeaders = 0x0200;
    config.instrumentHeaders = 0x0300;
    config.songIndexPointers = 0x0400;
    config.instrumentEntryBytes = 6;
    return config;
}

std::filesystem::path uniqueTempPath(std::string_view stem, std::string_view ext) {
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    return tempDir / std::format("{}-{}.{}", stem, tick, ext);
}

struct NspcPacket {
    uint16_t length = 0;
    uint16_t address = 0;
    std::vector<uint8_t> data;
};

std::optional<std::pair<std::vector<NspcPacket>, uint16_t>> decodeNspcExport(std::span<const uint8_t> bytes) {
    if (bytes.size() < 4) {
        return std::nullopt;
    }

    auto readU16 = [&](size_t offset) -> std::optional<uint16_t> {
        if (offset + 1u >= bytes.size()) {
            return std::nullopt;
        }
        return static_cast<uint16_t>(bytes[offset] | (static_cast<uint16_t>(bytes[offset + 1u]) << 8u));
    };

    size_t offset = 0;
    std::vector<NspcPacket> packets;
    while (true) {
        const auto length = readU16(offset);
        if (!length.has_value()) {
            return std::nullopt;
        }
        offset += 2;

        if (*length == 0) {
            const auto entryPoint = readU16(offset);
            if (!entryPoint.has_value()) {
                return std::nullopt;
            }
            offset += 2;
            if (offset != bytes.size()) {
                return std::nullopt;
            }
            return std::pair{std::move(packets), *entryPoint};
        }

        const auto address = readU16(offset);
        if (!address.has_value()) {
            return std::nullopt;
        }
        offset += 2;
        if (offset + *length > bytes.size()) {
            return std::nullopt;
        }

        NspcPacket packet{};
        packet.length = *length;
        packet.address = *address;
        packet.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                           bytes.begin() + static_cast<std::ptrdiff_t>(offset + *length));
        packets.push_back(std::move(packet));
        offset += *length;
    }
}

}  // namespace

TEST(NspcProjectFileTest, SaveAndLoadProjectIrPersistsOnlyUserProvidedContent) {
    NspcEngineConfig config = baseConfig();
    config.defaultEngineProvidedSongIds = {0};
    config.defaultEngineProvidedInstrumentIds = {0};
    config.defaultEngineProvidedSampleIds = {0};
    config.hasDefaultEngineProvidedSongs = true;
    config.hasDefaultEngineProvidedInstruments = true;
    config.hasDefaultEngineProvidedSamples = true;
    NspcProject project = buildProjectWithTwoSongsTwoAssets(std::move(config));
    project.songs()[1].setSongName("Roundtrip Song");
    project.songs()[1].setAuthor("Roundtrip Author");

    const auto path = uniqueTempPath("project-ir", "nproj");
    const auto cleanup = [&]() { std::error_code ec; std::filesystem::remove(path, ec); };
    cleanup();

    auto saveResult = saveProjectIrFile(project, path);
    ASSERT_TRUE(saveResult.has_value()) << saveResult.error();

    auto loadResult = loadProjectIrFile(path);
    cleanup();
    ASSERT_TRUE(loadResult.has_value()) << loadResult.error();

    const auto& overlay = *loadResult;
    EXPECT_EQ(overlay.engineName, "Project file test");
    ASSERT_EQ(overlay.songs.size(), 1u);
    EXPECT_EQ(overlay.songs[0].songId(), 1);
    EXPECT_TRUE(overlay.songs[0].isUserProvided());
    EXPECT_EQ(overlay.songs[0].songName(), "Roundtrip Song");
    EXPECT_EQ(overlay.songs[0].author(), "Roundtrip Author");

    ASSERT_EQ(overlay.instruments.size(), 1u);
    EXPECT_EQ(overlay.instruments[0].id, 1);
    EXPECT_EQ(overlay.instruments[0].contentOrigin, NspcContentOrigin::UserProvided);

    ASSERT_EQ(overlay.samples.size(), 1u);
    EXPECT_EQ(overlay.samples[0].id, 1);
    EXPECT_EQ(overlay.samples[0].contentOrigin, NspcContentOrigin::UserProvided);

    EXPECT_EQ(overlay.retainedEngineSongIds, std::vector<int>({0}));
    EXPECT_EQ(overlay.retainedEngineInstrumentIds, std::vector<int>({0}));
    EXPECT_EQ(overlay.retainedEngineSampleIds, std::vector<int>({0}));
}

TEST(NspcProjectFileTest, SaveAndLoadProjectIrPersistsEngineSongMetadataOverrides) {
    NspcEngineConfig config = baseConfig();
    config.defaultEngineProvidedSongIds = {0};
    config.defaultEngineProvidedInstrumentIds = {0};
    config.defaultEngineProvidedSampleIds = {0};
    config.hasDefaultEngineProvidedSongs = true;
    config.hasDefaultEngineProvidedInstruments = true;
    config.hasDefaultEngineProvidedSamples = true;
    NspcProject project = buildProjectWithTwoSongsTwoAssets(std::move(config));
    project.songs()[0].setSongName("Engine Song Name");
    project.songs()[0].setAuthor("Engine Song Author");

    const auto path = uniqueTempPath("project-ir-engine-song-meta", "nproj");
    const auto cleanup = [&]() { std::error_code ec; std::filesystem::remove(path, ec); };
    cleanup();

    auto saveResult = saveProjectIrFile(project, path);
    ASSERT_TRUE(saveResult.has_value()) << saveResult.error();

    auto loadResult = loadProjectIrFile(path);
    cleanup();
    ASSERT_TRUE(loadResult.has_value()) << loadResult.error();

    const auto engineSongIt = std::find_if(loadResult->songs.begin(), loadResult->songs.end(),
                                           [](const NspcSong& song) { return song.songId() == 0; });
    ASSERT_NE(engineSongIt, loadResult->songs.end());
    EXPECT_TRUE(engineSongIt->isEngineProvided());
    EXPECT_EQ(engineSongIt->songName(), "Engine Song Name");
    EXPECT_EQ(engineSongIt->author(), "Engine Song Author");
}

TEST(NspcProjectFileTest, LoadProjectIrFailsWhenEngineRetainedPayloadIsMissing) {
    const auto path = uniqueTempPath("project-ir-missing-retained", "ntrakproj");
    const auto cleanup = [&]() { std::error_code ec; std::filesystem::remove(path, ec); };
    cleanup();

    json root{
        {"format", "ntrak_project_ir"},
        {"version", 4},
        {"engine", "Project file test"},
        {"songs", json::array()},
        {"instruments", json::array()},
        {"samples", json::array()},
    };

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.good());
    out << root.dump(2);
    out.close();

    auto loadResult = loadProjectIrFile(path);
    cleanup();
    ASSERT_FALSE(loadResult.has_value());
    EXPECT_NE(loadResult.error().find("engineRetained"), std::string::npos);
}

TEST(NspcProjectFileTest, ApplyProjectIrOverlayMergesSongsAndAssetsById) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());

    NspcProjectIrData overlay{};
    overlay.engineName = "Project file test";
    overlay.retainedEngineSongIds = {0, 1, 2};
    overlay.retainedEngineInstrumentIds = {0, 1};
    overlay.retainedEngineSampleIds = {0, 1};

    NspcSong song = NspcSong::createEmpty(3);
    song.setSongId(3);
    song.setContentOrigin(NspcContentOrigin::UserProvided);
    overlay.songs.push_back(std::move(song));

    NspcInstrument instrument{};
    instrument.id = 7;
    instrument.sampleIndex = 1;
    instrument.adsr1 = 0x8F;
    instrument.adsr2 = 0xE0;
    instrument.gain = 0x7F;
    instrument.basePitchMult = 0x01;
    instrument.fracPitchMult = 0x00;
    instrument.contentOrigin = NspcContentOrigin::UserProvided;
    overlay.instruments.push_back(instrument);

    BrrSample sample{};
    sample.id = 8;
    sample.data = {0x01, 0, 0, 0, 0, 0, 0, 0, 0};
    sample.originalAddr = 0x0800;
    sample.originalLoopAddr = 0x0800;
    sample.contentOrigin = NspcContentOrigin::UserProvided;
    overlay.samples.push_back(sample);

    auto applyResult = applyProjectIrOverlay(project, overlay);
    ASSERT_TRUE(applyResult.has_value()) << applyResult.error();

    ASSERT_GE(project.songs().size(), 4u);
    EXPECT_EQ(project.songs()[3].songId(), 3);
    EXPECT_TRUE(project.songs()[3].isUserProvided());

    const auto instrumentIt =
        std::find_if(project.instruments().begin(), project.instruments().end(),
                     [](const NspcInstrument& value) { return value.id == 7; });
    ASSERT_NE(instrumentIt, project.instruments().end());
    EXPECT_EQ(instrumentIt->contentOrigin, NspcContentOrigin::UserProvided);

    const auto sampleIt =
        std::find_if(project.samples().begin(), project.samples().end(),
                     [](const BrrSample& value) { return value.id == 8; });
    ASSERT_NE(sampleIt, project.samples().end());
    EXPECT_EQ(sampleIt->contentOrigin, NspcContentOrigin::UserProvided);
}

TEST(NspcProjectFileTest, ApplyProjectIrOverlayReplacesExistingInstrumentAndSampleIds) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());
    ASSERT_EQ(project.instruments().size(), 2u);
    ASSERT_EQ(project.samples().size(), 2u);

    NspcProjectIrData overlay{};
    overlay.engineName = "Project file test";
    overlay.retainedEngineSongIds = {0, 1};
    overlay.retainedEngineInstrumentIds = {1};
    overlay.retainedEngineSampleIds = {1};

    NspcInstrument instrument{};
    instrument.id = 0;
    instrument.sampleIndex = 1;
    instrument.adsr1 = 0xAA;
    instrument.adsr2 = 0xBB;
    instrument.gain = 0xCC;
    instrument.basePitchMult = 0xDD;
    instrument.fracPitchMult = 0xEE;
    instrument.originalAddr = 0x0300;
    instrument.contentOrigin = NspcContentOrigin::UserProvided;
    overlay.instruments.push_back(instrument);

    BrrSample sample{};
    sample.id = 0;
    sample.data = {0x03, 0, 0, 0, 0, 0, 0, 0, 0};
    sample.originalAddr = 0x0520;
    sample.originalLoopAddr = 0x0520;
    sample.contentOrigin = NspcContentOrigin::UserProvided;
    overlay.samples.push_back(sample);

    auto applyResult = applyProjectIrOverlay(project, overlay);
    ASSERT_TRUE(applyResult.has_value()) << applyResult.error();

    EXPECT_EQ(project.instruments().size(), 2u);
    EXPECT_EQ(project.samples().size(), 2u);

    const auto instrumentIt =
        std::find_if(project.instruments().begin(), project.instruments().end(),
                     [](const NspcInstrument& value) { return value.id == 0; });
    ASSERT_NE(instrumentIt, project.instruments().end());
    EXPECT_EQ(instrumentIt->sampleIndex, 1);
    EXPECT_EQ(instrumentIt->adsr1, 0xAA);
    EXPECT_EQ(instrumentIt->contentOrigin, NspcContentOrigin::UserProvided);

    const auto sampleIt =
        std::find_if(project.samples().begin(), project.samples().end(),
                     [](const BrrSample& value) { return value.id == 0; });
    ASSERT_NE(sampleIt, project.samples().end());
    EXPECT_EQ(sampleIt->originalAddr, 0x0520);
    EXPECT_EQ(sampleIt->contentOrigin, NspcContentOrigin::UserProvided);
    EXPECT_EQ(sampleIt->data, sample.data);

    const auto& config = project.engineConfig();
    const uint8_t instrumentEntrySize = std::clamp<uint8_t>(config.instrumentEntryBytes, 5, 6);
    const uint16_t instrumentAddr =
        static_cast<uint16_t>(static_cast<uint32_t>(config.instrumentHeaders) +
                              static_cast<uint32_t>(instrument.id) * static_cast<uint32_t>(instrumentEntrySize));
    const uint16_t sampleDirAddr =
        static_cast<uint16_t>(static_cast<uint32_t>(config.sampleHeaders) + static_cast<uint32_t>(sample.id) * 4u);

    const auto aram = project.aram();
    EXPECT_EQ(aram.read(instrumentAddr + 0u), instrument.sampleIndex);
    EXPECT_EQ(aram.read(instrumentAddr + 1u), instrument.adsr1);
    EXPECT_EQ(aram.read(instrumentAddr + 2u), instrument.adsr2);
    EXPECT_EQ(aram.read(instrumentAddr + 3u), instrument.gain);
    EXPECT_EQ(aram.read(instrumentAddr + 4u), instrument.basePitchMult);
    EXPECT_EQ(aram.read(instrumentAddr + 5u), instrument.fracPitchMult);

    EXPECT_EQ(aram.read16(sampleDirAddr), sample.originalAddr);
    EXPECT_EQ(aram.read16(static_cast<uint16_t>(sampleDirAddr + 2u)), sample.originalLoopAddr);
    const auto sampleBytes = aram.bytes(sample.originalAddr, sample.data.size());
    EXPECT_TRUE(std::equal(sampleBytes.begin(), sampleBytes.end(), sample.data.begin(), sample.data.end()));
}

TEST(NspcProjectFileTest, ApplyProjectIrOverlayPrunesUnretainedEngineContent) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());
    ASSERT_EQ(project.songs().size(), 2u);
    ASSERT_EQ(project.instruments().size(), 2u);
    ASSERT_EQ(project.samples().size(), 2u);

    NspcProjectIrData overlay{};
    overlay.engineName = "Project file test";
    overlay.retainedEngineSongIds = {0};
    overlay.retainedEngineInstrumentIds = {1};
    overlay.retainedEngineSampleIds = {1};

    auto applyResult = applyProjectIrOverlay(project, overlay);
    ASSERT_TRUE(applyResult.has_value()) << applyResult.error();

    ASSERT_EQ(project.songs().size(), 1u);
    EXPECT_EQ(project.songs()[0].songId(), 0);
    EXPECT_TRUE(project.songs()[0].isEngineProvided());

    ASSERT_EQ(project.instruments().size(), 1u);
    EXPECT_EQ(project.instruments()[0].id, 1);
    EXPECT_EQ(project.instruments()[0].contentOrigin, NspcContentOrigin::EngineProvided);

    ASSERT_EQ(project.samples().size(), 1u);
    EXPECT_EQ(project.samples()[0].id, 1);
    EXPECT_EQ(project.samples()[0].contentOrigin, NspcContentOrigin::EngineProvided);

    const auto& config = project.engineConfig();
    const uint8_t instrumentEntrySize = std::clamp<uint8_t>(config.instrumentEntryBytes, 5, 6);
    const uint16_t instrument0Addr = static_cast<uint16_t>(static_cast<uint32_t>(config.instrumentHeaders));
    const uint16_t sample0DirAddr = static_cast<uint16_t>(static_cast<uint32_t>(config.sampleHeaders));
    const auto aram = project.aram();

    for (uint8_t i = 0; i < instrumentEntrySize; ++i) {
        EXPECT_EQ(aram.read(static_cast<uint16_t>(instrument0Addr + i)), 0);
    }
    EXPECT_EQ(aram.read16(sample0DirAddr), 0);
    EXPECT_EQ(aram.read16(static_cast<uint16_t>(sample0DirAddr + 2u)), 0);
    const auto sample0Bytes = aram.bytes(0x0500, 9);
    EXPECT_TRUE(std::all_of(sample0Bytes.begin(), sample0Bytes.end(), [](uint8_t value) { return value == 0; }));
}

TEST(NspcProjectFileTest, AramUsageSampleDataRegionsCarrySampleIds) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());
    project.refreshAramUsage();

    const auto sample0Region =
        std::find_if(project.aramUsage().regions.begin(), project.aramUsage().regions.end(),
                     [](const NspcAramRegion& region) {
                         return region.kind == NspcAramRegionKind::SampleData && region.objectId == 0;
                     });
    ASSERT_NE(sample0Region, project.aramUsage().regions.end());

    const auto sample1Region =
        std::find_if(project.aramUsage().regions.begin(), project.aramUsage().regions.end(),
                     [](const NspcAramRegion& region) {
                         return region.kind == NspcAramRegionKind::SampleData && region.objectId == 1;
                     });
    ASSERT_NE(sample1Region, project.aramUsage().regions.end());
}

TEST(NspcProjectFileTest, BuildUserContentNspcExportCoalescesAdjacentPackets) {
    NspcEngineConfig config{};
    config.name = "Export format test";
    config.entryPoint = 0x1234;
    config.instrumentHeaders = 0x0300;
    config.instrumentEntryBytes = 6;

    std::array<std::uint8_t, 0x10000> aram{};
    NspcProject project(std::move(config), std::move(aram));

    NspcInstrument inst0{};
    inst0.id = 0;
    inst0.sampleIndex = 0x10;
    inst0.adsr1 = 0x11;
    inst0.adsr2 = 0x12;
    inst0.gain = 0x13;
    inst0.basePitchMult = 0x14;
    inst0.fracPitchMult = 0x15;
    inst0.contentOrigin = NspcContentOrigin::UserProvided;

    NspcInstrument inst1{};
    inst1.id = 1;
    inst1.sampleIndex = 0x20;
    inst1.adsr1 = 0x21;
    inst1.adsr2 = 0x22;
    inst1.gain = 0x23;
    inst1.basePitchMult = 0x24;
    inst1.fracPitchMult = 0x25;
    inst1.contentOrigin = NspcContentOrigin::UserProvided;

    project.instruments().push_back(inst0);
    project.instruments().push_back(inst1);

    auto exportBytes = buildUserContentNspcExport(project);
    ASSERT_TRUE(exportBytes.has_value()) << exportBytes.error();

    auto decoded = decodeNspcExport(*exportBytes);
    ASSERT_TRUE(decoded.has_value());

    const auto& packets = decoded->first;
    const auto entryPoint = decoded->second;
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(entryPoint, 0x1234);

    const auto& packet = packets.front();
    EXPECT_EQ(packet.address, 0x0300);
    EXPECT_EQ(packet.length, 12);
    const std::vector<uint8_t> expected{
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
    };
    EXPECT_EQ(packet.data, expected);
}

TEST(NspcProjectFileTest, BuildSongScopedUploadAllocatesSequenceForNewSongWithoutIndexPointer) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());

    const auto addedSongIndex = project.addEmptySong();
    ASSERT_TRUE(addedSongIndex.has_value());

    const uint16_t songIndexEntryAddr =
        static_cast<uint16_t>(project.engineConfig().songIndexPointers + static_cast<uint16_t>(*addedSongIndex) * 2u);
    const uint16_t initialSequencePtr = project.aram().read16(songIndexEntryAddr);
    EXPECT_EQ(initialSequencePtr, 0u);

    auto compileResult = buildSongScopedUpload(project, static_cast<int>(*addedSongIndex));
    ASSERT_TRUE(compileResult.has_value()) << compileResult.error();

    const auto chunkIt =
        std::find_if(compileResult->upload.chunks.begin(), compileResult->upload.chunks.end(),
                     [songIndexEntryAddr](const NspcUploadChunk& chunk) { return chunk.address == songIndexEntryAddr; });
    ASSERT_NE(chunkIt, compileResult->upload.chunks.end());
    ASSERT_EQ(chunkIt->bytes.size(), 2u);

    const uint16_t sequenceAddr =
        static_cast<uint16_t>(chunkIt->bytes[0] | (static_cast<uint16_t>(chunkIt->bytes[1]) << 8));
    EXPECT_NE(sequenceAddr, 0u);
    EXPECT_NE(sequenceAddr, 0xFFFFu);
}

TEST(NspcProjectFileTest, BuildUserContentUploadFailsWhenUserSongsCollectivelyExceedAram) {
    NspcEngineConfig config{};
    config.name = "Tight ARAM test";
    config.entryPoint = 0x0400;
    config.songIndexPointers = 0x0200;
    config.reserved.push_back(NspcReservedRegion{
        .name = "Nearly all ARAM",
        .from = 0x0000,
        .to = 0xFFE0,
    });

    std::array<std::uint8_t, 0x10000> aram{};
    NspcProject project(std::move(config), std::move(aram));

    ASSERT_TRUE(project.addEmptySong().has_value());
    ASSERT_TRUE(project.addEmptySong().has_value());

    auto upload = buildUserContentUpload(project);
    ASSERT_FALSE(upload.has_value());
    EXPECT_NE(upload.error().find("Failed to compile user song"), std::string::npos);
    EXPECT_NE(upload.error().find("Out of ARAM"), std::string::npos);
}

TEST(NspcProjectFileTest, BuildUserContentUploadFailsWhenNothingIsUserProvided) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());
    auto upload = buildUserContentUpload(project);
    ASSERT_FALSE(upload.has_value());
    EXPECT_NE(upload.error().find("no user-provided"), std::string::npos);
}

TEST(NspcProjectFileTest, BuildUserContentUploadAllowsAliasedUserSampleBrrData) {
    NspcEngineConfig config{};
    config.name = "Aliased sample test";
    config.entryPoint = 0x1234;
    config.sampleHeaders = 0x0200;

    std::array<std::uint8_t, 0x10000> aram{};
    NspcProject project(std::move(config), std::move(aram));

    const std::vector<uint8_t> sharedBrr{
        0x01, 0, 0, 0, 0, 0, 0, 0, 0,
    };

    BrrSample sample0{};
    sample0.id = 0;
    sample0.name = "Alias A";
    sample0.data = sharedBrr;
    sample0.originalAddr = 0x5000;
    sample0.originalLoopAddr = 0x5000;
    sample0.contentOrigin = NspcContentOrigin::UserProvided;

    BrrSample sample1{};
    sample1.id = 1;
    sample1.name = "Alias B";
    sample1.data = sharedBrr;
    sample1.originalAddr = 0x5000;
    sample1.originalLoopAddr = 0x5000;
    sample1.contentOrigin = NspcContentOrigin::UserProvided;

    project.samples().push_back(sample0);
    project.samples().push_back(sample1);

    auto upload = buildUserContentUpload(project);
    ASSERT_TRUE(upload.has_value()) << upload.error();

    size_t brrChunkCount = 0;
    size_t dirChunkCount = 0;
    for (const auto& chunk : upload->chunks) {
        if (chunk.label.find("BRR") != std::string::npos) {
            ++brrChunkCount;
        }
        if (chunk.label.find("Directory") != std::string::npos) {
            ++dirChunkCount;
        }
    }

    EXPECT_EQ(dirChunkCount, 2u);
    EXPECT_EQ(brrChunkCount, 1u);
}

TEST(NspcProjectFileTest, MarkedUserInstrumentAndSampleAreSavedWithBase64BrrData) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());
    ASSERT_TRUE(project.setInstrumentContentOrigin(0, NspcContentOrigin::UserProvided));
    ASSERT_TRUE(project.setSampleContentOrigin(0, NspcContentOrigin::UserProvided));

    const auto path = uniqueTempPath("project-ir-marked", "ntrakproj");
    const auto cleanup = [&]() { std::error_code ec; std::filesystem::remove(path, ec); };
    cleanup();

    auto saveResult = saveProjectIrFile(project, path);
    ASSERT_TRUE(saveResult.has_value()) << saveResult.error();

    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good());
    json root{};
    in >> root;

    ASSERT_TRUE(root.contains("version"));
    EXPECT_EQ(root["version"].get<int>(), 4);

    ASSERT_TRUE(root.contains("instruments"));
    ASSERT_TRUE(root["instruments"].is_array());
    ASSERT_EQ(root["instruments"].size(), 1u);
    EXPECT_EQ(root["instruments"][0]["id"].get<int>(), 0);
    EXPECT_EQ(root["instruments"][0]["contentOrigin"].get<std::string>(), "user");

    ASSERT_TRUE(root.contains("samples"));
    ASSERT_TRUE(root["samples"].is_array());
    ASSERT_EQ(root["samples"].size(), 1u);
    EXPECT_EQ(root["samples"][0]["id"].get<int>(), 0);
    EXPECT_EQ(root["samples"][0]["contentOrigin"].get<std::string>(), "user");
    ASSERT_TRUE(root["samples"][0]["data"].is_string());
    ASSERT_TRUE(root["samples"][0]["dataEncoding"].is_string());
    EXPECT_EQ(root["samples"][0]["dataEncoding"].get<std::string>(), "base64");

    auto loadResult = loadProjectIrFile(path);
    cleanup();
    ASSERT_TRUE(loadResult.has_value()) << loadResult.error();
    ASSERT_EQ(loadResult->samples.size(), 1u);
    EXPECT_EQ(loadResult->samples[0].id, 0);
    EXPECT_EQ(loadResult->samples[0].contentOrigin, NspcContentOrigin::UserProvided);
    EXPECT_EQ(loadResult->samples[0].data, project.samples()[0].data);
}

TEST(NspcProjectFileTest, SaveProjectIrUsesPackedTrackEventEncoding) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());

    const auto addedSongIndex = project.addEmptySong();
    ASSERT_TRUE(addedSongIndex.has_value());

    auto& song = project.songs()[*addedSongIndex];
    NspcTrack track{};
    track.id = 0;
    track.originalAddr = 0x0880;

    NspcEventEntry durationEntry{};
    durationEntry.id = 1;
    durationEntry.event = NspcEvent{Duration{
        .ticks = 0x24,
        .quantization = 0x20,
        .velocity = std::nullopt,
    }};
    durationEntry.originalAddr = 0x0880;
    track.events.push_back(durationEntry);

    NspcEventEntry vcmdEntry{};
    vcmdEntry.id = 2;
    Vcmd volumeVcmd{};
    volumeVcmd.vcmd = VcmdVolume{.volume = 0x6F};
    vcmdEntry.event = NspcEvent{volumeVcmd};
    track.events.push_back(vcmdEntry);

    NspcEventEntry endEntry{};
    endEntry.id = 3;
    endEntry.event = NspcEvent{End{}};
    track.events.push_back(endEntry);

    song.tracks().push_back(std::move(track));

    const auto path = uniqueTempPath("project-ir-packed-track", "ntrakproj");
    const auto cleanup = [&]() { std::error_code ec; std::filesystem::remove(path, ec); };
    cleanup();

    auto saveResult = saveProjectIrFile(project, path);
    ASSERT_TRUE(saveResult.has_value()) << saveResult.error();

    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good());
    json root{};
    in >> root;
    in.close();

    ASSERT_TRUE(root.contains("songs"));
    ASSERT_TRUE(root["songs"].is_array());
    ASSERT_EQ(root["songs"].size(), 1u);
    ASSERT_TRUE(root["songs"][0].contains("tracks"));
    ASSERT_TRUE(root["songs"][0]["tracks"].is_array());
    ASSERT_EQ(root["songs"][0]["tracks"].size(), 1u);

    const auto& trackJson = root["songs"][0]["tracks"][0];
    ASSERT_TRUE(trackJson.contains("eventsEncoding"));
    EXPECT_EQ(trackJson["eventsEncoding"].get<std::string>(), "eventpack_v1");
    ASSERT_TRUE(trackJson.contains("eventsData"));
    ASSERT_TRUE(trackJson["eventsData"].is_string());
    EXPECT_FALSE(trackJson["eventsData"].get<std::string>().empty());
    EXPECT_FALSE(trackJson.contains("events"));

    auto loadResult = loadProjectIrFile(path);
    cleanup();
    ASSERT_TRUE(loadResult.has_value()) << loadResult.error();
    ASSERT_EQ(loadResult->songs.size(), 1u);
    ASSERT_EQ(loadResult->songs[0].tracks().size(), 1u);
    ASSERT_EQ(loadResult->songs[0].tracks()[0].events.size(), 3u);

    const auto* duration = std::get_if<Duration>(&loadResult->songs[0].tracks()[0].events[0].event);
    ASSERT_NE(duration, nullptr);
    EXPECT_EQ(duration->ticks, 0x24);
    ASSERT_TRUE(duration->quantization.has_value());
    EXPECT_EQ(*duration->quantization, 0x20);

    const auto* vcmd = std::get_if<Vcmd>(&loadResult->songs[0].tracks()[0].events[1].event);
    ASSERT_NE(vcmd, nullptr);
    const auto* volume = std::get_if<VcmdVolume>(&vcmd->vcmd);
    ASSERT_NE(volume, nullptr);
    EXPECT_EQ(volume->volume, 0x6F);

    EXPECT_TRUE(std::holds_alternative<End>(loadResult->songs[0].tracks()[0].events[2].event));
}

TEST(NspcProjectFileTest, SaveAndLoadPreservesBaseSpcPathHint) {
    NspcProject project = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::filesystem::path baseSpcPath = "/tmp/audio/base.spc";

    const auto path = uniqueTempPath("project-ir-base", "ntrakproj");
    const auto cleanup = [&]() { std::error_code ec; std::filesystem::remove(path, ec); };
    cleanup();

    auto saveResult = saveProjectIrFile(project, path, baseSpcPath);
    ASSERT_TRUE(saveResult.has_value()) << saveResult.error();

    auto loadResult = loadProjectIrFile(path);
    cleanup();
    ASSERT_TRUE(loadResult.has_value()) << loadResult.error();
    ASSERT_TRUE(loadResult->baseSpcPath.has_value());
    EXPECT_FALSE(loadResult->baseSpcPath->empty());
}

}  // namespace ntrak::nspc
