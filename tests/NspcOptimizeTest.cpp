#include "ntrak/nspc/NspcOptimize.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

namespace ntrak::nspc {
namespace {

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

Vcmd makePitchSlide(uint8_t delay, uint8_t length, uint8_t note) {
    Vcmd vcmd;
    vcmd.vcmd = VcmdPitchSlideToNote{.delay = delay, .length = length, .note = note};
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

TEST(NspcOptimizeTest, OptimizerExtractsSubroutinesForRepeatedMotifs) {
    NspcSong song = buildOptimizerFixtureSong();
    optimizeSongSubroutines(song);
    EXPECT_FALSE(song.subroutines().empty());
    EXPECT_TRUE(hasAnyTrackSubroutineCall(song));
}

TEST(NspcOptimizeTest, OptimizerAvoidsCallImmediatelyAfterDuration) {
    NspcSong song;
    NspcEventId nextId = 1;

    NspcTrack track;
    track.id = 0;
    track.originalAddr = 0x2000;

    // Repeated phrase where naive extraction may choose a start immediately after Duration.
    track.events.push_back(makeEntry(nextId, Duration{.ticks = 12, .quantization = std::nullopt, .velocity = std::nullopt}));
    track.events.push_back(makeEntry(nextId, Note{.pitch = 0x20}));
    track.events.push_back(makeEntry(nextId, makeInst(0x08)));
    track.events.push_back(makeEntry(nextId, Note{.pitch = 0x20}));
    track.events.push_back(makeEntry(nextId, makeInst(0x09)));
    track.events.push_back(makeEntry(nextId, Note{.pitch = 0x20}));
    track.events.push_back(makeEntry(nextId, Duration{.ticks = 12, .quantization = std::nullopt, .velocity = std::nullopt}));
    track.events.push_back(makeEntry(nextId, Note{.pitch = 0x20}));
    track.events.push_back(makeEntry(nextId, makeInst(0x08)));
    track.events.push_back(makeEntry(nextId, Note{.pitch = 0x20}));
    track.events.push_back(makeEntry(nextId, makeInst(0x09)));
    track.events.push_back(makeEntry(nextId, Note{.pitch = 0x20}));
    track.events.push_back(makeEntry(nextId, End{}));

    song.tracks().push_back(std::move(track));
    std::array<int, 8> channelTrackIds = {0, -1, -1, -1, -1, -1, -1, -1};
    song.patterns().push_back(NspcPattern{
        .id = 0,
        .channelTrackIds = channelTrackIds,
        .trackTableAddr = 0x2100,
    });

    optimizeSongSubroutines(song);

    for (const auto& optimizedTrack : song.tracks()) {
        for (size_t i = 1; i < optimizedTrack.events.size(); ++i) {
            const auto& prev = optimizedTrack.events[i - 1];
            const auto& cur = optimizedTrack.events[i];
            if (!std::holds_alternative<Duration>(prev.event)) {
                continue;
            }
            const auto* vcmd = std::get_if<Vcmd>(&cur.event);
            if (vcmd == nullptr) {
                continue;
            }
            const auto* call = std::get_if<VcmdSubroutineCall>(&vcmd->vcmd);
            EXPECT_TRUE(call == nullptr) << "Found Duration->Call boundary at track " << optimizedTrack.id
                                         << " event index " << i;
        }
    }
}

TEST(NspcOptimizeTest, OptimizerDoesNotEmitZeroCountSubroutineCalls) {
    NspcSong song = buildOptimizerFixtureSong();
    optimizeSongSubroutines(song);

    bool sawSubroutineCall = false;
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (!vcmd) {
                continue;
            }
            const auto* call = std::get_if<VcmdSubroutineCall>(&vcmd->vcmd);
            if (!call) {
                continue;
            }
            sawSubroutineCall = true;
            EXPECT_GT(call->count, 0u) << "Track " << track.id << " contains Cal with count=$00";
        }
    }

