#include "ntrak/nspc/NspcFlatten.hpp"
#include "ntrak/nspc/NspcProject.hpp"
#include "NspcTestHelpers.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace ntrak::nspc {
namespace {

using test_helpers::writeWord;

NspcProject buildFlattenClipProject() {
    NspcEngineConfig config{};
    config.name = "Flatten clip test";
    config.songIndexPointers = 0x0200;

    std::array<std::uint8_t, 0x10000> aram{};

    // Song index -> sequence table
    writeWord(aram, 0x0200, 0x0300);
    // Sequence: play pattern @0x0400, then end.
    writeWord(aram, 0x0300, 0x0400);
    writeWord(aram, 0x0302, 0x0000);

    // Pattern: ch0 -> track0 @0x0500, ch6 -> track6 @0x0600
    writeWord(aram, 0x0400, 0x0500);
    for (int ch = 1; ch < 8; ++ch) {
        writeWord(aram, static_cast<std::uint16_t>(0x0400 + ch * 2), 0x0000);
    }
    writeWord(aram, static_cast<std::uint16_t>(0x0400 + 6 * 2), 0x0600);

    // ch0 ends at tick 0x08.
    aram[0x0500] = 0x08;
    aram[0x0501] = 0x80;
    aram[0x0502] = 0x00;

    // ch6 has a tempo command at tick 0x10.
    aram[0x0600] = 0x08;
    aram[0x0601] = 0x80;
    aram[0x0602] = 0x08;
    aram[0x0603] = 0x80;
    aram[0x0604] = 0xE7;
    aram[0x0605] = 0x44;
    aram[0x0606] = 0x00;

    return NspcProject(config, std::move(aram));
}

std::optional<std::uint8_t> findTempoOnChannel(const NspcFlatPattern& pattern, int channel) {
    const auto& events = pattern.channels[static_cast<size_t>(channel)].events;
    for (const auto& event : events) {
        const auto* vcmd = std::get_if<Vcmd>(&event.event);
        if (!vcmd) {
            continue;
        }
        const auto* tempo = std::get_if<VcmdTempo>(&vcmd->vcmd);
        if (!tempo) {
            continue;
        }
        return tempo->tempo;
    }
    return std::nullopt;
}

}  // namespace

TEST(NspcFlattenTest, CanDisableEarliestTrackEndClipping) {
    NspcProject project = buildFlattenClipProject();
    ASSERT_EQ(project.songs().size(), 1u);

    const auto& song = project.songs().front();
    ASSERT_EQ(song.patterns().size(), 1u);
    const int patternId = song.patterns().front().id;

    const auto clipped = flattenPatternById(song, patternId);
    ASSERT_TRUE(clipped.has_value());
    EXPECT_EQ(clipped->totalTicks, 0x08u);
    EXPECT_FALSE(findTempoOnChannel(*clipped, 6).has_value());

    NspcFlattenOptions options{};
    options.clipToEarliestTrackEnd = false;
    const auto unclipped = flattenPatternById(song, patternId, options);
    ASSERT_TRUE(unclipped.has_value());
    EXPECT_EQ(unclipped->totalTicks, 0x10u);

    const auto tempo = findTempoOnChannel(*unclipped, 6);
    ASSERT_TRUE(tempo.has_value());
    EXPECT_EQ(*tempo, 0x44u);
}

}  // namespace ntrak::nspc
