#include "ntrak/nspc/NspcCommandHistory.hpp"
#include "ntrak/nspc/NspcData.hpp"
#include "ntrak/nspc/NspcEditor.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <format>
#include <iostream>
#include <memory>
#include <vector>

namespace ntrak::nspc {
namespace {

// Helper to create a track with events
NspcEventId addTrackWithEvents(NspcSong& song, int trackId, const std::vector<NspcEvent>& events) {
    auto& tracks = song.tracks();
    if (static_cast<size_t>(trackId) >= tracks.size()) {
        tracks.resize(trackId + 1);
    }

    NspcEventId nextId = 1;
    std::vector<NspcEventEntry> entries;
    entries.reserve(events.size() + 1);
    for (const auto& event : events) {
        entries.push_back(NspcEventEntry{
            .id = nextId++,
            .event = event,
            .originalAddr = std::nullopt,
        });
    }
    entries.push_back(NspcEventEntry{
        .id = nextId++,
        .event = End{},
        .originalAddr = std::nullopt,
    });

    tracks[trackId] = NspcTrack{
        .id = trackId,
        .events = std::move(entries),
        .originalAddr = 0x1000,
    };

    return nextId;
}

// Helper to add a pattern referencing a track
void addPattern(NspcSong& song, int patternId, int channel, int trackId) {
    auto& patterns = song.patterns();
    if (static_cast<size_t>(patternId) >= patterns.size()) {
        patterns.resize(patternId + 1);
    }

    std::array<int, 8> trackIds = {-1, -1, -1, -1, -1, -1, -1, -1};
    trackIds[channel] = trackId;

    patterns[patternId] = NspcPattern{
        .id = patternId,
        .channelTrackIds = trackIds,
        .trackTableAddr = 0x2000,
    };
}

// Helper to encode a track's events to bytes
std::vector<uint8_t> encodeTrack(const NspcTrack& track) {
    std::unordered_map<int, uint16_t> emptySubroutineMap;
    std::vector<std::string> warnings;

    std::vector<uint8_t> result;
    for (const auto& entry : track.events) {
        std::visit(
            [&result](const auto& event) {
                using T = std::decay_t<decltype(event)>;
                if constexpr (std::is_same_v<T, Duration>) {
                    result.push_back(event.ticks);
                    if (event.quantization.has_value() || event.velocity.has_value()) {
                        uint8_t qv =
                            static_cast<uint8_t>((event.quantization.value_or(0) << 4) | event.velocity.value_or(0));
                        result.push_back(qv);
                    }
                } else if constexpr (std::is_same_v<T, Note>) {
                    result.push_back(static_cast<uint8_t>(0x80 + event.pitch));
                } else if constexpr (std::is_same_v<T, Tie>) {
                    result.push_back(0xC8);
                } else if constexpr (std::is_same_v<T, Rest>) {
                    result.push_back(0xC9);
                } else if constexpr (std::is_same_v<T, End>) {
                    result.push_back(0x00);
                } else if constexpr (std::is_same_v<T, Vcmd>) {
                    // Encode VcmdInst
                    if (std::holds_alternative<VcmdInst>(event.vcmd)) {
                        result.push_back(0xE0);  // Instrument command opcode
                        result.push_back(std::get<VcmdInst>(event.vcmd).instrumentIndex);
                    }
                }
            },
            entry.event);
    }
    return result;
}

// Helper to print byte vector as hex
std::string bytesToHex(const std::vector<uint8_t>& bytes) {
    std::string result;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i > 0) {
            result += " ";
        }
        result += std::format("{:02X}", bytes[i]);
    }
    return result;
}

// Helper to compare byte vectors and report differences
void compareBytes(const std::string& context, const std::vector<uint8_t>& before, const std::vector<uint8_t>& after,
                  const std::vector<uint8_t>& expected) {
    std::cout << "\n=== " << context << " ===\n";
    std::cout << "Before:   " << bytesToHex(before) << '\n';
    std::cout << "After:    " << bytesToHex(after) << '\n';
    std::cout << "Expected: " << bytesToHex(expected) << '\n';

    if (after != expected) {
        std::cout << "MISMATCH DETECTED!\n";
        for (size_t i = 0; i < std::max(after.size(), expected.size()); ++i) {
            if (i >= after.size()) {
                std::cout << "  [" << i << "] missing, expected " << std::format("{:02X}", expected[i]) << '\n';
            } else if (i >= expected.size()) {
                std::cout << "  [" << i << "] extra byte " << std::format("{:02X}", after[i]) << '\n';
            } else if (after[i] != expected[i]) {
                std::cout << "  [" << i << "] got " << std::format("{:02X}", after[i]) << ", expected "
                          << std::format("{:02X}", expected[i]) << '\n';
            }
        }
    }
}

const Vcmd* eventAsVcmd(const NspcEventEntry& entry) {
    return std::get_if<Vcmd>(&entry.event);
}

//=============================================================================
// Test: Track ID Consistency
//=============================================================================

TEST(NspcEditorTest, TrackIdConsistency) {
    NspcSong song;

    // Create tracks 0, 1, 2
    addTrackWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});
    addTrackWithEvents(song, 1, {Duration{.ticks = 8}, Note{.pitch = 36}});
    addTrackWithEvents(song, 2, {Duration{.ticks = 8}, Note{.pitch = 48}});

    // Verify all tracks have matching id == index
    const auto& tracks = song.tracks();
    for (size_t i = 0; i < tracks.size(); ++i) {
        EXPECT_EQ(tracks[i].id, static_cast<int>(i)) << "Track at index " << i << " has mismatched id " << tracks[i].id;
    }
}

//=============================================================================
// Test: SetRowEvent at start of note
//=============================================================================

TEST(NspcEditorTest, SetRowEvent_AtStartOfNote) {
    NspcSong song;
    NspcEditor editor;

    // Create: Duration(8), Note C (pitch 24)
    addTrackWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    auto& track = song.tracks()[0];
    std::vector<uint8_t> before = encodeTrack(track);

    // Edit: Change note to D (pitch 26) at row 0
    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    bool result = editor.setRowEvent(song, loc, Note{.pitch = 26});

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // Expected: Duration(8), Note D (0x80 + 26 = 0x9A), End
    std::vector<uint8_t> expected = {0x08, 0x9A, 0x00};

    compareBytes("SetRowEvent_AtStartOfNote", before, after, expected);

    EXPECT_TRUE(result) << "setRowEvent should return true";
    EXPECT_EQ(after, expected) << "Byte stream mismatch";
}

