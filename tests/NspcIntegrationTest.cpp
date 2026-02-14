#include "ntrak/nspc/NspcCompile.hpp"
#include "ntrak/nspc/NspcData.hpp"
#include "ntrak/nspc/NspcEditor.hpp"
#include "ntrak/nspc/NspcEngine.hpp"
#include "ntrak/nspc/NspcFlatten.hpp"
#include "ntrak/nspc/NspcParser.hpp"
#include "ntrak/nspc/NspcProject.hpp"

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <vector>

namespace ntrak::nspc {
namespace {

// Helper to load an SPC file as raw bytes
std::optional<std::vector<uint8_t>> loadSpcFileRaw(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open SPC file: " << path << '\n';
        return std::nullopt;
    }

    std::vector<uint8_t> buffer(std::istreambuf_iterator<char>(file), {});
    return buffer;
}

// Helper to get the test fixtures directory
std::filesystem::path getFixturesPath() {
    // Look for fixtures relative to the test executable or source directory
    std::vector<std::filesystem::path> candidates = {
        "fixtures",
        "../fixtures",
        "../../tests/fixtures",
        "../../../tests/fixtures",
    };

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::canonical(candidate);
        }
    }

    // Fall back to source directory path
    return std::filesystem::path(__FILE__).parent_path() / "fixtures";
}

// Helper to print bytes as hex
std::string bytesToHex(const std::vector<uint8_t>& bytes, size_t maxBytes = 32) {
    std::string result;
    for (size_t i = 0; i < std::min(bytes.size(), maxBytes); ++i) {
        if (i > 0) {
            result += " ";
        }
        result += std::format("{:02X}", bytes[i]);
    }
    if (bytes.size() > maxBytes) {
        result += std::format(" ... ({} more bytes)", bytes.size() - maxBytes);
    }
    return result;
}

//=============================================================================
// Test Fixture for SPC file tests
//=============================================================================

class NspcIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override { fixturesPath_ = getFixturesPath(); }

    std::filesystem::path fixturesPath_;
};

//=============================================================================
// Test: Load SPC file, edit a note, compile, verify round-trip
//=============================================================================

TEST_F(NspcIntegrationTest, DISABLED_EditNoteAndCompile) {
    // This test is disabled by default because it requires a test SPC file
    // To enable: place an SPC file in tests/fixtures/ and update the filename

    auto spcPath = fixturesPath_ / "test.spc";
    if (!std::filesystem::exists(spcPath)) {
        GTEST_SKIP() << "Test SPC file not found: " << spcPath;
    }

    // Load SPC file as raw bytes
    auto spcData = loadSpcFileRaw(spcPath);
    ASSERT_TRUE(spcData.has_value()) << "Failed to load SPC file";

    // Parse using NspcParser which handles engine detection automatically
    NspcParser parser;
    auto projectResult = parser.load(std::span<const uint8_t>(*spcData));
    ASSERT_TRUE(projectResult.has_value()) << "Failed to parse SPC file: " << static_cast<int>(projectResult.error());

    auto& project = *projectResult;

    // Verify we have at least one song
    ASSERT_GT(project.songs().size(), 0) << "No songs found in SPC";

    // Get the first song
    auto& song = project.songs()[0];

    // Verify we have patterns
    ASSERT_GT(song.patterns().size(), 0) << "No patterns in song";

    std::cout << "\n=== SPC File Info ===" << '\n';
    std::cout << "Engine: " << project.engineConfig().name << '\n';
    std::cout << "Patterns: " << song.patterns().size() << '\n';
    std::cout << "Tracks: " << song.tracks().size() << '\n';
    std::cout << "Subroutines: " << song.subroutines().size() << '\n';

    // TODO: Add specific edit operations and verification
}

//=============================================================================
// Test: Full round-trip - parse, edit, compile, verify ARAM
//=============================================================================

