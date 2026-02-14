#pragma once

#include "ntrak/nspc/NspcCommandHistory.hpp"

#include <string>
#include <utility>

namespace ntrak::nspc {

/// RAII wrapper for command grouping
/// Usage:
///   NspcCommandTransaction txn(history, "Paste");
///   // ... execute multiple commands ...
///   txn.commit();
class NspcCommandTransaction {
public:
    NspcCommandTransaction(NspcCommandHistory& history, std::string description)
        : history_(history), active_(true) {
        history_.beginGroup(std::move(description));
    }

    ~NspcCommandTransaction() {
        if (active_) {
            history_.endGroup();
        }
    }

    // Disable copy/move
    NspcCommandTransaction(const NspcCommandTransaction&) = delete;
    NspcCommandTransaction& operator=(const NspcCommandTransaction&) = delete;
    NspcCommandTransaction(NspcCommandTransaction&&) = delete;
    NspcCommandTransaction& operator=(NspcCommandTransaction&&) = delete;

    void commit() {
        if (active_) {
            history_.endGroup();
            active_ = false;
        }
    }

    void cancel() {
        // endGroup will still be called in destructor if active_
        // This is just a marker for future use
        active_ = false;
    }

private:
    NspcCommandHistory& history_;
    bool active_;
};

}  // namespace ntrak::nspc