//=============================================================================
// Test: SetRowEvent mid-span (splits note)
//=============================================================================

TEST(NspcEditorTest, SetRowEvent_MidSpan) {
    NspcSong song;
    NspcEditor editor;

    // Create: Duration(8), Note C (pitch 24) — spans rows 0-7
    addTrackWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    auto& track = song.tracks()[0];
    std::vector<uint8_t> before = encodeTrack(track);

    // Edit: Insert Note D at row 3 (should split)
    // Result should be: Duration(3), Note C, Duration(5), Note D, End
    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 3};
    bool result = editor.setRowEvent(song, loc, Note{.pitch = 26});

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // Expected: Duration(3), Note C (0x98), Duration(5), Note D (0x9A), End
    std::vector<uint8_t> expected = {0x03, 0x98, 0x05, 0x9A, 0x00};

    compareBytes("SetRowEvent_MidSpan", before, after, expected);

    EXPECT_TRUE(result) << "setRowEvent should return true";
    EXPECT_EQ(after, expected) << "Byte stream mismatch after mid-span split";
}

TEST(NspcEditorTest, SetRowEvent_MidSpanPreservesQvOnLeadingSegment) {
    NspcSong song;
    NspcEditor editor;

    // Create: Duration(8, q=4,v=12), Note C
    addTrackWithEvents(song, 0, {Duration{.ticks = 8, .quantization = 4, .velocity = 12}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    // Split span at row 3
    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 3};
    const bool result = editor.setRowEvent(song, loc, Note{.pitch = 26});
    EXPECT_TRUE(result);

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);
    std::vector<uint8_t> expected = {0x03, 0x4C, 0x98, 0x05, 0x9A, 0x00};
    EXPECT_EQ(after, expected);
}

TEST(NspcEditorTest, SetRowEvent_MidSpanContinuation_NoChange) {
    NspcSong song;
    NspcEditor editor;

    // Create: Duration(8), Note C (rows 0-7)
    addTrackWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    std::vector<uint8_t> before = encodeTrack(song.tracks()[0]);

    // Tie inside an existing note span is already implied; should not split.
    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 3};
    const bool result = editor.setRowEvent(song, loc, Tie{});

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);
    std::vector<uint8_t> expected = {0x08, 0x98, 0x00};

    EXPECT_FALSE(result) << "Setting implicit continuation should be a no-op";
    EXPECT_EQ(before, expected);
    EXPECT_EQ(after, expected);
}

//=============================================================================
// Test: InsertTickAtRow / RemoveTickAtRow
//=============================================================================

TEST(NspcEditorTest, InsertTickAtRow_BoundaryShiftsLaterRowsDown) {
    NspcSong song;
    NspcEditor editor;

    // Row 0: Note C (len 4), Row 4: Note D (len 4)
    addTrackWithEvents(song, 0, {Duration{.ticks = 4}, Note{.pitch = 24}, Duration{.ticks = 4}, Note{.pitch = 26}});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 4};
    const bool result = editor.insertTickAtRow(song, loc);
    const std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // Inserted row is a one-tick continuation; later rows shift by +1.
    const std::vector<uint8_t> expected = {0x04, 0x98, 0x01, 0xC8, 0x04, 0x9A, 0x00};
    EXPECT_TRUE(result);
    EXPECT_EQ(after, expected);
}

TEST(NspcEditorTest, InsertTickAtRow_MidSpanSplitsAndExtendsTail) {
    NspcSong song;
    NspcEditor editor;

    // Row 0: Note C (len 8)
    addTrackWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 3};
    const bool result = editor.insertTickAtRow(song, loc);
    const std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // Split at row 3, then extend the tail by one tick (5 -> 6).
    const std::vector<uint8_t> expected = {0x03, 0x98, 0x06, 0xC8, 0x00};
    EXPECT_TRUE(result);
    EXPECT_EQ(after, expected);
}

TEST(NspcEditorTest, RemoveTickAtRow_DeletesRowAndPullsLaterRowsUp) {
    NspcSong song;
    NspcEditor editor;

    // Row 0: Note C (len 4), Row 4: Tie (len 1), Row 5: Note D (len 4)
    addTrackWithEvents(song, 0,
                       {Duration{.ticks = 4}, Note{.pitch = 24}, Duration{.ticks = 1}, Tie{}, Duration{.ticks = 4},
                        Note{.pitch = 26}});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 4};
    const bool result = editor.removeTickAtRow(song, loc);
    const std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // The inserted tie row is removed; note D returns to row 4.
    const std::vector<uint8_t> expected = {0x04, 0x98, 0x04, 0x9A, 0x00};
    EXPECT_TRUE(result);
    EXPECT_EQ(after, expected);
}

TEST(NspcEditorTest, RemoveTickAtRow_RemovesCommandsAnchoredOnDeletedRow) {
    NspcSong song;
    NspcEditor editor;

    // Row 0: Note C (len 4), Row 4: Instrument cmd + Note D (len 4)
    addTrackWithEvents(song, 0,
                       {Duration{.ticks = 4}, Note{.pitch = 24}, Vcmd{VcmdInst{.instrumentIndex = 5}},
                        Duration{.ticks = 4}, Note{.pitch = 26}});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 4};
    const bool result = editor.removeTickAtRow(song, loc);
    const std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // Command at deleted row is removed; second note shortens by one tick.
    const std::vector<uint8_t> expected = {0x04, 0x98, 0x03, 0x9A, 0x00};
    EXPECT_TRUE(result);
    EXPECT_EQ(after, expected);
}

//=============================================================================
// Test: SetInstrument adds new instrument command
//=============================================================================

TEST(NspcEditorTest, SetInstrument_NewCommand) {
    NspcSong song;
    NspcEditor editor;

    // Create: Duration(8), Note C
    addTrackWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    auto& track = song.tracks()[0];
    std::vector<uint8_t> before = encodeTrack(track);

    // Add instrument 5 at row 0
    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    bool result = editor.setInstrumentAtRow(song, loc, 5);

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // Expected: Instrument(5), Duration(8), Note C, End
    // Instrument command: 0xE0 0x05
    std::vector<uint8_t> expected = {0xE0, 0x05, 0x08, 0x98, 0x00};

    compareBytes("SetInstrument_NewCommand", before, after, expected);

    EXPECT_TRUE(result) << "setInstrumentAtRow should return true";
    EXPECT_EQ(after, expected) << "Byte stream mismatch after adding instrument";
}

//=============================================================================
// Test: SetInstrument replaces existing instrument
//=============================================================================

