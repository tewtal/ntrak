#include "ntrak/nspc/NspcProjectFile.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <format>
#include <string_view>

namespace ntrak::nspc {
namespace {

using json = nlohmann::json;

std::filesystem::path uniqueTempPath(std::string_view stem, std::string_view ext) {
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    return tempDir / std::format("{}-{}.{}", stem, tick, ext);
}

std::expected<NspcProjectIrData, std::string> writeAndLoad(const json& root, std::string_view stem) {
    const auto path = uniqueTempPath(stem, "nproj");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        return std::unexpected("failed to write temp file");
    }
    out << root.dump(2);
    out.close();

    auto loadResult = loadProjectIrFile(path);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return loadResult;
}

json baseProjectPayload() {
    return json{
        {"format", "ntrak_project_ir"},
        {"version", 4},
        {"engine", "Parse song test"},
        {"songs", json::array()},
        {"instruments", json::array()},
        {"samples", json::array()},
        {"engineRetained", json{{"songs", json::array()}, {"instruments", json::array()}, {"samples", json::array()}}},
    };
}

TEST(NspcProjectFileParseSongTest, LoadProjectIrRejectsInvalidPatternChannelTrackIdsShape) {
    json root = baseProjectPayload();
    root["songs"] = json::array({json{{"songId", 0},
                                      {"contentOrigin", "user"},
                                      {"sequence", json::array()},
                                      {"patterns", json::array({json{{"id", 0},
                                                                       {"trackTableAddr", 0x0700},
                                                                       {"channelTrackIds", json::array({0, 1, 2})}}})},
                                      {"tracks", json::array()},
                                      {"subroutines", json::array()}}});

    auto loadResult = writeAndLoad(root, "parse-song-bad-pattern");
    ASSERT_FALSE(loadResult.has_value());
    EXPECT_NE(loadResult.error().find("channelTrackIds"), std::string::npos);
}

TEST(NspcProjectFileParseSongTest, LoadProjectIrRejectsInvalidTrackEventsPayload) {
    json root = baseProjectPayload();
    root["songs"] = json::array({json{{"songId", 0},
                                      {"contentOrigin", "user"},
                                      {"sequence", json::array()},
                                      {"patterns", json::array()},
                                      {"tracks", json::array({json{{"id", 0},
                                                                    {"originalAddr", 0x0800},
                                                                    {"eventsEncoding", "eventpack_v1"},
                                                                    {"eventsData", json::object()}}})},
                                      {"subroutines", json::array()}}});

    auto loadResult = writeAndLoad(root, "parse-song-bad-track-events");
    ASSERT_FALSE(loadResult.has_value());
    EXPECT_NE(loadResult.error().find("Track eventsData must be a base64 string"), std::string::npos);
}

TEST(NspcProjectFileParseSongTest, LoadProjectIrParsesValidMinimalSong) {
    json root = baseProjectPayload();
    root["songs"] = json::array({json{{"songId", 0},
                                      {"songName", "Parse Song"},
                                      {"author", "Parse Author"},
                                      {"contentOrigin", "user"},
                                      {"sequence", json::array({json{{"type", "endSequence"}}})},
                                      {"patterns", json::array()},
                                      {"tracks", json::array()},
                                      {"subroutines", json::array()}}});

    auto loadResult = writeAndLoad(root, "parse-song-valid-minimal");
    ASSERT_TRUE(loadResult.has_value()) << loadResult.error();
    ASSERT_EQ(loadResult->songs.size(), 1u);
    EXPECT_EQ(loadResult->songs[0].songId(), 0);
    EXPECT_EQ(loadResult->songs[0].songName(), "Parse Song");
    EXPECT_EQ(loadResult->songs[0].author(), "Parse Author");
}

}  // namespace
}  // namespace ntrak::nspc
