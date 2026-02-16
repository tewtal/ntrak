#pragma once

#include "ntrak/nspc/NspcProject.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ntrak::nspc {

// --- Instrument source for each MIDI channel mapping ---

struct MidiInstrumentSource {
    enum class Kind : uint8_t {
        MapToExisting,  // Map to an existing instrument in the target project
        CreateBlank,    // Create a new dummy instrument (default ADSR, silent sample)
        FromBrrFile,    // Create a new instrument from a .brr file
        FromNtiFile,    // Create a new instrument from a .nti file
    };

    Kind kind = Kind::CreateBlank;
    int existingInstrumentId = -1;                    // For MapToExisting
    std::optional<std::filesystem::path> assetPath;   // For FromBrrFile or FromNtiFile
};

// --- Per-channel mapping configuration ---

struct MidiChannelMapping {
    int midiChannel = -1;              // 0-15
    int midiProgram = -1;              // GM program number detected, -1 if none
    std::string channelLabel;          // e.g. "Ch 1 (Piano)" or "Ch 10 (Drums)"
    bool enabled = true;               // Whether to import this channel
    int targetNspcChannel = -1;        // Which SNES channel (0-7) to assign, -1 = auto
    MidiInstrumentSource instrumentSource{};
    int8_t transposeOctaves = 0;       // Manual octave offset for this channel
};

// --- Import options ---

struct MidiImportOptions {
    std::vector<MidiChannelMapping> channelMappings;
    bool convertVelocityToVolume = true;   // Map MIDI velocity to VcmdVolume
    bool convertPanCc = true;              // Map CC#10 to VcmdPanning
    std::vector<int> instrumentsToDelete;  // Target instrument IDs to remove before importing
    std::vector<int> samplesToDelete;       // Target sample IDs to remove before importing
};

// --- Preview data per channel ---

struct MidiChannelPreview {
    int midiChannel = -1;
    int midiProgram = -1;
    std::string label;
    int noteCount = 0;
    int minNote = 127;
    int maxNote = 0;
    bool hasVelocityChanges = false;
    bool hasPanChanges = false;
};

// --- Preview result ---

struct MidiImportPreview {
    std::string fileName;
    int midiFormat = 0;
    int ppq = 480;
    int totalTracks = 0;
    int activeChannelCount = 0;
    int selectedChannelCount = 0;
    int estimatedPatternCount = 0;
    int estimatedTrackCount = 0;
    int estimatedNewInstrumentCount = 0;
    int estimatedNewSampleCount = 0;
    uint32_t currentFreeAramBytes = 0;
    uint32_t freeAramAfterDeletionBytes = 0;
    uint32_t estimatedRequiredSampleBytes = 0;
    std::vector<MidiChannelPreview> channels;
    std::vector<std::string> warnings;
};

// --- Import result ---

struct MidiImportResult {
    int targetSongIndex = -1;
    int importedPatternCount = 0;
    int importedTrackCount = 0;
    int importedInstrumentCount = 0;
    int importedSampleCount = 0;
    std::vector<std::string> warnings;
};

// --- Public API ---

/// Analyze a MIDI file and produce a preview with ARAM estimates and channel info.
std::expected<MidiImportPreview, std::string>
analyzeMidiFileForSongSlot(const NspcProject& baseProject, const std::filesystem::path& midiPath,
                           int targetSongIndex, const MidiImportOptions& options = {});

/// Execute the full import: parse MIDI, convert, and port into the target song slot.
std::expected<std::pair<NspcProject, MidiImportResult>, std::string>
importMidiFileIntoSongSlot(const NspcProject& baseProject, const std::filesystem::path& midiPath,
                           int targetSongIndex, const MidiImportOptions& options = {});

/// Scan a MIDI file and build default channel mappings (first 8 active channels enabled).
std::expected<std::vector<MidiChannelMapping>, std::string>
buildDefaultMidiChannelMappings(const std::filesystem::path& midiPath);

}  // namespace ntrak::nspc