TEST(NspcEditorTest, SetInstrument_ReplaceExisting) {
    NspcSong song;
    NspcEditor editor;

    // Create: Instrument(3), Duration(8), Note C
    addTrackWithEvents(song, 0, {Vcmd{VcmdInst{.instrumentIndex = 3}}, Duration{.ticks = 8}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    auto& track = song.tracks()[0];
    std::vector<uint8_t> before = encodeTrack(track);

    // Change instrument to 7 at row 0
    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    bool result = editor.setInstrumentAtRow(song, loc, 7);

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // Expected: Instrument(7), Duration(8), Note C, End
    std::vector<uint8_t> expected = {0xE0, 0x07, 0x08, 0x98, 0x00};

    compareBytes("SetInstrument_ReplaceExisting", before, after, expected);

    EXPECT_TRUE(result) << "setInstrumentAtRow should return true";
    EXPECT_EQ(after, expected) << "Byte stream mismatch after replacing instrument";
}

//=============================================================================
// Test: SetInstrument on unassigned channel creates full-length track
//=============================================================================

TEST(NspcEditorTest, SetInstrument_UnassignedChannelExtendsToPatternEnd) {
    NspcSong song;
    NspcEditor editor;

    // Channel 0 establishes an 8-tick baseline pattern length.
    addTrackWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation loc{.patternId = 0, .channel = 1, .row = 0};
    bool result = editor.setInstrumentAtRow(song, loc, 5);

    ASSERT_TRUE(song.patterns()[0].channelTrackIds.has_value());
    const int trackId = song.patterns()[0].channelTrackIds.value()[1];
    ASSERT_GE(trackId, 0);
    ASSERT_LT(static_cast<size_t>(trackId), song.tracks().size());

    std::vector<uint8_t> after = encodeTrack(song.tracks()[static_cast<size_t>(trackId)]);
    std::vector<uint8_t> expected = {
        0x01, 0xE0, 0x05, 0xC8,  // row 0 command anchor (tie)
        0x07, 0xC8,              // auto-extension to row 8 with ties
        0x00,
    };

    EXPECT_TRUE(result) << "setInstrumentAtRow should return true";
    EXPECT_EQ(after, expected) << "Unassigned channel track should be extended to pattern baseline";
}

//=============================================================================
// Test: DeleteRowEvent
//=============================================================================

TEST(NspcEditorTest, DeleteRowEvent) {
    NspcSong song;
    NspcEditor editor;

    // Create: Duration(8), Note C
    addTrackWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    auto& track = song.tracks()[0];
    std::vector<uint8_t> before = encodeTrack(track);

    // Delete note at row 0
    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    bool result = editor.deleteRowEvent(song, loc);

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // Expected: Duration(8), Tie (0xC8), End
    std::vector<uint8_t> expected = {0x08, 0xC8, 0x00};

    compareBytes("DeleteRowEvent", before, after, expected);

    EXPECT_TRUE(result) << "deleteRowEvent should return true";
    EXPECT_EQ(after, expected) << "Byte stream mismatch after delete";
}

TEST(NspcEditorTest, ClearInstrumentCommandAtInsertedRow_RemovesOrphanContinuationTick) {
    NspcSong song;
    NspcEditor editor;

    addTrackWithEvents(song, 0, {Duration{.ticks = 4}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    const NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 4};
    const bool setChanged = editor.setInstrumentAtRow(song, loc, 0x05);
    EXPECT_TRUE(setChanged);

    const bool clearChanged = editor.setInstrumentAtRow(song, loc, std::nullopt);
    EXPECT_TRUE(clearChanged);

    const std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);
    const std::vector<uint8_t> expected = {0x04, 0x98, 0x00};
    EXPECT_EQ(after, expected);
}

TEST(NspcEditorTest, DeleteRowEvent_BlankContinuationRow_NoChange) {
    NspcSong song;
    NspcEditor editor;

    // Duration(8), Note C. Row 3 is an implicit continuation row.
    addTrackWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    std::vector<uint8_t> before = encodeTrack(song.tracks()[0]);

    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 3};
    const bool result = editor.deleteRowEvent(song, loc);
    EXPECT_FALSE(result);

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);
    EXPECT_EQ(after, before);
}

//=============================================================================
// Test: Empty track - adding note
//=============================================================================

TEST(NspcEditorTest, SetRowEvent_EmptyTrack) {
    NspcSong song;
    NspcEditor editor;

    // Create empty track
    auto& tracks = song.tracks();
    tracks.push_back(NspcTrack{
        .id = 0,
        .events = {NspcEventEntry{.id = 1, .event = End{}, .originalAddr = std::nullopt}},
        .originalAddr = 0x1000,
    });
    addPattern(song, 0, 0, 0);

    auto& track = song.tracks()[0];
    std::vector<uint8_t> before = encodeTrack(track);

    // Add note at row 0
    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    bool result = editor.setRowEvent(song, loc, Note{.pitch = 24});

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // Expected: Duration(1), Note C, End
    std::vector<uint8_t> expected = {0x01, 0x98, 0x00};

    compareBytes("SetRowEvent_EmptyTrack", before, after, expected);

    EXPECT_TRUE(result) << "setRowEvent should return true";
    EXPECT_EQ(after, expected) << "Byte stream mismatch after adding to empty track";
}

//=============================================================================
// Test: Multiple notes - verify timing preserved
//=============================================================================

TEST(NspcEditorTest, SetRowEvent_PreserveTiming) {
    NspcSong song;
    NspcEditor editor;

    // Create: Duration(4), Note C, Duration(4), Note E — total 8 ticks
    addTrackWithEvents(song, 0, {Duration{.ticks = 4}, Note{.pitch = 24}, Duration{.ticks = 4}, Note{.pitch = 28}});
    addPattern(song, 0, 0, 0);

    auto& track = song.tracks()[0];
    std::vector<uint8_t> before = encodeTrack(track);

    // Edit: Change first note to D at row 0
    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    bool result = editor.setRowEvent(song, loc, Note{.pitch = 26});

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // Expected: Duration(4), Note D (0x9A), Duration(4), Note E (0x9C), End
    std::vector<uint8_t> expected = {0x04, 0x9A, 0x04, 0x9C, 0x00};

    compareBytes("SetRowEvent_PreserveTiming", before, after, expected);

    EXPECT_TRUE(result) << "setRowEvent should return true";
    EXPECT_EQ(after, expected) << "Second note timing should be preserved";
}

