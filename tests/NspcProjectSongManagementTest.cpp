#include "ntrak/nspc/NspcProject.hpp"
#include "NspcTestHelpers.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <variant>

namespace ntrak::nspc {
namespace {

using test_helpers::writeWord;

NspcProject buildTwoSongProject() {
    NspcEngineConfig config{};
    config.name = "Song management test";
    config.songIndexPointers = 0x0200;

    std::array<std::uint8_t, 0x10000> aram{};

    // Song table: two songs, then terminator.
    writeWord(aram, 0x0200, 0x0300);
    writeWord(aram, 0x0202, 0x0310);
    writeWord(aram, 0x0204, 0x0000);

    // Song 0 sequence: play pattern @0x0400, end.
    writeWord(aram, 0x0300, 0x0400);
    writeWord(aram, 0x0302, 0x0000);

    // Song 1 sequence: play pattern @0x0420, end.
    writeWord(aram, 0x0310, 0x0420);
    writeWord(aram, 0x0312, 0x0000);

    // Pattern track tables intentionally left all-zero (unassigned channels).
    return NspcProject(config, std::move(aram));
}

}  // namespace

TEST(NspcProjectSongManagementTest, AddEmptySongCreatesDefaultEditableSong) {
    NspcProject project = buildTwoSongProject();
    ASSERT_EQ(project.songs().size(), 2u);

    const auto addedSongIndex = project.addEmptySong();
    ASSERT_TRUE(addedSongIndex.has_value());
    ASSERT_EQ(*addedSongIndex, 2u);
    ASSERT_EQ(project.songs().size(), 3u);

    const auto& song = project.songs()[*addedSongIndex];
    EXPECT_EQ(song.songId(), 2);
    EXPECT_TRUE(song.isUserProvided());
    ASSERT_EQ(song.sequence().size(), 2u);
    ASSERT_TRUE(std::holds_alternative<PlayPattern>(song.sequence()[0]));
    ASSERT_TRUE(std::holds_alternative<EndSequence>(song.sequence()[1]));

    const auto* play = std::get_if<PlayPattern>(&song.sequence()[0]);
    ASSERT_NE(play, nullptr);
    EXPECT_EQ(play->patternId, 0);

    ASSERT_EQ(song.patterns().size(), 1u);
    const auto& pattern = song.patterns()[0];
    EXPECT_EQ(pattern.id, 0);
    ASSERT_TRUE(pattern.channelTrackIds.has_value());
    for (const int trackId : *pattern.channelTrackIds) {
        EXPECT_EQ(trackId, -1);
    }
}

TEST(NspcProjectSongManagementTest, DuplicateSongInsertsCopyAndReindexesLayouts) {
    NspcProject project = buildTwoSongProject();
    ASSERT_EQ(project.songs().size(), 2u);

    NspcSongAddressLayout firstLayout{};
    firstLayout.sequenceAddr = 0x5000;
    project.setSongAddressLayout(0, firstLayout);

    NspcSongAddressLayout secondLayout{};
    secondLayout.sequenceAddr = 0x6000;
    project.setSongAddressLayout(1, secondLayout);

    const auto duplicatedSongIndex = project.duplicateSong(0);
    ASSERT_TRUE(duplicatedSongIndex.has_value());
    ASSERT_EQ(*duplicatedSongIndex, 1u);
    ASSERT_EQ(project.songs().size(), 3u);

    EXPECT_EQ(project.songs()[0].songId(), 0);
    EXPECT_EQ(project.songs()[1].songId(), 1);
    EXPECT_EQ(project.songs()[2].songId(), 2);
    EXPECT_EQ(project.songs()[0].sequence().size(), project.songs()[1].sequence().size());
    EXPECT_TRUE(project.songs()[1].isUserProvided());

    const auto* layoutSong0 = project.songAddressLayout(0);
    ASSERT_NE(layoutSong0, nullptr);
    EXPECT_EQ(layoutSong0->sequenceAddr, 0x5000);

    EXPECT_EQ(project.songAddressLayout(1), nullptr);

    const auto* layoutSong2 = project.songAddressLayout(2);
    ASSERT_NE(layoutSong2, nullptr);
    EXPECT_EQ(layoutSong2->sequenceAddr, 0x6000);
}

TEST(NspcProjectSongManagementTest, RemoveSongReindexesSongsAndLayouts) {
    NspcProject project = buildTwoSongProject();
    ASSERT_EQ(project.songs().size(), 2u);

    NspcSongAddressLayout firstLayout{};
    firstLayout.sequenceAddr = 0x5000;
    project.setSongAddressLayout(0, firstLayout);

    NspcSongAddressLayout secondLayout{};
    secondLayout.sequenceAddr = 0x6000;
    project.setSongAddressLayout(1, secondLayout);

    ASSERT_TRUE(project.removeSong(0));
    ASSERT_EQ(project.songs().size(), 1u);
    EXPECT_EQ(project.songs()[0].songId(), 0);

    const auto* remappedLayout = project.songAddressLayout(0);
    ASSERT_NE(remappedLayout, nullptr);
    EXPECT_EQ(remappedLayout->sequenceAddr, 0x6000);
    EXPECT_EQ(project.songAddressLayout(1), nullptr);

    EXPECT_FALSE(project.removeSong(5));
    EXPECT_FALSE(project.duplicateSong(5).has_value());
}

}  // namespace ntrak::nspc
