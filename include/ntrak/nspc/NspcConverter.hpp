#pragma once

#include "ntrak/nspc/NspcProject.hpp"

#include <map>
#include <string>
#include <vector>

namespace ntrak::nspc {

/// Describes how a single instrument from the source should be handled in the target.
struct InstrumentMapping {
    int sourceInstrumentId = -1;  // VcmdInst.instrumentIndex value in the source song

    enum class Action : uint8_t {
        Copy,          // Copy the instrument (and its sample) into the target
        MapToExisting, // Map to an already-existing instrument in the target
    };

    Action action = Action::Copy;
    int targetInstrumentId = -1;  // For MapToExisting: target instrument id to use

    /// How to handle the source instrument's sample (only relevant when action == Copy).
    enum class SampleAction : uint8_t {
        CopyNew,         // Allocate a new sample slot and ARAM space
        UseExisting,     // Point instrument to an existing target sample (no data change)
        ReplaceExisting, // Overwrite an existing target sample's data with the source data
    };

    SampleAction sampleAction = SampleAction::CopyNew;
    int targetSampleId = -1;  // For UseExisting or ReplaceExisting: which target sample to use
};

struct SongPortRequest {
    int sourceSongIndex;   // Index in source.songs()
    int targetSongIndex;   // -1 = append as new song, else overwrite that index
    std::vector<InstrumentMapping> instrumentMappings;
    std::vector<int> instrumentsToDelete;  // Target instrument IDs to remove before porting
    std::vector<int> samplesToDelete;  // Target sample IDs to remove before porting (frees ARAM)
};

struct SongPortResult {
    bool success = false;
    std::string error;
    int resultSongIndex = -1;
    std::map<int, int> instrumentRemap;  // source instrument id -> target instrument id
};

/// Returns sorted unique instrument IDs (VcmdInst.instrumentIndex values) used in the song.
std::vector<int> findUsedInstrumentIds(const NspcProject& project, int songIndex);

/// Build default mappings: Copy all used instruments, appended after target's existing max id.
std::vector<InstrumentMapping> buildDefaultMappings(const NspcProject& source, const NspcProject& target,
                                                    int sourceSongIndex);

/// Execute port: copies song from source into target with instrument remapping.
/// Adds new instruments/samples to target as needed.
SongPortResult portSong(const NspcProject& source, NspcProject& target, const SongPortRequest& request);

}  // namespace ntrak::nspc