//=============================================================================
// Test: Subroutine editing - modifying events inside a subroutine
//=============================================================================

// Helper to add a subroutine with events
NspcEventId addSubroutineWithEvents(NspcSong& song, int subroutineId, const std::vector<NspcEvent>& events) {
    auto& subroutines = song.subroutines();
    if (static_cast<size_t>(subroutineId) >= subroutines.size()) {
        subroutines.resize(subroutineId + 1);
    }

    NspcEventId nextId = 1000 + (static_cast<NspcEventId>(subroutineId) * 100);
    std::vector<NspcEventEntry> entries;
    entries.reserve(events.size() + 1);
    for (const auto& event : events) {
        entries.push_back(NspcEventEntry{
            .id = nextId++,
            .event = event,
            .originalAddr = std::nullopt,
        });
    }
    entries.push_back(NspcEventEntry{
        .id = nextId++,
        .event = End{},
        .originalAddr = std::nullopt,
    });

    subroutines[subroutineId] = NspcSubroutine{
        .id = subroutineId,
        .events = std::move(entries),
        .originalAddr = static_cast<uint16_t>(0x3000 + (subroutineId * 0x100)),
    };

    return nextId;
}

// Helper to encode subroutine events
std::vector<uint8_t> encodeSubroutine(const NspcSubroutine& sub) {
    std::vector<uint8_t> result;
    for (const auto& entry : sub.events) {
        std::visit(
            [&result](const auto& event) {
                using T = std::decay_t<decltype(event)>;
                if constexpr (std::is_same_v<T, Duration>) {
                    result.push_back(event.ticks);
                } else if constexpr (std::is_same_v<T, Note>) {
                    result.push_back(static_cast<uint8_t>(0x80 + event.pitch));
                } else if constexpr (std::is_same_v<T, Tie>) {
                    result.push_back(0xC8);
                } else if constexpr (std::is_same_v<T, Rest>) {
                    result.push_back(0xC9);
                } else if constexpr (std::is_same_v<T, End>) {
                    result.push_back(0x00);
                }
            },
            entry.event);
    }
    return result;
}

// Helper to add a track that calls a subroutine
NspcEventId addTrackWithSubroutineCall(NspcSong& song, int trackId, int subroutineId) {
    auto& tracks = song.tracks();
    if (static_cast<size_t>(trackId) >= tracks.size()) {
        tracks.resize(trackId + 1);
    }

    NspcEventId nextId = static_cast<NspcEventId>(100) + (static_cast<NspcEventId>(trackId) * 10);
    std::vector<NspcEventEntry> entries;

    // Add subroutine call (using VcmdSubroutineCall which is how flattening resolves subroutines)
    entries.push_back(NspcEventEntry{
        .id = nextId++,
        .event = Vcmd{VcmdSubroutineCall{.subroutineId = subroutineId, .originalAddr = 0x3000, .count = 0}},
        .originalAddr = std::nullopt,
    });

    // Add End marker
    entries.push_back(NspcEventEntry{
        .id = nextId++,
        .event = End{},
        .originalAddr = std::nullopt,
    });

    tracks[trackId] = NspcTrack{
        .id = trackId,
        .events = std::move(entries),
        .originalAddr = static_cast<uint16_t>(0x1000 + (trackId * 0x100)),
    };

    return nextId;
}

TEST(NspcEditorTest, EditEventInsideSubroutine) {
    NspcSong song;
    NspcEditor editor;

    // Create subroutine with: Duration(8), Note C
    addSubroutineWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});

    // Create track that calls the subroutine
    addTrackWithSubroutineCall(song, 0, 0);
    addPattern(song, 0, 0, 0);

    std::vector<uint8_t> before = encodeSubroutine(song.subroutines()[0]);

    // Edit: Change the note (from subroutine) to D at row 0
    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    bool result = editor.setRowEvent(song, loc, Note{.pitch = 26});

    // The subroutine should be modified, not the main track
    std::vector<uint8_t> after = encodeSubroutine(song.subroutines()[0]);

    // Expected: Duration(8), Note D, End
    std::vector<uint8_t> expected = {0x08, 0x9A, 0x00};

    compareBytes("EditEventInsideSubroutine", before, after, expected);

    EXPECT_TRUE(result) << "setRowEvent should return true";
    EXPECT_EQ(after, expected) << "Subroutine events should be modified";
}

//=============================================================================
// Test: Shared track between patterns
//=============================================================================

TEST(NspcEditorTest, SharedTrack_EditAffectsBoth) {
    NspcSong song;
    NspcEditor editor;

    // Create one track used by two patterns
    addTrackWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});

    // Both patterns reference the same track
    addPattern(song, 0, 0, 0);
    addPattern(song, 1, 0, 0);  // Same trackId=0

    std::vector<uint8_t> before = encodeTrack(song.tracks()[0]);

    // Edit via pattern 0
    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    bool result = editor.setRowEvent(song, loc, Note{.pitch = 26});

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // Expected: Duration(8), Note D, End
    std::vector<uint8_t> expected = {0x08, 0x9A, 0x00};

    compareBytes("SharedTrack_EditAffectsBoth", before, after, expected);

    EXPECT_TRUE(result) << "setRowEvent should return true";
    EXPECT_EQ(after, expected) << "Shared track should be modified";

    // Verify the track is the same for both patterns
    const auto& patterns = song.patterns();
    EXPECT_EQ(patterns[0].channelTrackIds.value()[0], patterns[1].channelTrackIds.value()[0])
        << "Both patterns should reference the same track";
}

//=============================================================================
// Test: Sequential edits preserve data integrity
//=============================================================================

TEST(NspcEditorTest, SequentialEdits_DataIntegrity) {
    NspcSong song;
    NspcEditor editor;

    // Create: Duration(4), Note C, Duration(4), Note E, Duration(4), Note G
    addTrackWithEvents(song, 0,
                       {Duration{.ticks = 4}, Note{.pitch = 24}, Duration{.ticks = 4}, Note{.pitch = 28},
                        Duration{.ticks = 4}, Note{.pitch = 31}});
    addPattern(song, 0, 0, 0);

    // Edit 1: Change first note to D
    NspcEditorLocation loc1{.patternId = 0, .channel = 0, .row = 0};
    editor.setRowEvent(song, loc1, Note{.pitch = 26});

    // Edit 2: Change second note to F
    NspcEditorLocation loc2{.patternId = 0, .channel = 0, .row = 4};
    editor.setRowEvent(song, loc2, Note{.pitch = 29});

    // Edit 3: Delete third note
    NspcEditorLocation loc3{.patternId = 0, .channel = 0, .row = 8};
    editor.deleteRowEvent(song, loc3);

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // Expected compact form: Duration(4), Note D, Duration(8), Note F, End
    std::vector<uint8_t> expected = {0x04, 0x9A, 0x08, 0x9D, 0x00};

    std::cout << "\n=== SequentialEdits_DataIntegrity ===" << '\n';
    std::cout << "After:    " << bytesToHex(after) << '\n';
    std::cout << "Expected: " << bytesToHex(expected) << '\n';

    EXPECT_EQ(after, expected) << "Sequential edits should produce correct byte stream";
}