TEST_F(NspcIntegrationTest, DISABLED_FullRoundTrip) {
    auto spcPath = fixturesPath_ / "test.spc";
    if (!std::filesystem::exists(spcPath)) {
        GTEST_SKIP() << "Test SPC file not found: " << spcPath;
    }

    auto spcData = loadSpcFileRaw(spcPath);
    ASSERT_TRUE(spcData.has_value()) << "Failed to load SPC file";

    NspcParser parser;
    auto projectResult = parser.load(std::span<const uint8_t>(*spcData));
    ASSERT_TRUE(projectResult.has_value()) << "Failed to parse SPC file: " << static_cast<int>(projectResult.error());

    auto& project = *projectResult;

    ASSERT_GT(project.songs().size(), 0) << "No songs found";

    std::cout << "\n=== Engine: " << project.engineConfig().name << " ===" << '\n';

    // Verify round-trip before any edits
    auto reportResult = verifySongRoundTrip(project, 0);
    if (reportResult.has_value()) {
        const auto& report = *reportResult;
        std::cout << "\n=== Round-Trip Report (Before Edits) ===" << '\n';
        std::cout << "Equivalent: " << (report.equivalent ? "YES" : "NO") << '\n';
        std::cout << "Objects compared: " << report.objectsCompared << '\n';
        std::cout << "Bytes compared: " << report.bytesCompared << '\n';
        std::cout << "Differing bytes: " << report.differingBytes << '\n';

        for (const auto& msg : report.messages) {
            std::cout << "  " << msg << '\n';
        }

        // The round-trip should be equivalent before edits
        EXPECT_TRUE(report.equivalent) << "Round-trip should be equivalent before edits";
    } else {
        FAIL() << "verifySongRoundTrip failed: " << reportResult.error();
    }
}

//=============================================================================
// Test: Edit instrument on channel 6 - verify only one byte changes
// BUG REPRO: Changing instrument from 0x02 to 0x0B on first note of channel 6
// causes playback to stop.
//=============================================================================

