#include "ntrak/nspc/NspcFlatten.hpp"

#include <algorithm>

namespace ntrak::nspc {

namespace {

struct FlattenState {
    const NspcSong* song = nullptr;
    const NspcFlattenOptions* options = nullptr;
    NspcFlatChannel* channel = nullptr;
    uint32_t tick = 0;
    Duration currentDuration{.ticks = 1, .quantization = std::nullopt, .velocity = std::nullopt};
    std::vector<NspcSubroutineFrame> callStack;
};

const std::vector<NspcEventEntry>* resolveEvents(const NspcSong& song, NspcEventOwner owner, int ownerId) {
    if (ownerId < 0) {
        return nullptr;
    }

    const size_t index = static_cast<size_t>(ownerId);
    if (owner == NspcEventOwner::Track) {
        const auto& tracks = song.tracks();
        if (index >= tracks.size()) {
            return nullptr;
        }
        const auto& track = tracks[index];
        if (track.id != ownerId) {
            return nullptr;
        }
        return &track.events;
    }

    const auto& subroutines = song.subroutines();
    if (index >= subroutines.size()) {
        return nullptr;
    }
    const auto& subroutine = subroutines[index];
    if (subroutine.id != ownerId) {
        return nullptr;
    }
    return &subroutine.events;
}

bool pushFlatEvent(FlattenState& state, const NspcEventEntry& entry, NspcEventOwner owner, int ownerId,
                   size_t eventIndex) {
    if (state.channel->events.size() >= state.options->maxEventsPerChannel) {
        return false;
    }

    state.channel->events.push_back(NspcFlatEvent{
        .tick = state.tick,
        .event = entry.event,
        .source =
            NspcEventRef{
                .owner = owner,
                .ownerId = ownerId,
                .eventIndex = eventIndex,
                .eventId = entry.id,
            },
        .subroutineStack = state.callStack,
    });

    return true;
}

bool wouldRecurse(const FlattenState& state, int subroutineId) {
    return std::any_of(state.callStack.begin(), state.callStack.end(),
                       [subroutineId](const NspcSubroutineFrame& frame) { return frame.subroutineId == subroutineId; });
}

bool flattenStream(FlattenState& state, NspcEventOwner owner, int ownerId) {
    const auto* events = resolveEvents(*state.song, owner, ownerId);
    if (!events) {
        return true;
    }

    for (size_t i = 0; i < events->size(); ++i) {
        const auto& entry = (*events)[i];
        if (!pushFlatEvent(state, entry, owner, ownerId, i)) {
            return false;
        }

        if (const auto* duration = std::get_if<Duration>(&entry.event)) {
            state.currentDuration = *duration;
            continue;
        }

        if (const auto* vcmd = std::get_if<Vcmd>(&entry.event)) {
            const auto* subroutineCall = std::get_if<VcmdSubroutineCall>(&vcmd->vcmd);
            if (!subroutineCall) {
                continue;
            }

            if (state.callStack.size() >= state.options->maxSubroutineDepth) {
                continue;
            }
            if (wouldRecurse(state, subroutineCall->subroutineId)) {
                continue;
            }

            const int iterations = static_cast<int>(subroutineCall->count);
            for (int iteration = 0; iteration < iterations; ++iteration) {
                state.callStack.push_back(NspcSubroutineFrame{
                    .subroutineId = subroutineCall->subroutineId,
                    .iteration = static_cast<uint8_t>(iteration),
                    .callEvent =
                        NspcEventRef{
                            .owner = owner,
                            .ownerId = ownerId,
                            .eventIndex = i,
                            .eventId = entry.id,
                        },
                });

                if (!flattenStream(state, NspcEventOwner::Subroutine, subroutineCall->subroutineId)) {
                    return false;
                }

                state.callStack.pop_back();
            }
            continue;
        }

        const bool consumesDuration = std::holds_alternative<Note>(entry.event) ||
                                      std::holds_alternative<Tie>(entry.event) ||
                                      std::holds_alternative<Rest>(entry.event) ||
                                      std::holds_alternative<Percussion>(entry.event);

        if (consumesDuration) {
            state.tick += state.currentDuration.ticks;
            if (state.tick >= state.options->maxTicksPerChannel) {
                return false;
            }
            continue;
        }

        if (std::holds_alternative<End>(entry.event)) {
            return true;
        }
    }

    return true;
}

std::optional<uint32_t> findTrackEndTick(const NspcFlatChannel& channel) {
    for (const auto& event : channel.events) {
        if (event.source.owner != NspcEventOwner::Track) {
            continue;
        }
        if (std::holds_alternative<End>(event.event)) {
            return event.tick;
        }
    }
    return std::nullopt;
}

}  // namespace

NspcFlatPattern flattenPattern(const NspcSong& song, const NspcPattern& pattern, const NspcFlattenOptions& options) {
    NspcFlatPattern flatPattern{};
    flatPattern.patternId = pattern.id;
    std::optional<uint32_t> earliestPatternEndTick;

    for (int channelIndex = 0; channelIndex < 8; ++channelIndex) {
        auto& flatChannel = flatPattern.channels[static_cast<size_t>(channelIndex)];
        flatChannel.channel = channelIndex;

        if (!pattern.channelTrackIds.has_value()) {
            continue;
        }

        const int trackId = pattern.channelTrackIds.value()[static_cast<size_t>(channelIndex)];
        flatChannel.trackId = trackId;
        if (trackId < 0) {
            continue;
        }

        FlattenState state{
            .song = &song,
            .options = &options,
            .channel = &flatChannel,
        };
        (void)flattenStream(state, NspcEventOwner::Track, trackId);

        flatChannel.totalTicks = state.tick;
        if (const auto channelEndTick = findTrackEndTick(flatChannel); channelEndTick.has_value()) {
            if (!earliestPatternEndTick.has_value() || *channelEndTick < *earliestPatternEndTick) {
                earliestPatternEndTick = *channelEndTick;
            }
        } else {
            flatPattern.totalTicks = std::max(flatPattern.totalTicks, state.tick);
        }
    }

    if (!options.clipToEarliestTrackEnd) {
        for (const auto& channel : flatPattern.channels) {
            flatPattern.totalTicks = std::max(flatPattern.totalTicks, channel.totalTicks);
        }
    }

    if (options.clipToEarliestTrackEnd && earliestPatternEndTick.has_value()) {
        const uint32_t stopTick = *earliestPatternEndTick;
        flatPattern.totalTicks = stopTick;

        for (auto& channel : flatPattern.channels) {
            auto& events = channel.events;
            events.erase(std::remove_if(events.begin(), events.end(),
                                        [stopTick](const NspcFlatEvent& event) { return event.tick > stopTick; }),
                         events.end());
            channel.totalTicks = std::min(channel.totalTicks, stopTick);
        }
    }

    return flatPattern;
}

std::optional<NspcFlatPattern> flattenPatternById(const NspcSong& song, int patternId,
                                                  const NspcFlattenOptions& options) {
    if (patternId < 0) {
        return std::nullopt;
    }

    const auto& patterns = song.patterns();
    const auto patternIt = std::find_if(patterns.begin(), patterns.end(),
                                        [patternId](const NspcPattern& pattern) { return pattern.id == patternId; });
    if (patternIt == patterns.end()) {
        return std::nullopt;
    }

    return flattenPattern(song, *patternIt, options);
}

}  // namespace ntrak::nspc