//=============================================================================
// Test: Delete row event keeps boundary when commands exist at row
//=============================================================================

TEST(NspcEditorTest, DeleteRowEvent_WithCommandBoundary_UsesTie) {
    NspcSong song;
    NspcEditor editor;

    // Row 0: Note C (4 ticks)
    // Row 4: Instrument change + Note E (4 ticks)
    addTrackWithEvents(song, 0,
                       {Duration{.ticks = 4}, Note{.pitch = 24}, Vcmd{VcmdInst{.instrumentIndex = 5}},
                        Duration{.ticks = 4}, Note{.pitch = 28}});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 4};
    const bool result = editor.deleteRowEvent(song, loc);
    EXPECT_TRUE(result);

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // Command at row 4 must keep the row boundary; deletion falls back to Tie at the row.
    std::vector<uint8_t> expected = {0x04, 0x98, 0xE0, 0x05, 0x04, 0xC8, 0x00};
    EXPECT_EQ(after, expected);
}

//=============================================================================
// Test: Add note past end of existing content
//=============================================================================

TEST(NspcEditorTest, AddNotePastEnd) {
    NspcSong song;
    NspcEditor editor;

    // Create: Duration(4), Note C — ends at tick 4
    addTrackWithEvents(song, 0, {Duration{.ticks = 4}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    // Add note at row 8 (past end of existing content)
    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 8};
    bool result = editor.setRowEvent(song, loc, Note{.pitch = 36});

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // Expected: Duration(4), Note C, Duration(4), Tie, Duration(1), Note E, End
    // Gap of 4 ticks (rows 4-7) continues the prior note instead of inserting rest.
    std::vector<uint8_t> expected = {0x04, 0x98, 0x04, 0xC8, 0x01, 0xA4, 0x00};

    std::cout << "\n=== AddNotePastEnd ===" << '\n';
    std::cout << "After:    " << bytesToHex(after) << '\n';
    std::cout << "Expected: " << bytesToHex(expected) << '\n';

    EXPECT_TRUE(result) << "setRowEvent should return true";
    EXPECT_EQ(after, expected) << "Gap should continue with tie";
}

TEST(NspcEditorTest, SetPatternLength_ExtendTracksToTargetTick) {
    NspcSong song;
    NspcEditor editor;

    addTrackWithEvents(song, 0, {Duration{.ticks = 4}, Note{.pitch = 24}});
    addTrackWithEvents(song, 1, {Duration{.ticks = 8}, Note{.pitch = 36}});
    song.patterns().push_back(NspcPattern{
        .id = 0,
        .channelTrackIds = std::array<int, 8>{0, 1, -1, -1, -1, -1, -1, -1},
        .trackTableAddr = 0x2000,
    });

    const bool result = editor.setPatternLength(song, 0, 12);
    EXPECT_TRUE(result);

    std::vector<uint8_t> ch0 = encodeTrack(song.tracks()[0]);
    std::vector<uint8_t> ch1 = encodeTrack(song.tracks()[1]);
    std::vector<uint8_t> expectedCh0 = {0x04, 0x98, 0x08, 0xC8, 0x00};
    std::vector<uint8_t> expectedCh1 = {0x08, 0xA4, 0x04, 0xC8, 0x00};

    EXPECT_EQ(ch0, expectedCh0);
    EXPECT_EQ(ch1, expectedCh1);
}

TEST(NspcEditorTest, SetPatternLength_TrimsTracksToTargetTick) {
    NspcSong song;
    NspcEditor editor;

    addTrackWithEvents(song, 0, {Duration{.ticks = 4}, Note{.pitch = 24}, Duration{.ticks = 4}, Note{.pitch = 28}});
    addTrackWithEvents(song, 1, {Duration{.ticks = 8}, Note{.pitch = 36}});
    song.patterns().push_back(NspcPattern{
        .id = 0,
        .channelTrackIds = std::array<int, 8>{0, 1, -1, -1, -1, -1, -1, -1},
        .trackTableAddr = 0x2000,
    });

    const bool result = editor.setPatternLength(song, 0, 4);
    EXPECT_TRUE(result);

    std::vector<uint8_t> ch0 = encodeTrack(song.tracks()[0]);
    std::vector<uint8_t> ch1 = encodeTrack(song.tracks()[1]);
    std::vector<uint8_t> expectedCh0 = {0x04, 0x98, 0x00};
    std::vector<uint8_t> expectedCh1 = {0x04, 0xA4, 0x00};

    EXPECT_EQ(ch0, expectedCh0);
    EXPECT_EQ(ch1, expectedCh1);
}

TEST(NspcEditorTest, SetPatternLength_CreatesAnchorTrackForEmptyPattern) {
    NspcSong song;
    NspcEditor editor;

    song.patterns().push_back(NspcPattern{
        .id = 0,
        .channelTrackIds = std::array<int, 8>{-1, -1, -1, -1, -1, -1, -1, -1},
        .trackTableAddr = 0x2000,
    });

    const bool result = editor.setPatternLength(song, 0, 6);
    EXPECT_TRUE(result);
    ASSERT_TRUE(song.patterns()[0].channelTrackIds.has_value());

    const int trackId = song.patterns()[0].channelTrackIds.value()[0];
    ASSERT_GE(trackId, 0);
    ASSERT_LT(static_cast<size_t>(trackId), song.tracks().size());

    std::vector<uint8_t> bytes = encodeTrack(song.tracks()[static_cast<size_t>(trackId)]);
    std::vector<uint8_t> expected = {0x06, 0xC8, 0x00};
    EXPECT_EQ(bytes, expected);
}

TEST(NspcEditorTest, SetPatternLength_SubroutineTrack_NoChange) {
    NspcSong song;
    NspcEditor editor;

    addSubroutineWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});
    addTrackWithSubroutineCall(song, 0, 0);
    addPattern(song, 0, 0, 0);

    const size_t beforeEventCount = song.tracks()[0].events.size();
    const bool result = editor.setPatternLength(song, 0, 4);
    EXPECT_FALSE(result);
    ASSERT_EQ(song.tracks()[0].events.size(), beforeEventCount);

    const Vcmd* first = eventAsVcmd(song.tracks()[0].events[0]);
    ASSERT_NE(first, nullptr);
    EXPECT_TRUE(std::holds_alternative<VcmdSubroutineCall>(first->vcmd));
}