TEST_F(NspcIntegrationTest, DISABLED_Channel6InstrumentEdit) {
    auto spcPath = fixturesPath_ / "test.spc";
    if (!std::filesystem::exists(spcPath)) {
        GTEST_SKIP() << "Test SPC file not found: " << spcPath;
    }

    auto spcData = loadSpcFileRaw(spcPath);
    ASSERT_TRUE(spcData.has_value()) << "Failed to load SPC file";

    NspcParser parser;
    auto projectResult = parser.load(std::span<const uint8_t>(*spcData));
    ASSERT_TRUE(projectResult.has_value()) << "Failed to parse SPC file: " << static_cast<int>(projectResult.error());

    auto& project = *projectResult;
    ASSERT_GT(project.songs().size(), 0) << "No songs found";

    auto& song = project.songs()[0];
    ASSERT_GT(song.patterns().size(), 0) << "No patterns found";

    // Channel 6 is index 5 (0-indexed), pattern ID should match the first pattern's ID
    // NOTE: The instrument command is at row 0 (with the Rest), but the actual note
    // the user edits is at row 5. At row 5 there's NO existing instrument command,
    // so setInstrumentAtRow will INSERT a new E0 XX command (2 bytes).
    constexpr int kChannel = 5;
    constexpr uint32_t kRow = 5;  // Row where the note is (not row 0 where the Rest is)
    constexpr uint8_t kOldInstrument = 0x02;
    constexpr uint8_t kNewInstrument = 0x0B;

    // Get the first pattern's ID
    const auto& pattern = song.patterns()[0];
    const int patternId = pattern.id;

    std::cout << "\n=== Channel 6 Instrument Edit Test ===" << '\n';
    std::cout << "Engine: " << project.engineConfig().name << '\n';
    std::cout << "Pattern ID: " << patternId << ", Channel: " << kChannel << ", Row: " << kRow << '\n';
    std::cout << "Instrument change: " << std::format("0x{:02X}", kOldInstrument) << " -> "
              << std::format("0x{:02X}", kNewInstrument) << '\n';

    // Build upload before edit to get baseline byte count
    auto uploadBeforeResult = buildSongScopedUpload(project, 0);
    ASSERT_TRUE(uploadBeforeResult.has_value()) << "Failed to build upload before edit: " << uploadBeforeResult.error();

    const auto& uploadBefore = uploadBeforeResult->upload;

    // Create editor location
    NspcEditorLocation location{
        .patternId = patternId,
        .channel = kChannel,
        .row = kRow,
    };

    // Flatten the pattern to see what events exist at the edit location
    auto flatPattern = flattenPatternById(song, patternId);
    if (flatPattern.has_value()) {
        const auto& flatChannel = flatPattern->channels[static_cast<size_t>(kChannel)];
        std::cout << "\n=== Events at Row " << kRow << " Channel " << kChannel << " ===" << '\n';
        std::cout << "Track ID: " << flatChannel.trackId << '\n';
        bool hasInstrument = false;
        for (const auto& evt : flatChannel.events) {
            if (evt.tick == kRow) {
                std::cout << "  tick=" << evt.tick;
                // Check event type
                if (std::holds_alternative<Vcmd>(evt.event)) {
                    const auto& vcmd = std::get<Vcmd>(evt.event);
                    if (const auto* inst = std::get_if<VcmdInst>(&vcmd.vcmd)) {
                        std::cout << " VcmdInst{instrument=" << std::format("0x{:02X}", inst->instrumentIndex) << "}";
                        hasInstrument = true;
                    } else {
                        std::cout << " Vcmd{other}";
                    }
                } else if (std::holds_alternative<Note>(evt.event)) {
                    const auto& note = std::get<Note>(evt.event);
                    std::cout << " Note{pitch=" << std::format("0x{:02X}", note.pitch) << "}";
                } else if (std::holds_alternative<Rest>(evt.event)) {
                    std::cout << " Rest{}";
                } else if (std::holds_alternative<Tie>(evt.event)) {
                    std::cout << " Tie{}";
                } else {
                    std::cout << " OtherEvent";
                }
                std::cout << '\n';
            }
        }
        if (!hasInstrument) {
            std::cout << "  *** NO INSTRUMENT COMMAND at this location ***" << '\n';
        }
    }

    // Perform the instrument edit
    std::cout << "\nPerforming setInstrumentAtRow edit..." << '\n';
    NspcEditor editor;
    bool editResult = editor.setInstrumentAtRow(song, location, kNewInstrument);
    EXPECT_TRUE(editResult) << "setInstrumentAtRow returned false";

    // Build upload after edit
    auto uploadAfterResult = buildSongScopedUpload(project, 0);
    ASSERT_TRUE(uploadAfterResult.has_value()) << "Failed to build upload after edit: " << uploadAfterResult.error();

    const auto& uploadAfter = uploadAfterResult->upload;

    std::cout << "\n=== Upload Comparison ===" << '\n';
    std::cout << "Chunks before: " << uploadBefore.chunks.size() << '\n';
    std::cout << "Chunks after: " << uploadAfter.chunks.size() << '\n';

    // Show chunk details to identify relocation
    std::cout << "\n=== Chunk Addresses (Before) ===" << '\n';
    for (const auto& chunk : uploadBefore.chunks) {
        std::cout << std::format("  ${:04X} ({:3d} bytes): {}\n", chunk.address, chunk.bytes.size(), chunk.label);
    }
    std::cout << "\n=== Chunk Addresses (After) ===" << '\n';
    for (const auto& chunk : uploadAfter.chunks) {
        std::cout << std::format("  ${:04X} ({:3d} bytes): {}\n", chunk.address, chunk.bytes.size(), chunk.label);
    }

    // Count total bytes
    size_t totalBytesBefore = 0;
    size_t totalBytesAfter = 0;
    for (const auto& chunk : uploadBefore.chunks) {
        totalBytesBefore += chunk.bytes.size();
    }
    for (const auto& chunk : uploadAfter.chunks) {
        totalBytesAfter += chunk.bytes.size();
    }
    std::cout << "\nTotal bytes before: " << totalBytesBefore << '\n';
    std::cout << "Total bytes after: " << totalBytesAfter << '\n';

    // Compare byte-by-byte to find actual changes
    // Build ARAM image from each upload
    std::array<uint8_t, 0x10000> aramBefore{};
    std::array<uint8_t, 0x10000> aramAfter{};

    for (const auto& chunk : uploadBefore.chunks) {
        for (size_t i = 0; i < chunk.bytes.size(); ++i) {
            if (chunk.address + i < aramBefore.size()) {
                aramBefore[chunk.address + i] = chunk.bytes[i];
            }
        }
    }
    for (const auto& chunk : uploadAfter.chunks) {
        for (size_t i = 0; i < chunk.bytes.size(); ++i) {
            if (chunk.address + i < aramAfter.size()) {
                aramAfter[chunk.address + i] = chunk.bytes[i];
            }
        }
    }

    // Count differing bytes
    size_t differingBytes = 0;
    std::vector<std::pair<uint16_t, std::pair<uint8_t, uint8_t>>> diffs;
    for (size_t i = 0; i < aramBefore.size(); ++i) {
        if (aramBefore[i] != aramAfter[i]) {
            differingBytes++;
            if (diffs.size() < 20) {  // Limit output
                diffs.emplace_back(static_cast<uint16_t>(i), std::make_pair(aramBefore[i], aramAfter[i]));
            }
        }
    }

    std::cout << "\n=== ARAM Differences ===" << '\n';
    std::cout << "Differing bytes: " << differingBytes << '\n';
    for (const auto& [addr, values] : diffs) {
        std::cout << std::format("  0x{:04X}: {:02X} -> {:02X}\n", addr, values.first, values.second);
    }

    // For INSERT operations (adding instrument where none existed), we expect:
    // 1. Track 05 grows by 2 bytes (totalBytesAfter == totalBytesBefore + 2)
    // 2. Track 06 may relocate since it was adjacent to Track 05
    // 3. Pattern table pointers update to reflect relocation
    // NOTE: This is different from REPLACE operations which only change 1-2 bytes
    EXPECT_EQ(totalBytesAfter, totalBytesBefore + 2) << "Instrument INSERT should add exactly 2 bytes (E0 + value)";

    // Verify no track was allocated into the echo buffer (address < 0x0600 is typically unsafe)
    // The exact safe area depends on engine config but 0x0500 was the echo buffer in our test
    for (const auto& chunk : uploadAfter.chunks) {
        if (chunk.label.find("Track") != std::string::npos) {
            EXPECT_GE(chunk.address, 0x1000)
                << "Track " << chunk.label << " at $" << std::format("{:04X}", chunk.address)
                << " may be in echo buffer or low memory region";
        }
    }

    // Verify compile succeeded and round-trip is valid
    // Note: Round-trip will show diffs because data shifted, but compile should work
    auto reportResult = verifySongRoundTrip(project, 0);
    ASSERT_TRUE(reportResult.has_value()) << "Round-trip verification should succeed";

    const auto& report = *reportResult;
    std::cout << "\n=== Round-Trip After Edit ===" << '\n';
    std::cout << "Equivalent: " << (report.equivalent ? "YES" : "NO") << '\n';
    std::cout << "Objects compared: " << report.objectsCompared << '\n';
    std::cout << "Differing bytes: " << report.differingBytes << '\n';
    for (const auto& msg : report.messages) {
        std::cout << "  " << msg << '\n';
    }

    // The round-trip shows diffs due to the insertion shifting data,
    // but the key assertion is that compile + verify didn't crash
    // and tracks are in safe memory locations
}

