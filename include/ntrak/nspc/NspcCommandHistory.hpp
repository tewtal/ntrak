#pragma once

#include "ntrak/nspc/NspcCommand.hpp"
#include "ntrak/nspc/NspcData.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ntrak::nspc {

/// Manages undo/redo history for commands
class NspcCommandHistory {
public:
    NspcCommandHistory();

    /// Execute and record a command
    bool execute(NspcSong& song, std::unique_ptr<NspcCommand> command);

    /// Undo/redo operations
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    bool undo(NspcSong& song);
    bool redo(NspcSong& song);

    /// Get descriptions for UI (tooltips, menu items)
    [[nodiscard]] std::optional<std::string> undoDescription() const;
    [[nodiscard]] std::optional<std::string> redoDescription() const;

    /// Transaction support for grouping commands
    void beginGroup(std::string description);
    void endGroup();
    [[nodiscard]] bool isInGroup() const { return currentGroup_ != nullptr; }

    /// Clear all history (called on song change)
    void clear();

    /// Configuration
    void setMaxHistorySize(size_t size) { maxHistorySize_ = size; }
    [[nodiscard]] size_t maxHistorySize() const { return maxHistorySize_; }

    /// Stats for debugging/UI
    [[nodiscard]] size_t undoStackSize() const { return currentIndex_; }
    [[nodiscard]] size_t redoStackSize() const;

private:
    void trimHistory();
    void clearRedoStack();

    std::vector<std::unique_ptr<NspcCommand>> history_;
    size_t currentIndex_ = 0;  // Points to next undo position
    size_t maxHistorySize_ = 100;

    // For grouping commands
    std::unique_ptr<NspcCommandGroup> currentGroup_;
    std::string pendingGroupDescription_;
};

}  // namespace ntrak::nspc