TEST(NspcEditorTest, SetRowEvent_ContinuationRowsMergeIntoSingleSpan) {
    NspcSong song;
    NspcEditor editor;

    // Start from an assigned-but-empty track.
    addTrackWithEvents(song, 0, {});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation row0{.patternId = 0, .channel = 0, .row = 0};
    NspcEditorLocation row1{.patternId = 0, .channel = 0, .row = 1};
    NspcEditorLocation row2{.patternId = 0, .channel = 0, .row = 2};
    NspcEditorLocation row3{.patternId = 0, .channel = 0, .row = 3};

    EXPECT_TRUE(editor.setRowEvent(song, row0, Note{.pitch = 24}));
    EXPECT_TRUE(editor.setRowEvent(song, row1, Tie{}));
    EXPECT_TRUE(editor.setRowEvent(song, row2, Tie{}));
    EXPECT_TRUE(editor.setRowEvent(song, row3, Tie{}));

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);
    std::vector<uint8_t> expected = {0x04, 0x98, 0x00};
    EXPECT_EQ(after, expected);
}

//=============================================================================
// Test: Multi-channel pattern independence
//=============================================================================

TEST(NspcEditorTest, MultiChannel_Independence) {
    NspcSong song;
    NspcEditor editor;

    // Create two tracks for different channels
    addTrackWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});  // Channel 0
    addTrackWithEvents(song, 1, {Duration{.ticks = 8}, Note{.pitch = 36}});  // Channel 1

    // Create pattern with both channels
    auto& patterns = song.patterns();
    patterns.push_back(NspcPattern{
        .id = 0,
        .channelTrackIds = std::array<int, 8>{0, 1, -1, -1, -1, -1, -1, -1},
        .trackTableAddr = 0x2000,
    });

    // Edit channel 0
    NspcEditorLocation loc0{.patternId = 0, .channel = 0, .row = 0};
    editor.setRowEvent(song, loc0, Note{.pitch = 26});

    // Verify channel 0 changed
    std::vector<uint8_t> ch0 = encodeTrack(song.tracks()[0]);
    std::vector<uint8_t> ch0Expected = {0x08, 0x9A, 0x00};
    EXPECT_EQ(ch0, ch0Expected) << "Channel 0 should be modified";

    // Verify channel 1 unchanged
    std::vector<uint8_t> ch1 = encodeTrack(song.tracks()[1]);
    std::vector<uint8_t> ch1Expected = {0x08, 0xA4, 0x00};
    EXPECT_EQ(ch1, ch1Expected) << "Channel 1 should be unchanged";
}

//=============================================================================
// Test: addEffectAtRow appends after existing row effects
//=============================================================================

