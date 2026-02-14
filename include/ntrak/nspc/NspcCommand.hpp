#pragma once

#include "ntrak/nspc/NspcData.hpp"
#include "ntrak/nspc/NspcEditor.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ntrak::nspc {

/// Base class for all undoable commands
class NspcCommand {
public:
    virtual ~NspcCommand() = default;

    /// Execute the command (for initial execution and redo)
    virtual bool execute(NspcSong& song) = 0;

    /// Undo the command
    virtual bool undo(NspcSong& song) = 0;

    /// Get human-readable description for UI
    [[nodiscard]] virtual std::string description() const = 0;

protected:
    NspcCommand() = default;
};

/// Composite command for grouping multiple operations into a single undo step
class NspcCommandGroup final : public NspcCommand {
public:
    explicit NspcCommandGroup(std::string description);

    void addCommand(std::unique_ptr<NspcCommand> command);
    [[nodiscard]] bool isEmpty() const { return commands_.empty(); }
    [[nodiscard]] size_t size() const { return commands_.size(); }

    bool execute(NspcSong& song) override;
    bool undo(NspcSong& song) override;
    [[nodiscard]] std::string description() const override { return description_; }

private:
    std::string description_;
    std::vector<std::unique_ptr<NspcCommand>> commands_;
};

/// Base class for single-cell edit commands with state capture
class NspcCellCommand : public NspcCommand {
protected:
    /// Captured state for a single cell/track
    struct CellState {
        // Full tracks snapshot for clean restoration (handles track creation/removal)
        std::vector<NspcTrack> allTracks;

        // Pattern channel track mapping
        std::optional<std::array<int, 8>> patternChannelTrackIds;

        // Subroutine data for full restoration (edits may modify subroutine events)
        struct SubroutineSnapshot {
            int id = -1;
            std::vector<NspcEventEntry> events;
        };
        std::vector<SubroutineSnapshot> subroutineSnapshots;

        // Content origin flag
        NspcContentOrigin contentOrigin = NspcContentOrigin::UserProvided;
    };

    NspcEditorLocation location_;
    CellState beforeState_;
    CellState afterState_;
    bool capturedBefore_ = false;

    /// Capture the full state of the track at the current location
    void captureState(NspcSong& song, CellState& state);

    /// Restore the captured state
    void restoreState(NspcSong& song, const CellState& state);
};

/// Command for setting a row event (note, tie, rest, percussion)
class SetRowEventCommand final : public NspcCellCommand {
public:
    SetRowEventCommand(const NspcEditorLocation& location, const NspcRowEvent& event);

    bool execute(NspcSong& song) override;
    bool undo(NspcSong& song) override;
    [[nodiscard]] std::string description() const override;

private:
    NspcRowEvent event_;
};

/// Command for deleting a row event
class DeleteRowEventCommand final : public NspcCellCommand {
public:
    explicit DeleteRowEventCommand(const NspcEditorLocation& location);

    bool execute(NspcSong& song) override;
    bool undo(NspcSong& song) override;
    [[nodiscard]] std::string description() const override;
};

/// Command for inserting one tick at a row (shift later events down)
class InsertTickCommand final : public NspcCellCommand {
public:
    explicit InsertTickCommand(const NspcEditorLocation& location);

    bool execute(NspcSong& song) override;
    bool undo(NspcSong& song) override;
    [[nodiscard]] std::string description() const override;
};

/// Command for removing one tick at a row (shift later events up)
class RemoveTickCommand final : public NspcCellCommand {
public:
    explicit RemoveTickCommand(const NspcEditorLocation& location);

    bool execute(NspcSong& song) override;
    bool undo(NspcSong& song) override;
    [[nodiscard]] std::string description() const override;
};

/// Command for setting instrument at a row
class SetInstrumentCommand final : public NspcCellCommand {
public:
    SetInstrumentCommand(const NspcEditorLocation& location, std::optional<uint8_t> instrument);

    bool execute(NspcSong& song) override;
    bool undo(NspcSong& song) override;
    [[nodiscard]] std::string description() const override;

private:
    std::optional<uint8_t> instrument_;
};

/// Command for setting volume at a row
class SetVolumeCommand final : public NspcCellCommand {
public:
    SetVolumeCommand(const NspcEditorLocation& location, std::optional<uint8_t> volume);

    bool execute(NspcSong& song) override;
    bool undo(NspcSong& song) override;
    [[nodiscard]] std::string description() const override;

private:
    std::optional<uint8_t> volume_;
};

/// Command for setting quantization/velocity at a row
class SetQvCommand final : public NspcCellCommand {
public:
    SetQvCommand(const NspcEditorLocation& location, std::optional<uint8_t> qv);

    bool execute(NspcSong& song) override;
    bool undo(NspcSong& song) override;
    [[nodiscard]] std::string description() const override;

private:
    std::optional<uint8_t> qv_;
};

/// Command for setting all effects at a row (replaces effect chain)
class SetEffectsCommand final : public NspcCellCommand {
public:
    SetEffectsCommand(const NspcEditorLocation& location, std::vector<Vcmd> effects,
                      bool preserveSubroutineCalls = true);

    bool execute(NspcSong& song) override;
    bool undo(NspcSong& song) override;
    [[nodiscard]] std::string description() const override;

private:
    std::vector<Vcmd> effects_;
    bool preserveSubroutineCalls_ = true;
};

}  // namespace ntrak::nspc
