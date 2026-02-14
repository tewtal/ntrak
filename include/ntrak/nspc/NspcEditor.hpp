#pragma once

#include "ntrak/nspc/NspcData.hpp"

#include <cstdint>
#include <optional>
#include <variant>

namespace ntrak::nspc {

struct NspcEditorLocation {
    int patternId = -1;
    int channel = 0;
    uint32_t row = 0;
};

using NspcRowEvent = std::variant<Note, Tie, Rest, Percussion>;

class NspcEditor final {
public:
    bool setPatternLength(NspcSong& song, int patternId, uint32_t targetTick);

    bool insertTickAtRow(NspcSong& song, const NspcEditorLocation& location);
    bool removeTickAtRow(NspcSong& song, const NspcEditorLocation& location);

    bool setRowEvent(NspcSong& song, const NspcEditorLocation& location, const NspcRowEvent& event);
    bool deleteRowEvent(NspcSong& song, const NspcEditorLocation& location);

    bool setInstrumentAtRow(NspcSong& song, const NspcEditorLocation& location, std::optional<uint8_t> instrument);
    bool setVolumeAtRow(NspcSong& song, const NspcEditorLocation& location, std::optional<uint8_t> volume);
    bool setQvAtRow(NspcSong& song, const NspcEditorLocation& location, std::optional<uint8_t> qv);

    bool setEffectAtRow(NspcSong& song, const NspcEditorLocation& location, const Vcmd& effect);
    bool addEffectAtRow(NspcSong& song, const NspcEditorLocation& location, const Vcmd& effect);
    bool clearEffectsAtRow(NspcSong& song, const NspcEditorLocation& location,
                           bool preserveSubroutineCalls = true);

    bool createSubroutineFromRowRange(NspcSong& song, const NspcEditorLocation& location, uint32_t startRow,
                                      uint32_t endRow);
    bool flattenSubroutineOnChannel(NspcSong& song, const NspcEditorLocation& location, int subroutineId);
    bool deleteSubroutine(NspcSong& song, int subroutineId);
};

}  // namespace ntrak::nspc
