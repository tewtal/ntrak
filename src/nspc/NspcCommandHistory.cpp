#include "ntrak/nspc/NspcCommandHistory.hpp"

#include <algorithm>

namespace ntrak::nspc {

NspcCommandHistory::NspcCommandHistory() = default;

bool NspcCommandHistory::execute(NspcSong& song, std::unique_ptr<NspcCommand> command) {
    if (!command) {
        return false;
    }

    // Execute the command immediately (user must see the result!)
    if (!command->execute(song)) {
        return false;
    }

    // If we're in a group, add to the group for undo purposes
    if (currentGroup_) {
        currentGroup_->addCommand(std::move(command));
        return true;
    }

    // Not in a group - add directly to history
    // Clear any redo stack (new command invalidates redo)
    clearRedoStack();

    // Add to history
    history_.push_back(std::move(command));
    currentIndex_ = history_.size();

    // Trim if we exceed max size
    trimHistory();

    return true;
}

bool NspcCommandHistory::canUndo() const {
    return currentIndex_ > 0;
}

bool NspcCommandHistory::canRedo() const {
    return currentIndex_ < history_.size();
}

bool NspcCommandHistory::undo(NspcSong& song) {
    if (!canUndo()) {
        return false;
    }

    --currentIndex_;
    return history_[currentIndex_]->undo(song);
}

bool NspcCommandHistory::redo(NspcSong& song) {
    if (!canRedo()) {
        return false;
    }

    bool result = history_[currentIndex_]->execute(song);
    if (result) {
        ++currentIndex_;
    }
    return result;
}

std::optional<std::string> NspcCommandHistory::undoDescription() const {
    if (!canUndo()) {
        return std::nullopt;
    }
    return history_[currentIndex_ - 1]->description();
}

std::optional<std::string> NspcCommandHistory::redoDescription() const {
    if (!canRedo()) {
        return std::nullopt;
    }
    return history_[currentIndex_]->description();
}

void NspcCommandHistory::beginGroup(std::string description) {
    if (currentGroup_) {
        // Nested groups not supported - end the current one first
        endGroup();
    }

    currentGroup_ = std::make_unique<NspcCommandGroup>(std::move(description));
}

void NspcCommandHistory::endGroup() {
    if (!currentGroup_) {
        return;
    }

    // Only add non-empty groups to history
    if (!currentGroup_->isEmpty()) {
        // Clear redo stack (commands were already executed)
        clearRedoStack();

        // Add group to history
        history_.push_back(std::move(currentGroup_));
        currentIndex_ = history_.size();

        // Trim if needed
        trimHistory();
    }

    currentGroup_.reset();
}

void NspcCommandHistory::clear() {
    history_.clear();
    currentIndex_ = 0;
    currentGroup_.reset();
}

size_t NspcCommandHistory::redoStackSize() const {
    return history_.size() - currentIndex_;
}

void NspcCommandHistory::trimHistory() {
    if (history_.size() <= maxHistorySize_) {
        return;
    }

    // Remove oldest commands
    size_t toRemove = history_.size() - maxHistorySize_;
    history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(toRemove));
    currentIndex_ = std::min(currentIndex_, history_.size());
}

void NspcCommandHistory::clearRedoStack() {
    if (currentIndex_ < history_.size()) {
        history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(currentIndex_), history_.end());
    }
}

}  // namespace ntrak::nspc