//=============================================================================
// Parameterized test for multiple SPC files
//=============================================================================

class NspcSpcFileTest : public ::testing::TestWithParam<std::string> {
protected:
    void SetUp() override { fixturesPath_ = getFixturesPath(); }

    std::filesystem::path fixturesPath_;
};

TEST_P(NspcSpcFileTest, DISABLED_RoundTripEquivalent) {
    auto spcPath = fixturesPath_ / GetParam();
    if (!std::filesystem::exists(spcPath)) {
        GTEST_SKIP() << "SPC file not found: " << spcPath;
    }

    auto spcData = loadSpcFileRaw(spcPath);
    ASSERT_TRUE(spcData.has_value()) << "Failed to load SPC file";

    NspcParser parser;
    auto projectResult = parser.load(std::span<const uint8_t>(*spcData));
    ASSERT_TRUE(projectResult.has_value()) << "Failed to parse SPC file: " << static_cast<int>(projectResult.error());

    auto& project = *projectResult;

    if (project.songs().empty()) {
        GTEST_SKIP() << "No songs found in SPC";
    }

    auto reportResult = verifySongRoundTrip(project, 0);
    ASSERT_TRUE(reportResult.has_value()) << "verifySongRoundTrip failed: " << reportResult.error();

    const auto& report = *reportResult;
    std::cout << "\n=== " << GetParam() << " ===" << '\n';
    std::cout << "Round-trip equivalent: " << (report.equivalent ? "YES" : "NO") << '\n';
    std::cout << "Differing bytes: " << report.differingBytes << '\n';

    EXPECT_TRUE(report.equivalent) << "Round-trip should be equivalent";
}

// Allow this test suite to exist without instantiation (users provide their own SPC files)
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(NspcSpcFileTest);

// Uncomment and add your test SPC files here:
// INSTANTIATE_TEST_SUITE_P(SpcFiles, NspcSpcFileTest, ::testing::Values("test.spc", "another.spc"));

}  // namespace
}  // namespace ntrak::nspc