TEST(NspcEditorTest, AddEffectAtRow_AppendsAfterExistingEffects) {
    NspcSong song;
    NspcEditor editor;

    addTrackWithEvents(song, 0,
                       {Vcmd{VcmdPanFade{.time = 0x10, .target = 0x20}}, Duration{.ticks = 8}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    const bool result = editor.addEffectAtRow(song, loc, Vcmd{VcmdTempo{.tempo = 0x60}});
    EXPECT_TRUE(result);

    const auto& events = song.tracks()[0].events;
    ASSERT_GE(events.size(), 5U);

    const Vcmd* first = eventAsVcmd(events[0]);
    ASSERT_NE(first, nullptr);
    EXPECT_TRUE(std::holds_alternative<VcmdPanFade>(first->vcmd));

    const Vcmd* second = eventAsVcmd(events[1]);
    ASSERT_NE(second, nullptr);
    ASSERT_TRUE(std::holds_alternative<VcmdTempo>(second->vcmd));
    EXPECT_EQ(std::get<VcmdTempo>(second->vcmd).tempo, 0x60);

    EXPECT_TRUE(std::holds_alternative<Duration>(events[2].event));
    EXPECT_TRUE(std::holds_alternative<Note>(events[3].event));
}

//=============================================================================
// Test: addEffectAtRow creates first row effect when none exist
//=============================================================================

TEST(NspcEditorTest, AddEffectAtRow_CreatesFirstEffect) {
    NspcSong song;
    NspcEditor editor;

    addTrackWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    const bool result = editor.addEffectAtRow(song, loc, Vcmd{VcmdTempoFade{.time = 0x08, .target = 0x50}});
    EXPECT_TRUE(result);

    const auto& events = song.tracks()[0].events;
    ASSERT_GE(events.size(), 4U);

    const Vcmd* first = eventAsVcmd(events[0]);
    ASSERT_NE(first, nullptr);
    ASSERT_TRUE(std::holds_alternative<VcmdTempoFade>(first->vcmd));
    EXPECT_EQ(std::get<VcmdTempoFade>(first->vcmd).time, 0x08);
    EXPECT_EQ(std::get<VcmdTempoFade>(first->vcmd).target, 0x50);

    EXPECT_TRUE(std::holds_alternative<Duration>(events[1].event));
    EXPECT_TRUE(std::holds_alternative<Note>(events[2].event));
}

//=============================================================================
// Test: clearEffectsAtRow removes all effects at row
//=============================================================================

TEST(NspcEditorTest, ClearEffectsAtRow_RemovesAllRowEffects) {
    NspcSong song;
    NspcEditor editor;

    addTrackWithEvents(song, 0,
                       {Vcmd{VcmdPanFade{.time = 0x10, .target = 0x20}}, Vcmd{VcmdTempo{.tempo = 0x70}},
                        Duration{.ticks = 8}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    const bool result = editor.clearEffectsAtRow(song, loc);
    EXPECT_TRUE(result);

    const auto& events = song.tracks()[0].events;
    ASSERT_GE(events.size(), 3U);
    EXPECT_TRUE(std::holds_alternative<Duration>(events[0].event));
    EXPECT_TRUE(std::holds_alternative<Note>(events[1].event));
}

TEST(NspcEditorTest, ClearEffectsAtRow_PreservesSubroutineCall) {
    NspcSong song;
    NspcEditor editor;

    addTrackWithEvents(song, 0, {Vcmd{VcmdSubroutineCall{.subroutineId = 3, .originalAddr = 0x3200, .count = 1}}});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    const bool result = editor.clearEffectsAtRow(song, loc);
    EXPECT_FALSE(result) << "Should not treat subroutine call as an effect to clear";

    const auto& events = song.tracks()[0].events;
    ASSERT_GE(events.size(), 1U);
    const auto* vcmd = eventAsVcmd(events[0]);
    ASSERT_NE(vcmd, nullptr);
    EXPECT_TRUE(std::holds_alternative<VcmdSubroutineCall>(vcmd->vcmd)) << "Subroutine call should be preserved";
}

TEST(NspcEditorTest, ClearEffectsAtRow_RemoveSubroutineCallWhenRequested) {
    NspcSong song;
    NspcEditor editor;

    addTrackWithEvents(song, 0, {Vcmd{VcmdSubroutineCall{.subroutineId = 3, .originalAddr = 0x3200, .count = 1}}});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    const bool result = editor.clearEffectsAtRow(song, loc, false);
    EXPECT_TRUE(result);

    const auto& events = song.tracks()[0].events;
    ASSERT_GE(events.size(), 1U);
    EXPECT_TRUE(std::holds_alternative<End>(events[0].event));
}

TEST(NspcEditorTest, SetEffectsCommand_PreservesSubroutineCall) {
    NspcSong song;
    NspcCommandHistory history;

    addTrackWithEvents(song, 0, {Vcmd{VcmdSubroutineCall{.subroutineId = 4, .originalAddr = 0x3300, .count = 1}}});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    std::vector<Vcmd> effects;
    effects.push_back(Vcmd{VcmdPanFade{.time = 0x10, .target = 0x20}});
    auto cmd = std::make_unique<SetEffectsCommand>(loc, std::move(effects));
    const bool result = history.execute(song, std::move(cmd));
    EXPECT_TRUE(result);

    const auto& events = song.tracks()[0].events;
    bool foundPanFade = false;
    bool foundSubroutineCall = false;
    for (const auto& entry : events) {
        const auto* vcmd = eventAsVcmd(entry);
        if (vcmd == nullptr) {
            continue;
        }
        if (std::holds_alternative<VcmdPanFade>(vcmd->vcmd)) {
            const auto& panFade = std::get<VcmdPanFade>(vcmd->vcmd);
            EXPECT_EQ(panFade.time, 0x10);
            EXPECT_EQ(panFade.target, 0x20);
            foundPanFade = true;
        }
        if (std::holds_alternative<VcmdSubroutineCall>(vcmd->vcmd)) {
            foundSubroutineCall = true;
        }
    }
    EXPECT_TRUE(foundPanFade) << "Effect should be added at the row";
    EXPECT_TRUE(foundSubroutineCall) << "Subroutine call should be preserved";
}

TEST(NspcEditorTest, SetEffectsCommand_CanReplaceSubroutineCallWhenRequested) {
    NspcSong song;
    NspcCommandHistory history;

    addTrackWithEvents(song, 0, {Vcmd{VcmdSubroutineCall{.subroutineId = 4, .originalAddr = 0x3300, .count = 1}}});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    std::vector<Vcmd> effects;
    effects.push_back(Vcmd{VcmdPanFade{.time = 0x10, .target = 0x20}});
    auto cmd = std::make_unique<SetEffectsCommand>(loc, std::move(effects), false);
    const bool result = history.execute(song, std::move(cmd));
    EXPECT_TRUE(result);

    const auto& events = song.tracks()[0].events;
    bool foundPanFade = false;
    bool foundSubroutineCall = false;
    for (const auto& entry : events) {
        const auto* vcmd = eventAsVcmd(entry);
        if (vcmd == nullptr) {
            continue;
        }
        foundPanFade = foundPanFade || std::holds_alternative<VcmdPanFade>(vcmd->vcmd);
        foundSubroutineCall = foundSubroutineCall || std::holds_alternative<VcmdSubroutineCall>(vcmd->vcmd);
    }
    EXPECT_TRUE(foundPanFade);
    EXPECT_FALSE(foundSubroutineCall);
}

TEST(NspcEditorTest, CreateSubroutineFromRowRange_ExtractsTrackSlice) {
    NspcSong song;
    NspcEditor editor;

    addTrackWithEvents(song, 0, {Duration{.ticks = 4}, Note{.pitch = 24}, Duration{.ticks = 4}, Note{.pitch = 26}});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation loc{
        .patternId = 0,
        .channel = 0,
        .row = 0,
    };

    const bool changed = editor.createSubroutineFromRowRange(song, loc, 0, 4);
    EXPECT_TRUE(changed);

    ASSERT_EQ(song.subroutines().size(), 1U);
    EXPECT_EQ(song.subroutines()[0].id, 0);

    const auto& trackEvents = song.tracks()[0].events;
    ASSERT_GE(trackEvents.size(), 2U);
    const auto* callVcmd = eventAsVcmd(trackEvents[0]);
    ASSERT_NE(callVcmd, nullptr);
    ASSERT_TRUE(std::holds_alternative<VcmdSubroutineCall>(callVcmd->vcmd));
    const auto& call = std::get<VcmdSubroutineCall>(callVcmd->vcmd);
    EXPECT_EQ(call.subroutineId, 0);
    EXPECT_EQ(call.count, 1);
    EXPECT_TRUE(std::holds_alternative<End>(trackEvents[1].event));

    const auto& subEvents = song.subroutines()[0].events;
    ASSERT_GE(subEvents.size(), 5U);
    EXPECT_TRUE(std::holds_alternative<Duration>(subEvents[0].event));
    EXPECT_TRUE(std::holds_alternative<Note>(subEvents[1].event));
    EXPECT_TRUE(std::holds_alternative<Duration>(subEvents[2].event));
    EXPECT_TRUE(std::holds_alternative<Note>(subEvents[3].event));
    EXPECT_TRUE(std::holds_alternative<End>(subEvents[4].event));
}

TEST(NspcEditorTest, FlattenSubroutineOnChannel_InlinesCallsForTargetTrack) {
    NspcSong song;
    NspcEditor editor;

    addSubroutineWithEvents(song, 0, {Duration{.ticks = 2}, Note{.pitch = 30}});
    auto& tracks = song.tracks();
    tracks.resize(1);
    tracks[0] = NspcTrack{
        .id = 0,
        .events =
            {
                NspcEventEntry{.id = 1,
                               .event = Vcmd{VcmdSubroutineCall{.subroutineId = 0, .originalAddr = 0x3000, .count = 2}},
                               .originalAddr = std::nullopt},
                NspcEventEntry{.id = 2, .event = End{}, .originalAddr = std::nullopt},
            },
        .originalAddr = 0x1000,
    };
    addPattern(song, 0, 0, 0);

    const bool changed =
        editor.flattenSubroutineOnChannel(song, NspcEditorLocation{.patternId = 0, .channel = 0, .row = 0}, 0);
    EXPECT_TRUE(changed);

    const auto& events = song.tracks()[0].events;
    ASSERT_GE(events.size(), 5U);
    EXPECT_TRUE(std::holds_alternative<Duration>(events[0].event));
    EXPECT_TRUE(std::holds_alternative<Note>(events[1].event));
    EXPECT_TRUE(std::holds_alternative<Duration>(events[2].event));
    EXPECT_TRUE(std::holds_alternative<Note>(events[3].event));
    EXPECT_TRUE(std::holds_alternative<End>(events[4].event));
}

TEST(NspcEditorTest, DeleteSubroutine_FlattensTargetAndReindexesRemainingCalls) {
    NspcSong song;
    NspcEditor editor;

    addSubroutineWithEvents(song, 0, {Duration{.ticks = 2}, Note{.pitch = 24}});
    addSubroutineWithEvents(song, 1, {Duration{.ticks = 2}, Note{.pitch = 26}});

    auto& tracks = song.tracks();
    tracks.resize(2);
    tracks[0] = NspcTrack{
        .id = 0,
        .events =
            {
                NspcEventEntry{.id = 1,
                               .event = Vcmd{VcmdSubroutineCall{.subroutineId = 0, .originalAddr = 0x3000, .count = 1}},
                               .originalAddr = std::nullopt},
                NspcEventEntry{.id = 2, .event = End{}, .originalAddr = std::nullopt},
            },
        .originalAddr = 0x1000,
    };
    tracks[1] = NspcTrack{
        .id = 1,
        .events =
            {
                NspcEventEntry{.id = 3,
                               .event = Vcmd{VcmdSubroutineCall{.subroutineId = 1, .originalAddr = 0x3100, .count = 1}},
                               .originalAddr = std::nullopt},
                NspcEventEntry{.id = 4, .event = End{}, .originalAddr = std::nullopt},
            },
        .originalAddr = 0x1100,
    };

    const bool changed = editor.deleteSubroutine(song, 0);
    EXPECT_TRUE(changed);

    ASSERT_EQ(song.subroutines().size(), 1U);
    EXPECT_EQ(song.subroutines()[0].id, 0);

    const auto& track0Events = song.tracks()[0].events;
    ASSERT_GE(track0Events.size(), 3U);
    EXPECT_TRUE(std::holds_alternative<Duration>(track0Events[0].event));
    EXPECT_TRUE(std::holds_alternative<Note>(track0Events[1].event));
    EXPECT_TRUE(std::holds_alternative<End>(track0Events[2].event));

    const auto& track1Events = song.tracks()[1].events;
    ASSERT_GE(track1Events.size(), 2U);
    const auto* callVcmd = eventAsVcmd(track1Events[0]);
    ASSERT_NE(callVcmd, nullptr);
    ASSERT_TRUE(std::holds_alternative<VcmdSubroutineCall>(callVcmd->vcmd));
    const auto& remappedCall = std::get<VcmdSubroutineCall>(callVcmd->vcmd);
    EXPECT_EQ(remappedCall.subroutineId, 0);
}

//=============================================================================
// Test: Set QV at row
//=============================================================================

TEST(NspcEditorTest, SetQvAtRow) {
    NspcSong song;
    NspcEditor editor;

    addTrackWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    const bool result = editor.setQvAtRow(song, loc, static_cast<uint8_t>(0x4C));
    EXPECT_TRUE(result);

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);
    std::vector<uint8_t> expected = {0x08, 0x4C, 0x98, 0x00};
    EXPECT_EQ(after, expected);
}

TEST(NspcEditorTest, SetQvAtRowInsertsDurationAtLaterSpan) {
    NspcSong song;
    NspcEditor editor;

    // Shared duration across two note rows.
    addTrackWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}, Note{.pitch = 26}});
    addPattern(song, 0, 0, 0);

    // Row 8 is the second note start.
    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 8};
    const bool result = editor.setQvAtRow(song, loc, static_cast<uint8_t>(0x2F));
    EXPECT_TRUE(result);

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);
    std::vector<uint8_t> expected = {0x08, 0x98, 0x08, 0x2F, 0x9A, 0x00};
    EXPECT_EQ(after, expected);
}

