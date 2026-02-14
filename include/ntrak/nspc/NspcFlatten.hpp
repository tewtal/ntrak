#pragma once

#include "ntrak/nspc/NspcData.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace ntrak::nspc {

struct NspcSubroutineFrame {
    int subroutineId = -1;
    uint8_t iteration = 0;
    NspcEventRef callEvent{};
};

struct NspcFlatEvent {
    uint32_t tick = 0;
    NspcEvent event{};
    NspcEventRef source{};
    std::vector<NspcSubroutineFrame> subroutineStack;
};

struct NspcFlatChannel {
    int channel = 0;
    int trackId = -1;
    uint32_t totalTicks = 0;
    std::vector<NspcFlatEvent> events;
};

struct NspcFlatPattern {
    int patternId = -1;
    uint32_t totalTicks = 0;
    std::array<NspcFlatChannel, 8> channels{};
};

struct NspcFlattenOptions {
    uint16_t maxSubroutineDepth = 16;
    uint32_t maxEventsPerChannel = 100000;
    uint32_t maxTicksPerChannel = 0x100000;
    // If true, clip all channel events/ticks to the earliest explicit End tick in the pattern.
    bool clipToEarliestTrackEnd = true;
};

NspcFlatPattern flattenPattern(const NspcSong& song, const NspcPattern& pattern,
                               const NspcFlattenOptions& options = {});

std::optional<NspcFlatPattern> flattenPatternById(const NspcSong& song, int patternId,
                                                  const NspcFlattenOptions& options = {});

}  // namespace ntrak::nspc