    EXPECT_TRUE(sawSubroutineCall);
}

TEST(NspcOptimizeTest, OptimizerExtractsTwoRepeatRunWithCountTwo) {
    NspcSong song;
    NspcEventId nextId = 1;

    NspcTrack track;
    track.id = 0;
    track.originalAddr = 0x2200;

    auto appendPhrase = [&]() {
        track.events.push_back(
            makeEntry(nextId, Duration{.ticks = 8, .quantization = std::nullopt, .velocity = std::nullopt}));
        track.events.push_back(makeEntry(nextId, Note{.pitch = 0x20}));
        track.events.push_back(makeEntry(nextId, makeVolume(0x60)));
        track.events.push_back(makeEntry(nextId, Rest{}));
        track.events.push_back(makeEntry(nextId, Tie{}));
    };

    appendPhrase();
    appendPhrase();
    track.events.push_back(makeEntry(nextId, End{}));

    song.tracks().push_back(std::move(track));
    std::array<int, 8> channelTrackIds = {0, -1, -1, -1, -1, -1, -1, -1};
    song.patterns().push_back(NspcPattern{
        .id = 0,
        .channelTrackIds = channelTrackIds,
        .trackTableAddr = 0x2300,
    });

    optimizeSongSubroutines(song);

    ASSERT_FALSE(song.subroutines().empty());
    bool foundCountTwoCall = false;
    for (const auto& optimizedTrack : song.tracks()) {
        for (const auto& entry : optimizedTrack.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (!vcmd) {
                continue;
            }
            const auto* call = std::get_if<VcmdSubroutineCall>(&vcmd->vcmd);
            if (!call) {
                continue;
            }
            if (call->count == 2u) {
                foundCountTwoCall = true;
            }
        }
    }

    EXPECT_TRUE(foundCountTwoCall);
}

TEST(NspcOptimizeTest, OptimizerAllowsCountOneForSeparatedRuns) {
    NspcSong song;
    NspcEventId nextId = 1;

    NspcTrack track;
    track.id = 0;
    track.originalAddr = 0x2400;

    auto appendPhrase = [&]() {
        track.events.push_back(
            makeEntry(nextId, Duration{.ticks = 8, .quantization = std::nullopt, .velocity = std::nullopt}));
        track.events.push_back(makeEntry(nextId, Note{.pitch = 0x20}));
        track.events.push_back(makeEntry(nextId, makeVolume(0x60)));
        track.events.push_back(makeEntry(nextId, makeInst(0x08)));
        track.events.push_back(makeEntry(nextId, Rest{}));
        track.events.push_back(makeEntry(nextId, Tie{}));
        track.events.push_back(makeEntry(nextId, makeVolume(0x62)));
        track.events.push_back(makeEntry(nextId, makeInst(0x09)));
        track.events.push_back(makeEntry(nextId, Note{.pitch = 0x22}));
        track.events.push_back(makeEntry(nextId, Rest{}));
    };

    appendPhrase();
    track.events.push_back(makeEntry(nextId, makeInst(0x41)));
    appendPhrase();
    track.events.push_back(makeEntry(nextId, makeInst(0x42)));
    appendPhrase();
    track.events.push_back(makeEntry(nextId, End{}));

    song.tracks().push_back(std::move(track));
    std::array<int, 8> channelTrackIds = {0, -1, -1, -1, -1, -1, -1, -1};
    song.patterns().push_back(NspcPattern{
        .id = 0,
        .channelTrackIds = channelTrackIds,
        .trackTableAddr = 0x2500,
    });

    optimizeSongSubroutines(song);

    ASSERT_FALSE(song.subroutines().empty());
    bool foundCountOneCall = false;
    for (const auto& optimizedTrack : song.tracks()) {
        for (const auto& entry : optimizedTrack.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (!vcmd) {
                continue;
            }
            const auto* call = std::get_if<VcmdSubroutineCall>(&vcmd->vcmd);
            if (!call) {
                continue;
            }
            if (call->count == 1u) {
                foundCountOneCall = true;
            }
        }
    }

    EXPECT_TRUE(foundCountOneCall);
}

TEST(NspcOptimizeTest, OptimizerAvoidsCountOneForControlOnlyPhrase) {
    NspcSong song;
    NspcEventId nextId = 1;

    NspcTrack track;
    track.id = 0;
    track.originalAddr = 0x2600;

    auto appendControlPhrase = [&]() {
        track.events.push_back(makeEntry(nextId, makeInst(0x10)));
        track.events.push_back(makeEntry(nextId, makeVolume(0x20)));
        track.events.push_back(makeEntry(nextId, makeInst(0x11)));
        track.events.push_back(makeEntry(nextId, makeVolume(0x21)));
        track.events.push_back(makeEntry(nextId, makeInst(0x12)));
        track.events.push_back(makeEntry(nextId, makeVolume(0x22)));
        track.events.push_back(makeEntry(nextId, makeInst(0x13)));
    };

    appendControlPhrase();
    track.events.push_back(makeEntry(nextId, makeInst(0x40)));
    appendControlPhrase();
    track.events.push_back(makeEntry(nextId, makeInst(0x41)));
    appendControlPhrase();
    track.events.push_back(makeEntry(nextId, End{}));

    song.tracks().push_back(std::move(track));
    std::array<int, 8> channelTrackIds = {0, -1, -1, -1, -1, -1, -1, -1};
    song.patterns().push_back(NspcPattern{
        .id = 0,
        .channelTrackIds = channelTrackIds,
        .trackTableAddr = 0x2700,
    });

    optimizeSongSubroutines(song);

    bool sawAnyCall = false;
    bool sawCountOneCall = false;
    for (const auto& optimizedTrack : song.tracks()) {
        for (const auto& entry : optimizedTrack.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (!vcmd) {
                continue;
            }
            const auto* call = std::get_if<VcmdSubroutineCall>(&vcmd->vcmd);
            if (!call) {
                continue;
            }
            sawAnyCall = true;
            if (call->count == 1u) {
                sawCountOneCall = true;
            }
        }
    }

    EXPECT_FALSE(sawAnyCall);
    EXPECT_FALSE(sawCountOneCall);
    EXPECT_TRUE(song.subroutines().empty());
}

TEST(NspcOptimizeTest, OptimizerAvoidsCallBoundaryImmediatelyBeforePitchSlide) {
    NspcSong song;
    NspcEventId nextId = 1;

    NspcTrack track;
    track.id = 0;
    track.originalAddr = 0x2A00;

    auto appendPhrase = [&](uint8_t slideNote) {
        track.events.push_back(
            makeEntry(nextId, Duration{.ticks = 8, .quantization = std::nullopt, .velocity = std::nullopt}));
        track.events.push_back(makeEntry(nextId, Note{.pitch = 0x20}));
        track.events.push_back(makeEntry(nextId, makeVolume(0x40)));
        track.events.push_back(makeEntry(nextId, makePitchSlide(1, 2, slideNote)));
    };

    appendPhrase(0x30);
    appendPhrase(0x31);
    appendPhrase(0x32);
    track.events.push_back(makeEntry(nextId, End{}));

    song.tracks().push_back(std::move(track));
    std::array<int, 8> channelTrackIds = {0, -1, -1, -1, -1, -1, -1, -1};
    song.patterns().push_back(NspcPattern{
        .id = 0,
        .channelTrackIds = channelTrackIds,
        .trackTableAddr = 0x2B00,
    });

    optimizeSongSubroutines(song);

    auto isCall = [](const NspcEventEntry& entry) {
        const auto* vcmd = std::get_if<Vcmd>(&entry.event);
        return vcmd && std::holds_alternative<VcmdSubroutineCall>(vcmd->vcmd);
    };
    auto isPitchSlide = [](const NspcEventEntry& entry) {
        const auto* vcmd = std::get_if<Vcmd>(&entry.event);
        return vcmd && std::holds_alternative<VcmdPitchSlideToNote>(vcmd->vcmd);
    };

    for (const auto& optimizedTrack : song.tracks()) {
        for (size_t i = 0; i + 1 < optimizedTrack.events.size(); ++i) {
            EXPECT_FALSE(isCall(optimizedTrack.events[i]) && isPitchSlide(optimizedTrack.events[i + 1]))
                << "Found Call->F9 boundary at track " << optimizedTrack.id << " event index " << i;
        }
    }
}

TEST(NspcOptimizeTest, OptimizerDoesNotCreateSubroutineStartingWithPitchSlide) {
    NspcSong song;
    NspcEventId nextId = 1;

    NspcTrack track;
    track.id = 0;
    track.originalAddr = 0x2C00;

    auto appendPhrase = [&](uint8_t instrument) {
        track.events.push_back(makeEntry(nextId, makePitchSlide(1, 2, 0x30)));
        track.events.push_back(makeEntry(nextId, makeInst(instrument)));
    };

    appendPhrase(0x10);
    appendPhrase(0x11);
    appendPhrase(0x12);
    track.events.push_back(makeEntry(nextId, End{}));

    song.tracks().push_back(std::move(track));
    std::array<int, 8> channelTrackIds = {0, -1, -1, -1, -1, -1, -1, -1};
    song.patterns().push_back(NspcPattern{
        .id = 0,
        .channelTrackIds = channelTrackIds,
        .trackTableAddr = 0x2D00,
    });

    optimizeSongSubroutines(song);

    for (const auto& subroutine : song.subroutines()) {
        ASSERT_FALSE(subroutine.events.empty());
        const auto* vcmd = std::get_if<Vcmd>(&subroutine.events.front().event);
        const bool startsWithPitchSlide = vcmd && std::holds_alternative<VcmdPitchSlideToNote>(vcmd->vcmd);
        EXPECT_FALSE(startsWithPitchSlide) << "Subroutine " << subroutine.id << " starts with F9";
    }
}

TEST(NspcOptimizeTest, OptimizerDoesNotEndSubroutinesWithBareDuration) {
    NspcSong song;
    NspcEventId nextId = 1;

    NspcTrack track;
    track.id = 0;
    track.originalAddr = 0x2800;

    auto appendPhrase = [&]() {
        track.events.push_back(makeEntry(nextId, makeInst(0x20)));
        track.events.push_back(
            makeEntry(nextId, Duration{.ticks = 8, .quantization = std::nullopt, .velocity = std::nullopt}));
        track.events.push_back(makeEntry(nextId, Note{.pitch = 0x21}));
        track.events.push_back(makeEntry(nextId, makeVolume(0x40)));
        track.events.push_back(
            makeEntry(nextId, Duration{.ticks = 6, .quantization = std::nullopt, .velocity = std::nullopt}));
        track.events.push_back(makeEntry(nextId, Note{.pitch = 0x22}));
        track.events.push_back(makeEntry(nextId, makeVolume(0x41)));
        track.events.push_back(
            makeEntry(nextId, Duration{.ticks = 4, .quantization = std::nullopt, .velocity = std::nullopt}));
    };

    appendPhrase();
    appendPhrase();
    appendPhrase();
    track.events.push_back(makeEntry(nextId, End{}));

    song.tracks().push_back(std::move(track));
    std::array<int, 8> channelTrackIds = {0, -1, -1, -1, -1, -1, -1, -1};
    song.patterns().push_back(NspcPattern{
        .id = 0,
        .channelTrackIds = channelTrackIds,
        .trackTableAddr = 0x2900,
    });

    optimizeSongSubroutines(song);

    for (const auto& subroutine : song.subroutines()) {
        ASSERT_FALSE(subroutine.events.empty());

        size_t endIndex = subroutine.events.size() - 1;
        for (size_t i = 0; i < subroutine.events.size(); ++i) {
            if (std::holds_alternative<End>(subroutine.events[i].event)) {
                endIndex = i;
                break;
            }
        }
        if (endIndex == 0) {
            continue;
        }

        const auto* duration = std::get_if<Duration>(&subroutine.events[endIndex - 1].event);
        if (!duration) {
            continue;
        }
        const bool bareDuration = !duration->quantization.has_value() && !duration->velocity.has_value();
        EXPECT_FALSE(bareDuration) << "Subroutine " << subroutine.id << " ends with bare Duration before End";
    }
}

}  // namespace ntrak::nspc