TEST(NspcEditorTest, SetQvAtRowMidSpanSplitsAtSelectedRow) {
    NspcSong song;
    NspcEditor editor;

    addTrackWithEvents(song, 0, {Duration{.ticks = 8}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    // Row 3 is an implicit continuation row of the first note span.
    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 3};
    const bool result = editor.setQvAtRow(song, loc, static_cast<uint8_t>(0x2F));
    EXPECT_TRUE(result);

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);
    std::vector<uint8_t> expected = {0x03, 0x98, 0x05, 0x2F, 0xC8, 0x00};
    EXPECT_EQ(after, expected);
}

//=============================================================================
// Test: Edit with quantization and velocity preserved
//=============================================================================

TEST(NspcEditorTest, PreserveQuantizationVelocity) {
    NspcSong song;
    NspcEditor editor;

    // Create: Duration(8, q=4, v=12), Note C
    addTrackWithEvents(song, 0, {Duration{.ticks = 8, .quantization = 4, .velocity = 12}, Note{.pitch = 24}});
    addPattern(song, 0, 0, 0);

    // Edit note
    NspcEditorLocation loc{.patternId = 0, .channel = 0, .row = 0};
    editor.setRowEvent(song, loc, Note{.pitch = 26});

    std::vector<uint8_t> after = encodeTrack(song.tracks()[0]);

    // Expected: Duration(8) + QV byte (0x4C), Note D, End
    std::vector<uint8_t> expected = {0x08, 0x4C, 0x9A, 0x00};

    std::cout << "\n=== PreserveQuantizationVelocity ===" << '\n';
    std::cout << "After:    " << bytesToHex(after) << '\n';
    std::cout << "Expected: " << bytesToHex(expected) << '\n';

    EXPECT_EQ(after, expected) << "Quantization/velocity should be preserved";
}

}  // namespace
}  // namespace ntrak::nspc
