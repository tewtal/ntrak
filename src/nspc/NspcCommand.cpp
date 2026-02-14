#include "ntrak/nspc/NspcCommand.hpp"

#include <algorithm>
#include <format>

namespace ntrak::nspc {

// ============================================================================
// NspcCommandGroup
// ============================================================================

NspcCommandGroup::NspcCommandGroup(std::string description) : description_(std::move(description)) {}

void NspcCommandGroup::addCommand(std::unique_ptr<NspcCommand> command) {
    commands_.push_back(std::move(command));
}

bool NspcCommandGroup::execute(NspcSong& song) {
    bool anyExecuted = false;
    for (auto& cmd : commands_) {
        if (cmd->execute(song)) {
            anyExecuted = true;
        }
    }
    return anyExecuted;
}

bool NspcCommandGroup::undo(NspcSong& song) {
    // Undo in reverse order
    bool anyUndone = false;
    for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
        if ((*it)->undo(song)) {
            anyUndone = true;
        }
    }
    return anyUndone;
}

// ============================================================================
// NspcCellCommand - State Capture/Restore
// ============================================================================

void NspcCellCommand::captureState(NspcSong& song, CellState& state) {
    state.contentOrigin = song.contentOrigin();

    // Capture full tracks vector (handles track creation/removal on undo)
    state.allTracks = song.tracks();

    // Find the pattern and capture channel track mapping
    auto patternIt = std::find_if(song.patterns().begin(), song.patterns().end(),
                                   [this](const NspcPattern& p) { return p.id == location_.patternId; });

    if (patternIt != song.patterns().end()) {
        state.patternChannelTrackIds = patternIt->channelTrackIds;
    }

    // Capture subroutine state (edits on subroutine rows modify subroutine events directly)
    state.subroutineSnapshots.clear();
    for (const auto& sub : song.subroutines()) {
        state.subroutineSnapshots.push_back(CellState::SubroutineSnapshot{
            .id = sub.id,
            .events = sub.events,
        });
    }
}

void NspcCellCommand::restoreState(NspcSong& song, const CellState& state) {
    song.setContentOrigin(state.contentOrigin);

    // Restore full tracks vector (cleans up any tracks created during execute)
    song.tracks() = state.allTracks;

    // Find the pattern and restore channel track mapping
    auto patternIt = std::find_if(song.patterns().begin(), song.patterns().end(),
                                   [this](NspcPattern& p) { return p.id == location_.patternId; });

    if (patternIt != song.patterns().end()) {
        patternIt->channelTrackIds = state.patternChannelTrackIds;
    }

    // Restore subroutine state
    for (const auto& snapshot : state.subroutineSnapshots) {
        auto subIt = std::find_if(song.subroutines().begin(), song.subroutines().end(),
                                   [&](const NspcSubroutine& s) { return s.id == snapshot.id; });
        if (subIt != song.subroutines().end()) {
            subIt->events = snapshot.events;
        }
    }
}

// ============================================================================
// SetRowEventCommand
// ============================================================================

SetRowEventCommand::SetRowEventCommand(const NspcEditorLocation& location, const NspcRowEvent& event)
    : event_(event) {
    location_ = location;
}

bool SetRowEventCommand::execute(NspcSong& song) {
    if (!capturedBefore_) {
        captureState(song, beforeState_);
        capturedBefore_ = true;
    }

    NspcEditor editor;
    bool result = editor.setRowEvent(song, location_, event_);

    if (result) {
        captureState(song, afterState_);
    }

    return result;
}

bool SetRowEventCommand::undo(NspcSong& song) {
    restoreState(song, beforeState_);
    return true;
}

std::string SetRowEventCommand::description() const {
    if (std::holds_alternative<Note>(event_)) {
        return "Set Note";
    }
    if (std::holds_alternative<Tie>(event_)) {
        return "Set Tie";
    }
    if (std::holds_alternative<Rest>(event_)) {
        return "Set Rest";
    }
    if (std::holds_alternative<Percussion>(event_)) {
        return "Set Percussion";
    }
    return "Set Row Event";
}

// ============================================================================
// DeleteRowEventCommand
// ============================================================================

DeleteRowEventCommand::DeleteRowEventCommand(const NspcEditorLocation& location) {
    location_ = location;
}

bool DeleteRowEventCommand::execute(NspcSong& song) {
    if (!capturedBefore_) {
        captureState(song, beforeState_);
        capturedBefore_ = true;
    }

    NspcEditor editor;
    bool result = editor.deleteRowEvent(song, location_);

    if (result) {
        captureState(song, afterState_);
    }

    return result;
}

bool DeleteRowEventCommand::undo(NspcSong& song) {
    restoreState(song, beforeState_);
    return true;
}

std::string DeleteRowEventCommand::description() const {
    return "Delete Row Event";
}

// ============================================================================
// InsertTickCommand
// ============================================================================

InsertTickCommand::InsertTickCommand(const NspcEditorLocation& location) {
    location_ = location;
}

