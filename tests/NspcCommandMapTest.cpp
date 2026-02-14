#include "ntrak/nspc/NspcCompile.hpp"
#include "ntrak/nspc/NspcProject.hpp"
#include "NspcTestHelpers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <vector>

namespace ntrak::nspc {
namespace {

using test_helpers::writeWord;

NspcEngineConfig prototypeEngineConfig() {
    NspcEngineConfig config{};
    config.name = "SMW Prototype Test";
    config.songIndexPointers = 0x0200;

    NspcCommandMap map{};
    map.noteStart = 0x80;
    map.noteEnd = 0xC5;
    map.tie = 0xC6;
    map.restStart = 0xC7;
    map.restEnd = 0xCF;
    map.restWrite = 0xC7;
    map.percussionStart = 0xD0;
    map.percussionEnd = 0xD9;
    map.vcmdStart = 0xDA;
    map.strictReadVcmdMap = true;
    map.strictWriteVcmdMap = true;
    map.readVcmdMap = {
        {0xDA, 0xE0}, {0xDB, 0xE1}, {0xDC, 0xE2}, {0xDD, 0xF9}, {0xDE, 0xE3}, {0xDF, 0xE4},
        {0xE0, 0xE5}, {0xE1, 0xE6}, {0xE2, 0xE7}, {0xE3, 0xE8}, {0xE4, 0xE9}, {0xE5, 0xEB},
        {0xE6, 0xEC}, {0xE7, 0xED}, {0xE8, 0xEE}, {0xE9, 0xEF}, {0xEA, 0xF0}, {0xEB, 0xF1},
        {0xEC, 0xF2}, {0xED, 0xF3}, {0xEE, 0xF4}, {0xEF, 0xF5}, {0xF0, 0xF6}, {0xF1, 0xF7},
        {0xF2, 0xF8},
    };
    map.writeVcmdMap = {
        {0xE0, 0xDA}, {0xE1, 0xDB}, {0xE2, 0xDC}, {0xE3, 0xDE}, {0xE4, 0xDF}, {0xE5, 0xE0},
        {0xE6, 0xE1}, {0xE7, 0xE2}, {0xE8, 0xE3}, {0xE9, 0xE4}, {0xEB, 0xE5}, {0xEC, 0xE6},
        {0xED, 0xE7}, {0xEE, 0xE8}, {0xEF, 0xE9}, {0xF0, 0xEA}, {0xF1, 0xEB}, {0xF2, 0xEC},
        {0xF3, 0xED}, {0xF4, 0xEE}, {0xF5, 0xEF}, {0xF6, 0xF0}, {0xF7, 0xF1}, {0xF8, 0xF2},
        {0xF9, 0xDD},
    };
    config.commandMap = std::move(map);
    return config;
}

NspcProject buildPrototypeProject() {
    std::array<std::uint8_t, 0x10000> aram{};

    // Song index -> sequence table.
    writeWord(aram, 0x0200, 0x0300);
    // Sequence: play pattern @0x0400, then end.
    writeWord(aram, 0x0300, 0x0400);
    writeWord(aram, 0x0302, 0x0000);
    // Pattern: channel 0 -> track @0x0500, other channels off.
    writeWord(aram, 0x0400, 0x0500);
    for (int ch = 1; ch < 8; ++ch) {
        writeWord(aram, static_cast<uint16_t>(0x0400 + ch * 2), 0x0000);
    }

    // Track bytes in prototype format:
    // dur, note, tie, rest, perc, inst, pitch-slide, end
    const std::array<std::uint8_t, 12> trackBytes = {
        0x08, 0x80, 0xC6, 0xC9, 0xD2, 0xDA, 0x07, 0xDD, 0x01, 0x02, 0x03, 0x00,
    };
    std::copy(trackBytes.begin(), trackBytes.end(), aram.begin() + 0x0500);

    return NspcProject(prototypeEngineConfig(), std::move(aram));
}

NspcProject buildPrototypeProjectWithTrackBytes(std::span<const std::uint8_t> trackBytes) {
    std::array<std::uint8_t, 0x10000> aram{};

    // Song index -> sequence table.
    writeWord(aram, 0x0200, 0x0300);
    // Sequence: play pattern @0x0400, then end.
    writeWord(aram, 0x0300, 0x0400);
    writeWord(aram, 0x0302, 0x0000);
    // Pattern: channel 0 -> track @0x0500, other channels off.
    writeWord(aram, 0x0400, 0x0500);
    for (int ch = 1; ch < 8; ++ch) {
        writeWord(aram, static_cast<uint16_t>(0x0400 + ch * 2), 0x0000);
    }

    std::copy(trackBytes.begin(), trackBytes.end(), aram.begin() + 0x0500);
    return NspcProject(prototypeEngineConfig(), std::move(aram));
}

NspcEventEntry makeEntry(NspcEventId& nextId, NspcEvent event) {
    NspcEventEntry entry;
    entry.id = nextId++;
    entry.event = std::move(event);
    entry.originalAddr = std::nullopt;
    return entry;
}

Vcmd makeInst(uint8_t instrument) {
    Vcmd vcmd;
    vcmd.vcmd = VcmdInst{.instrumentIndex = instrument};
    return vcmd;
}

Vcmd makeVolume(uint8_t volume) {
    Vcmd vcmd;
    vcmd.vcmd = VcmdVolume{.volume = volume};
    return vcmd;
}

std::vector<NspcEventEntry> appendMotif(NspcEventId& nextId) {
    std::vector<NspcEventEntry> motif;
    motif.push_back(makeEntry(nextId, Duration{.ticks = 8, .quantization = std::nullopt, .velocity = std::nullopt}));
    motif.push_back(makeEntry(nextId, Note{.pitch = 0x05}));
    motif.push_back(makeEntry(nextId, Note{.pitch = 0x08}));
    motif.push_back(makeEntry(nextId, makeVolume(0x50)));
    motif.push_back(makeEntry(nextId, Duration{.ticks = 6, .quantization = 3, .velocity = 10}));
    motif.push_back(makeEntry(nextId, Rest{}));
    return motif;
}

void appendEvents(std::vector<NspcEventEntry>& out, const std::vector<NspcEventEntry>& source, NspcEventId& nextId) {
    for (const auto& entry : source) {
        out.push_back(makeEntry(nextId, entry.event));
    }
}

NspcSong buildOptimizerFixtureSong() {
    NspcSong song;
    song.setSongId(0);

    NspcEventId nextId = 1;
    const auto motif = appendMotif(nextId);

    NspcTrack track0;
    track0.id = 0;
    track0.originalAddr = 0x1000;
    track0.events.push_back(makeEntry(nextId, makeInst(0x01)));
    appendEvents(track0.events, motif, nextId);
    appendEvents(track0.events, motif, nextId);
    appendEvents(track0.events, motif, nextId);
    track0.events.push_back(makeEntry(nextId, End{}));

    NspcTrack track1;
    track1.id = 1;
    track1.originalAddr = 0x1100;
    track1.events.push_back(makeEntry(nextId, Duration{.ticks = 4, .quantization = std::nullopt, .velocity = std::nullopt}));
    track1.events.push_back(makeEntry(nextId, Note{.pitch = 0x03}));
    appendEvents(track1.events, motif, nextId);
    track1.events.push_back(makeEntry(nextId, Duration{.ticks = 2, .quantization = std::nullopt, .velocity = std::nullopt}));
    track1.events.push_back(makeEntry(nextId, Tie{}));
    appendEvents(track1.events, motif, nextId);
    track1.events.push_back(makeEntry(nextId, End{}));

    song.tracks().push_back(std::move(track0));
    song.tracks().push_back(std::move(track1));

    std::array<int, 8> channelTrackIds = {0, 1, -1, -1, -1, -1, -1, -1};
    song.patterns().push_back(NspcPattern{
        .id = 0,
        .channelTrackIds = channelTrackIds,
        .trackTableAddr = 0x2000,
    });
    song.sequence().push_back(PlayPattern{.patternId = 0, .trackTableAddr = 0x2000});
    song.sequence().push_back(EndSequence{});

    return song;
}

bool hasAnyTrackSubroutineCall(const NspcSong& song) {
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (!vcmd) {
                continue;
            }
            if (std::holds_alternative<VcmdSubroutineCall>(vcmd->vcmd)) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

TEST(NspcCommandMapTest, ParsesPrototypeBytesIntoCommonInternalEvents) {
    NspcProject project = buildPrototypeProject();
    ASSERT_EQ(project.songs().size(), 1u);

    const auto& song = project.songs().front();
    ASSERT_EQ(song.tracks().size(), 1u);
    const auto& events = song.tracks().front().events;
    ASSERT_EQ(events.size(), 8u);

    ASSERT_TRUE(std::holds_alternative<Duration>(events[0].event));
    EXPECT_EQ(std::get<Duration>(events[0].event).ticks, 0x08);

    ASSERT_TRUE(std::holds_alternative<Note>(events[1].event));
    EXPECT_EQ(std::get<Note>(events[1].event).pitch, 0x00);

    EXPECT_TRUE(std::holds_alternative<Tie>(events[2].event));
    EXPECT_TRUE(std::holds_alternative<Rest>(events[3].event));

    ASSERT_TRUE(std::holds_alternative<Percussion>(events[4].event));
    EXPECT_EQ(std::get<Percussion>(events[4].event).index, 0x02);

    ASSERT_TRUE(std::holds_alternative<Vcmd>(events[5].event));
    const auto& instVcmd = std::get<Vcmd>(events[5].event);
    ASSERT_TRUE(std::holds_alternative<VcmdInst>(instVcmd.vcmd));
    EXPECT_EQ(std::get<VcmdInst>(instVcmd.vcmd).instrumentIndex, 0x07);

    ASSERT_TRUE(std::holds_alternative<Vcmd>(events[6].event));
    const auto& slideVcmd = std::get<Vcmd>(events[6].event);
    ASSERT_TRUE(std::holds_alternative<VcmdPitchSlideToNote>(slideVcmd.vcmd));
    EXPECT_EQ(std::get<VcmdPitchSlideToNote>(slideVcmd.vcmd).delay, 0x01);
    EXPECT_EQ(std::get<VcmdPitchSlideToNote>(slideVcmd.vcmd).length, 0x02);
    EXPECT_EQ(std::get<VcmdPitchSlideToNote>(slideVcmd.vcmd).note, 0x03);

    EXPECT_TRUE(std::holds_alternative<End>(events[7].event));
}

TEST(NspcCommandMapTest, EncodesCommonInternalEventsBackToPrototypeBytes) {
    NspcProject project = buildPrototypeProject();

    auto compileResult = buildSongScopedUpload(project, 0);
    ASSERT_TRUE(compileResult.has_value()) << compileResult.error();

    const auto& chunks = compileResult->upload.chunks;
    const auto trackIt = std::find_if(chunks.begin(), chunks.end(),
                                      [](const NspcUploadChunk& chunk) { return chunk.label.starts_with("Track "); });
    ASSERT_NE(trackIt, chunks.end());

    const std::vector<std::uint8_t> expected = {
        0x08, 0x80, 0xC6, 0xC7, 0xD2, 0xDA, 0x07, 0xDD, 0x01, 0x02, 0x03, 0x00,
    };
    EXPECT_EQ(trackIt->bytes, expected);
}

TEST(NspcCommandMapTest, RejectsUnmappedCommonVcmdWhenStrictModeEnabled) {
    NspcProject project = buildPrototypeProject();
    auto& track = project.songs().front().tracks().front();
    ASSERT_GE(track.events.size(), 1u);

    track.events.insert(track.events.end() - 1, NspcEventEntry{
                                               .id = 9999,
                                               .event = NspcEvent{Vcmd{VcmdPerVoiceTranspose{.semitones = 1}}},
                                               .originalAddr = std::nullopt,
                                           });

    auto compileResult = buildSongScopedUpload(project, 0);
    ASSERT_FALSE(compileResult.has_value());
    EXPECT_NE(compileResult.error().find("VCMD $EA"), std::string::npos);
}

TEST(NspcCommandMapTest, RejectsUnmappedRawVcmdWhenStrictReadMappingEnabled) {
    const std::array<std::uint8_t, 4> trackBytes = {
        0x08,
        0xFD,  // Unmapped in prototype read map.
        0x80,
        0x00,
    };
    NspcProject project = buildPrototypeProjectWithTrackBytes(trackBytes);
    EXPECT_TRUE(project.songs().empty());
}

TEST(NspcCommandMapTest, ParsesPrototypeVolumeAndVolumeFadeOpcodes) {
    const std::array<std::uint8_t, 8> trackBytes = {
        0x08, 0xE7, 0x40, 0xE8, 0x02, 0x30, 0x80, 0x00,
    };
    NspcProject project = buildPrototypeProjectWithTrackBytes(trackBytes);
    ASSERT_EQ(project.songs().size(), 1u);

    const auto& events = project.songs().front().tracks().front().events;
    ASSERT_EQ(events.size(), 5u);

    ASSERT_TRUE(std::holds_alternative<Duration>(events[0].event));
    EXPECT_EQ(std::get<Duration>(events[0].event).ticks, 0x08);

    ASSERT_TRUE(std::holds_alternative<Vcmd>(events[1].event));
    ASSERT_TRUE(std::holds_alternative<VcmdVolume>(std::get<Vcmd>(events[1].event).vcmd));
    EXPECT_EQ(std::get<VcmdVolume>(std::get<Vcmd>(events[1].event).vcmd).volume, 0x40);

    ASSERT_TRUE(std::holds_alternative<Vcmd>(events[2].event));
    ASSERT_TRUE(std::holds_alternative<VcmdVolumeFade>(std::get<Vcmd>(events[2].event).vcmd));
    EXPECT_EQ(std::get<VcmdVolumeFade>(std::get<Vcmd>(events[2].event).vcmd).time, 0x02);
    EXPECT_EQ(std::get<VcmdVolumeFade>(std::get<Vcmd>(events[2].event).vcmd).target, 0x30);

    ASSERT_TRUE(std::holds_alternative<Note>(events[3].event));
    EXPECT_TRUE(std::holds_alternative<End>(events[4].event));
}

TEST(NspcCommandMapTest, ParsesFiveByteInstrumentEntryWithZeroFractionalPitch) {
    NspcEngineConfig config{};
    config.name = "SMW 5-byte instrument test";
    config.sampleHeaders = 0x1000;
    config.instrumentHeaders = 0x1100;
    config.instrumentEntryBytes = 5;

    std::array<std::uint8_t, 0x10000> aram{};

    // Sample directory entry 0: start=0x1200, loop=0x1200
    writeWord(aram, 0x1000, 0x1200);
    writeWord(aram, 0x1002, 0x1200);

    // Minimal valid BRR sample: one block, END flag set, range nibble <= 0x0C.
    aram[0x1200] = 0x01;

    // 5-byte instrument entry (sample, ADSR1, ADSR2, GAIN, basePitch)
    aram[0x1100] = 0x00;
    aram[0x1101] = 0x8F;
    aram[0x1102] = 0xE0;
    aram[0x1103] = 0x7F;
    aram[0x1104] = 0x12;

    // Terminator entry (all zero in 5-byte format).
    aram[0x1105] = 0x00;
    aram[0x1106] = 0x00;
    aram[0x1107] = 0x00;
    aram[0x1108] = 0x00;
    aram[0x1109] = 0x00;

    NspcProject project(config, std::move(aram));
    ASSERT_EQ(project.instruments().size(), 1u);
    const auto& inst = project.instruments().front();
    EXPECT_EQ(inst.sampleIndex, 0x00);
    EXPECT_EQ(inst.basePitchMult, 0x12);
    EXPECT_EQ(inst.fracPitchMult, 0x00);
}

TEST(NspcCommandMapTest, BuildSongScopedUploadOnlyPersistsOptimizedSubroutinesWhenEnabled) {
    NspcProject project = buildPrototypeProject();
    auto& song = project.songs().front();
    song = buildOptimizerFixtureSong();

    ASSERT_TRUE(song.subroutines().empty());
    ASSERT_FALSE(hasAnyTrackSubroutineCall(song));

    NspcBuildOptions buildOptions{};
    buildOptions.optimizeSubroutines = true;
    buildOptions.applyOptimizedSongToProject = false;
    auto compileWithoutPersist = buildSongScopedUpload(project, 0, buildOptions);
    ASSERT_TRUE(compileWithoutPersist.has_value()) << compileWithoutPersist.error();

    EXPECT_TRUE(project.songs().front().subroutines().empty());
    EXPECT_FALSE(hasAnyTrackSubroutineCall(project.songs().front()));

    buildOptions.applyOptimizedSongToProject = true;
    auto compileWithPersist = buildSongScopedUpload(project, 0, buildOptions);
    ASSERT_TRUE(compileWithPersist.has_value()) << compileWithPersist.error();

    EXPECT_FALSE(project.songs().front().subroutines().empty());
    EXPECT_TRUE(hasAnyTrackSubroutineCall(project.songs().front()));
}

}  // namespace ntrak::nspc