bool InsertTickCommand::execute(NspcSong& song) {
    if (!capturedBefore_) {
        captureState(song, beforeState_);
        capturedBefore_ = true;
    }

    NspcEditor editor;
    bool result = editor.insertTickAtRow(song, location_);

    if (result) {
        captureState(song, afterState_);
    }

    return result;
}

bool InsertTickCommand::undo(NspcSong& song) {
    restoreState(song, beforeState_);
    return true;
}

std::string InsertTickCommand::description() const {
    return "Insert Tick";
}

// ============================================================================
// RemoveTickCommand
// ============================================================================

RemoveTickCommand::RemoveTickCommand(const NspcEditorLocation& location) {
    location_ = location;
}

bool RemoveTickCommand::execute(NspcSong& song) {
    if (!capturedBefore_) {
        captureState(song, beforeState_);
        capturedBefore_ = true;
    }

    NspcEditor editor;
    bool result = editor.removeTickAtRow(song, location_);

    if (result) {
        captureState(song, afterState_);
    }

    return result;
}

bool RemoveTickCommand::undo(NspcSong& song) {
    restoreState(song, beforeState_);
    return true;
}

std::string RemoveTickCommand::description() const {
    return "Remove Tick";
}

// ============================================================================
// SetInstrumentCommand
// ============================================================================

SetInstrumentCommand::SetInstrumentCommand(const NspcEditorLocation& location, std::optional<uint8_t> instrument)
    : instrument_(instrument) {
    location_ = location;
}

bool SetInstrumentCommand::execute(NspcSong& song) {
    if (!capturedBefore_) {
        captureState(song, beforeState_);
        capturedBefore_ = true;
    }

    NspcEditor editor;
    bool result = editor.setInstrumentAtRow(song, location_, instrument_);

    if (result) {
        captureState(song, afterState_);
    }

    return result;
}

bool SetInstrumentCommand::undo(NspcSong& song) {
    restoreState(song, beforeState_);
    return true;
}

std::string SetInstrumentCommand::description() const {
    if (instrument_.has_value()) {
        return std::format("Set Instrument {:02X}", *instrument_);
    }
    return "Clear Instrument";
}

// ============================================================================
// SetVolumeCommand
// ============================================================================

SetVolumeCommand::SetVolumeCommand(const NspcEditorLocation& location, std::optional<uint8_t> volume)
    : volume_(volume) {
    location_ = location;
}

bool SetVolumeCommand::execute(NspcSong& song) {
    if (!capturedBefore_) {
        captureState(song, beforeState_);
        capturedBefore_ = true;
    }

    NspcEditor editor;
    bool result = editor.setVolumeAtRow(song, location_, volume_);

    if (result) {
        captureState(song, afterState_);
    }

    return result;
}

bool SetVolumeCommand::undo(NspcSong& song) {
    restoreState(song, beforeState_);
    return true;
}

std::string SetVolumeCommand::description() const {
    if (volume_.has_value()) {
        return std::format("Set Volume {:02X}", *volume_);
    }
    return "Clear Volume";
}

// ============================================================================
// SetQvCommand
// ============================================================================

SetQvCommand::SetQvCommand(const NspcEditorLocation& location, std::optional<uint8_t> qv)
    : qv_(qv) {
    location_ = location;
}

bool SetQvCommand::execute(NspcSong& song) {
    if (!capturedBefore_) {
        captureState(song, beforeState_);
        capturedBefore_ = true;
    }

    NspcEditor editor;
    bool result = editor.setQvAtRow(song, location_, qv_);

    if (result) {
        captureState(song, afterState_);
    }

    return result;
}

bool SetQvCommand::undo(NspcSong& song) {
    restoreState(song, beforeState_);
    return true;
}

std::string SetQvCommand::description() const {
    if (qv_.has_value()) {
        return std::format("Set QV {:02X}", *qv_);
    }
    return "Clear QV";
}

// ============================================================================
// SetEffectsCommand
// ============================================================================

SetEffectsCommand::SetEffectsCommand(const NspcEditorLocation& location, std::vector<Vcmd> effects,
                                     bool preserveSubroutineCalls)
    : effects_(std::move(effects)), preserveSubroutineCalls_(preserveSubroutineCalls) {
    location_ = location;
}

bool SetEffectsCommand::execute(NspcSong& song) {
    if (!capturedBefore_) {
        captureState(song, beforeState_);
        capturedBefore_ = true;
    }

    NspcEditor editor;

    // Clear existing effects first
    bool cleared = editor.clearEffectsAtRow(song, location_, preserveSubroutineCalls_);

    // Add new effects (always attempt, even if clear failed because there were no effects)
    bool result = cleared;
    for (const auto& effect : effects_) {
        bool added = editor.addEffectAtRow(song, location_, effect);
        result = result || added;  // Success if any operation succeeded
    }

    if (result) {
        captureState(song, afterState_);
    }

    return result;
}

bool SetEffectsCommand::undo(NspcSong& song) {
    restoreState(song, beforeState_);
    return true;
}

std::string SetEffectsCommand::description() const {
    if (effects_.empty()) {
        return "Clear Effects";
    }
    if (effects_.size() == 1) {
        return "Set Effect";
    }
    return std::format("Set {} Effects", effects_.size());
}

}  // namespace ntrak::nspc
