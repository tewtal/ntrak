#include "ntrak/nspc/NspcEditor.hpp"

#include "ntrak/nspc/NspcFlatten.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace ntrak::nspc {
namespace {

constexpr uint8_t kMinDurationTicks = 1;
constexpr uint8_t kMaxDurationTicks = 0x7F;

struct RowSpan {
    uint32_t startTick = 0;
    uint32_t length = 1;
    NspcRowEvent event{};
    NspcEventRef source{};
};

struct TrackRowSpan {
    uint32_t startTick = 0;
    uint32_t length = 1;
    NspcRowEvent event{};
    size_t eventIndex = 0;
    Duration duration{};
};

struct TrackInsertPoint {
    size_t eventIndex = 0;
    Duration duration{};
};

struct ResolvedRef {
    std::vector<NspcEventEntry>* events = nullptr;
    size_t index = 0;
    NspcEventRef ref{};
};

void eraseOrphanDurationBeforeIndex(std::vector<NspcEventEntry>& events, size_t erasedIndex);

uint8_t clampTicks(uint32_t ticks) {
    return static_cast<uint8_t>(std::clamp<uint32_t>(ticks, kMinDurationTicks, kMaxDurationTicks));
}

NspcEvent toEvent(const NspcRowEvent& rowEvent) {
    return std::visit(
        overloaded{
            [](const Note& value) -> NspcEvent { return value; },
            [](const Tie& value) -> NspcEvent { return value; },
            [](const Rest& value) -> NspcEvent { return value; },
            [](const Percussion& value) -> NspcEvent { return value; },
        },
        rowEvent);
}

std::optional<NspcRowEvent> toRowEvent(const NspcEvent& event) {
    if (const auto* value = std::get_if<Note>(&event)) {
        return *value;
    }
    if (const auto* value = std::get_if<Tie>(&event)) {
        return *value;
    }
    if (const auto* value = std::get_if<Rest>(&event)) {
        return *value;
    }
    if (const auto* value = std::get_if<Percussion>(&event)) {
        return *value;
    }
    return std::nullopt;
}

NspcRowEvent continuationEvent(const NspcRowEvent& event) {
    return std::visit(overloaded{
                          [](const Note&) -> NspcRowEvent { return Tie{}; },
                          [](const Tie&) -> NspcRowEvent { return Tie{}; },
                          [](const Rest&) -> NspcRowEvent { return Rest{}; },
                          [](const Percussion&) -> NspcRowEvent { return Tie{}; },
                      },
                      event);
}

bool sameRowEvent(const NspcRowEvent& lhs, const NspcRowEvent& rhs) {
    return std::visit(overloaded{
                          [](const Note& a, const Note& b) { return a.pitch == b.pitch; },
                          [](const Tie&, const Tie&) { return true; },
                          [](const Rest&, const Rest&) { return true; },
                          [](const Percussion& a, const Percussion& b) { return a.index == b.index; },
                          [](const auto&, const auto&) { return false; },
                      },
                      lhs, rhs);
}

bool isContinuationOf(const NspcRowEvent& previous, const NspcRowEvent& candidate) {
    return sameRowEvent(candidate, continuationEvent(previous));
}

bool isInstrumentCommand(const Vcmd& command) {
    return std::holds_alternative<VcmdInst>(command.vcmd);
}

bool isVolumeCommand(const Vcmd& command) {
    return std::holds_alternative<VcmdVolume>(command.vcmd);
}

bool isEffectCommand(const Vcmd& command) {
    return !isInstrumentCommand(command) && !isVolumeCommand(command);
}

bool sameEventRef(const NspcEventRef& lhs, const NspcEventRef& rhs) {
    return lhs.owner == rhs.owner && lhs.ownerId == rhs.ownerId && lhs.eventIndex == rhs.eventIndex &&
           lhs.eventId == rhs.eventId;
}

NspcPattern* findPatternById(NspcSong& song, int patternId) {
    auto& patterns = song.patterns();
    const auto it = std::find_if(patterns.begin(), patterns.end(),
                                 [patternId](const NspcPattern& pattern) { return pattern.id == patternId; });
    if (it == patterns.end()) {
        return nullptr;
    }
    return &(*it);
}

const NspcPattern* findPatternById(const NspcSong& song, int patternId) {
    const auto& patterns = song.patterns();
    const auto it = std::find_if(patterns.begin(), patterns.end(),
                                 [patternId](const NspcPattern& pattern) { return pattern.id == patternId; });
    if (it == patterns.end()) {
        return nullptr;
    }
    return &(*it);
}

NspcSubroutine* findSubroutineById(NspcSong& song, int subroutineId) {
    auto& subroutines = song.subroutines();
    const auto it = std::find_if(subroutines.begin(), subroutines.end(),
                                 [subroutineId](const NspcSubroutine& subroutine) { return subroutine.id == subroutineId; });
    if (it == subroutines.end()) {
        return nullptr;
    }
    return &(*it);
}

const NspcSubroutine* findSubroutineById(const NspcSong& song, int subroutineId) {
    const auto& subroutines = song.subroutines();
    const auto it = std::find_if(subroutines.begin(), subroutines.end(),
                                 [subroutineId](const NspcSubroutine& subroutine) { return subroutine.id == subroutineId; });
    if (it == subroutines.end()) {
        return nullptr;
    }
    return &(*it);
}

int allocateTrackId(const NspcSong& song) {
    const auto& tracks = song.tracks();
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].id != static_cast<int>(i)) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(tracks.size());
}

NspcTrack* resolveTrackById(NspcSong& song, int trackId, bool createIfMissing) {
    if (trackId < 0) {
        return nullptr;
    }

    auto& tracks = song.tracks();
    const size_t index = static_cast<size_t>(trackId);
    if (index < tracks.size() && tracks[index].id == trackId) {
        return &tracks[index];
    }

    if (!createIfMissing) {
        return nullptr;
    }

    if (index >= tracks.size()) {
        tracks.resize(index + 1);
    }

    tracks[index] = NspcTrack{
        .id = trackId,
        .events = {},
        .originalAddr = 0,
    };
    return &tracks[index];
}

NspcTrack* resolveChannelTrack(NspcSong& song, const NspcEditorLocation& location, bool createIfMissing) {
    if (location.patternId < 0 || location.channel < 0 || location.channel >= 8) {
        return nullptr;
    }

    NspcPattern* pattern = findPatternById(song, location.patternId);
    if (!pattern) {
        return nullptr;
    }

    if (!pattern->channelTrackIds.has_value()) {
        if (!createIfMissing) {
            return nullptr;
        }
        pattern->channelTrackIds = std::array<int, 8>{-1, -1, -1, -1, -1, -1, -1, -1};
    }

    auto& trackIds = pattern->channelTrackIds.value();
    int trackId = trackIds[static_cast<size_t>(location.channel)];
    if (trackId < 0) {
        if (!createIfMissing) {
            return nullptr;
        }
        trackId = allocateTrackId(song);
        trackIds[static_cast<size_t>(location.channel)] = trackId;
    }

    return resolveTrackById(song, trackId, createIfMissing);
}

std::vector<NspcEventEntry>* resolveOwnerEvents(NspcSong& song, NspcEventOwner owner, int ownerId) {
    if (ownerId < 0) {
        return nullptr;
    }

    const size_t index = static_cast<size_t>(ownerId);
    if (owner == NspcEventOwner::Track) {
        auto& tracks = song.tracks();
        if (index >= tracks.size()) {
            return nullptr;
        }
        if (tracks[index].id != ownerId) {
            return nullptr;
        }
        return &tracks[index].events;
    }

    auto& subroutines = song.subroutines();
    if (index >= subroutines.size()) {
        return nullptr;
    }
    if (subroutines[index].id != ownerId) {
        return nullptr;
    }
    return &subroutines[index].events;
}

NspcEventId scanNextEventId(const NspcSong& song) {
    NspcEventId maxId = 0;
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            maxId = std::max(maxId, entry.id);
        }
    }
    for (const auto& subroutine : song.subroutines()) {
        for (const auto& entry : subroutine.events) {
            maxId = std::max(maxId, entry.id);
        }
    }
    return maxId + 1;
}

NspcEventId nextEventId(NspcSong& song) {
    const NspcEventId cached = song.peekNextEventId();
    if (cached > 1) {
        return cached;
    }
    const NspcEventId scanned = scanNextEventId(song);
    song.setNextEventId(scanned);
    return scanned;
}

void syncNextEventId(NspcSong& song, NspcEventId nextId) {
    if (nextId > song.peekNextEventId()) {
        song.setNextEventId(nextId);
    }
}

std::optional<size_t> resolveEventIndex(const std::vector<NspcEventEntry>& events, const NspcEventRef& ref) {
    if (ref.eventIndex < events.size()) {
        const auto& entry = events[ref.eventIndex];
        if (ref.eventId == 0 || entry.id == ref.eventId) {
            return ref.eventIndex;
        }
    }

    if (ref.eventId == 0) {
        return std::nullopt;
    }

    for (size_t i = 0; i < events.size(); ++i) {
        if (events[i].id == ref.eventId) {
            return i;
        }
    }
    return std::nullopt;
}

NspcEventEntry makeEntry(NspcEventId& nextId, NspcEvent event) {
    return NspcEventEntry{
        .id = nextId++,
        .event = std::move(event),
        .originalAddr = std::nullopt,
    };
}

void insertDurationEvent(std::vector<NspcEventEntry>& events, size_t index, uint8_t ticks, NspcEventId& nextId) {
    events.insert(events.begin() + static_cast<std::ptrdiff_t>(index),
                  makeEntry(nextId, Duration{
                                        .ticks = ticks,
                                        .quantization = std::nullopt,
                                        .velocity = std::nullopt,
                                    }));
}

bool isDurationEvent(const NspcEvent& event) {
    return std::holds_alternative<Duration>(event);
}

bool isEndEvent(const NspcEvent& event) {
    return std::holds_alternative<End>(event);
}

bool isTimedRowEvent(const NspcEvent& event) {
    return std::holds_alternative<Note>(event) || std::holds_alternative<Tie>(event) || std::holds_alternative<Rest>(event) ||
           std::holds_alternative<Percussion>(event);
}

bool ensureDurationBeforeEvent(std::vector<NspcEventEntry>& events, size_t& eventIndex, uint8_t ticks, NspcEventId& nextId) {
    if (eventIndex > 0) {
        if (auto* existing = std::get_if<Duration>(&events[eventIndex - 1].event)) {
            existing->ticks = ticks;
            return false;
        }
    }

    insertDurationEvent(events, eventIndex, ticks, nextId);
    ++eventIndex;
    return true;
}

void restoreDurationBeforeNextTimedEvent(std::vector<NspcEventEntry>& events, size_t scanIndex, uint8_t ticks,
                                         NspcEventId& nextId) {
    for (size_t i = scanIndex; i < events.size(); ++i) {
        const NspcEvent& event = events[i].event;
        if (isEndEvent(event)) {
            return;
        }
        if (isDurationEvent(event)) {
            return;
        }
        if (isTimedRowEvent(event)) {
            insertDurationEvent(events, i, ticks, nextId);
            return;
        }
    }
}

void applyDurationState(Duration& target, const Duration& source, uint8_t ticks) {
    target.ticks = ticks;
    target.quantization = source.quantization;
    target.velocity = source.velocity;
}

void insertDurationEventWithState(std::vector<NspcEventEntry>& events, size_t index, const Duration& duration,
                                  NspcEventId& nextId) {
    insertDurationEvent(events, index, clampTicks(duration.ticks), nextId);
    auto* inserted = std::get_if<Duration>(&events[index].event);
    if (!inserted) {
        return;
    }
    applyDurationState(*inserted, duration, clampTicks(duration.ticks));
}

void restoreDurationStateBeforeNextTimedEvent(std::vector<NspcEventEntry>& events, size_t scanIndex,
                                              const Duration& duration, NspcEventId& nextId) {
    for (size_t i = scanIndex; i < events.size(); ++i) {
        auto& event = events[i].event;
        if (isEndEvent(event)) {
            return;
        }
        if (auto* existingDuration = std::get_if<Duration>(&event)) {
            applyDurationState(*existingDuration, duration, existingDuration->ticks);
            return;
        }
        if (isTimedRowEvent(event)) {
            insertDurationEventWithState(events, i, duration, nextId);
            return;
        }
    }
}

std::vector<TrackRowSpan> collectTrackRowSpans(const std::vector<NspcEventEntry>& events, uint32_t* endTickOut = nullptr) {
    std::vector<TrackRowSpan> spans;
    Duration currentDuration{
        .ticks = kMinDurationTicks,
        .quantization = std::nullopt,
        .velocity = std::nullopt,
    };
    uint32_t tick = 0;

    for (size_t i = 0; i < events.size(); ++i) {
        const auto& event = events[i].event;
        if (const auto* duration = std::get_if<Duration>(&event)) {
            currentDuration = *duration;
            currentDuration.ticks = std::max<uint8_t>(duration->ticks, kMinDurationTicks);
            continue;
        }

        if (isEndEvent(event)) {
            break;
        }

        const auto rowEvent = toRowEvent(event);
        if (!rowEvent.has_value()) {
            continue;
        }

        spans.push_back(TrackRowSpan{
            .startTick = tick,
            .length = std::max<uint8_t>(currentDuration.ticks, kMinDurationTicks),
            .event = *rowEvent,
            .eventIndex = i,
            .duration = currentDuration,
        });
        tick += std::max<uint8_t>(currentDuration.ticks, kMinDurationTicks);
    }

    if (endTickOut) {
        *endTickOut = tick;
    }
    return spans;
}

std::optional<size_t> findTrackSpanAtRow(const std::vector<TrackRowSpan>& spans, uint32_t row) {
    for (size_t i = 0; i < spans.size(); ++i) {
        const auto& span = spans[i];
        const uint32_t endTick = span.startTick + span.length;
        if (row >= span.startTick && row < endTick) {
            return i;
        }
    }
    return std::nullopt;
}

NspcRowEvent continuationEventForInsertedRow(const std::vector<TrackRowSpan>& spans, uint32_t row) {
    if (row == 0) {
        return Tie{};
    }

    const uint32_t previousRow = row - 1;
    for (const auto& span : spans) {
        const uint32_t endTick = span.startTick + span.length;
        if (previousRow >= span.startTick && previousRow < endTick) {
            return continuationEvent(span.event);
        }
    }

    return Tie{};
}

std::optional<TrackInsertPoint> findTrackInsertionPointAtRow(const std::vector<NspcEventEntry>& events, uint32_t row) {
    Duration currentDuration{
        .ticks = kMinDurationTicks,
        .quantization = std::nullopt,
        .velocity = std::nullopt,
    };
    uint32_t tick = 0;

    for (size_t i = 0; i < events.size(); ++i) {
        const auto& event = events[i].event;
        if (const auto* duration = std::get_if<Duration>(&event)) {
            currentDuration = *duration;
            currentDuration.ticks = std::max<uint8_t>(duration->ticks, kMinDurationTicks);
            continue;
        }

        if (tick >= row) {
            return TrackInsertPoint{
                .eventIndex = i,
                .duration = currentDuration,
            };
        }

        if (isTimedRowEvent(event)) {
            tick += std::max<uint8_t>(currentDuration.ticks, kMinDurationTicks);
        }
    }

    return std::nullopt;
}

bool eraseTrackCommandsAtRow(std::vector<NspcEventEntry>& events, uint32_t row) {
    Duration currentDuration{
        .ticks = kMinDurationTicks,
        .quantization = std::nullopt,
        .velocity = std::nullopt,
    };
    uint32_t tick = 0;
    std::vector<size_t> eraseIndices;

    for (size_t i = 0; i < events.size(); ++i) {
        const auto& event = events[i].event;
        if (const auto* duration = std::get_if<Duration>(&event)) {
            currentDuration = *duration;
            currentDuration.ticks = std::max<uint8_t>(duration->ticks, kMinDurationTicks);
            continue;
        }

        if (tick == row && std::holds_alternative<Vcmd>(event)) {
            eraseIndices.push_back(i);
        }

        if (isTimedRowEvent(event)) {
            tick += std::max<uint8_t>(currentDuration.ticks, kMinDurationTicks);
        }
        if (isEndEvent(event) && tick > row) {
            break;
        }
    }

    if (eraseIndices.empty()) {
        return false;
    }

    std::ranges::sort(eraseIndices, [](size_t lhs, size_t rhs) { return lhs > rhs; });
    for (const size_t index : eraseIndices) {
        events.erase(events.begin() + static_cast<std::ptrdiff_t>(index));
        eraseOrphanDurationBeforeIndex(events, index);
    }
    return true;
}

size_t findEndIndex(const std::vector<NspcEventEntry>& events) {
    const auto it = std::find_if(events.begin(), events.end(),
                                 [](const NspcEventEntry& entry) { return std::holds_alternative<End>(entry.event); });
    if (it == events.end()) {
        return events.size();
    }
    return static_cast<size_t>(std::distance(events.begin(), it));
}

std::vector<RowSpan> collectRowSpans(const NspcFlatChannel& channel) {
    std::vector<RowSpan> spans;
    uint8_t currentDuration = 1;

    for (const auto& flatEvent : channel.events) {
        if (const auto* duration = std::get_if<Duration>(&flatEvent.event)) {
            currentDuration = std::max<uint8_t>(duration->ticks, kMinDurationTicks);
            continue;
        }

        const auto rowEvent = toRowEvent(flatEvent.event);
        if (!rowEvent.has_value()) {
            continue;
        }

        spans.push_back(RowSpan{
            .startTick = flatEvent.tick,
            .length = std::max<uint8_t>(currentDuration, kMinDurationTicks),
            .event = *rowEvent,
            .source = flatEvent.source,
        });
    }

    return spans;
}

std::optional<NspcFlatChannel> flattenChannel(const NspcSong& song, const NspcEditorLocation& location) {
    if (location.patternId < 0 || location.channel < 0 || location.channel >= 8) {
        return std::nullopt;
    }

    const auto pattern = findPatternById(song, location.patternId);
    if (!pattern) {
        return std::nullopt;
    }
    if (!pattern->channelTrackIds.has_value()) {
        return std::nullopt;
    }

    auto flatPattern = flattenPatternById(song, location.patternId);
    if (!flatPattern.has_value()) {
        return std::nullopt;
    }

    return flatPattern->channels[static_cast<size_t>(location.channel)];
}

bool channelHasAssignedTrack(const NspcSong& song, const NspcEditorLocation& location) {
    if (location.patternId < 0 || location.channel < 0 || location.channel >= 8) {
        return false;
    }

    const auto* pattern = findPatternById(song, location.patternId);
    if (!pattern || !pattern->channelTrackIds.has_value()) {
        return false;
    }

    return pattern->channelTrackIds.value()[static_cast<size_t>(location.channel)] >= 0;
}

std::optional<uint32_t> patternEndTick(const NspcSong& song, int patternId) {
    const auto flatPattern = flattenPatternById(song, patternId);
    if (!flatPattern.has_value()) {
        return std::nullopt;
    }
    return flatPattern->totalTicks;
}

bool extendChannelToTick(NspcSong& song, const NspcEditorLocation& location, uint32_t targetTick) {
    NspcTrack* track = resolveChannelTrack(song, location, true);
    if (!track) {
        return false;
    }

    auto channel = flattenChannel(song, location);
    uint32_t endTick = 0;
    NspcRowEvent fillEvent = Tie{};
    if (channel.has_value()) {
        const auto spans = collectRowSpans(*channel);
        if (!spans.empty()) {
            const auto& last = spans.back();
            endTick = last.startTick + last.length;
            fillEvent = continuationEvent(last.event);
        }
    }

    if (targetTick <= endTick) {
        return false;
    }

    NspcEventId nextId = nextEventId(song);
    size_t insertIndex = findEndIndex(track->events);
    uint32_t gap = targetTick - endTick;
    while (gap > 0) {
        const uint8_t chunk = clampTicks(gap);
        insertDurationEvent(track->events, insertIndex, chunk, nextId);
        ++insertIndex;
        track->events.insert(track->events.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                             makeEntry(nextId, toEvent(fillEvent)));
        ++insertIndex;
        gap -= chunk;
    }
    syncNextEventId(song, nextId);
    return true;
}

void eraseOrphanDurationBeforeIndex(std::vector<NspcEventEntry>& events, size_t erasedIndex);

bool setTrackLength(NspcTrack& track, uint32_t targetTick, NspcEventId& nextId) {
    bool changed = false;

    if (track.events.empty()) {
        track.events.push_back(makeEntry(nextId, End{}));
        changed = true;
    }

    size_t endIndex = findEndIndex(track.events);
    if (endIndex == track.events.size()) {
        track.events.push_back(makeEntry(nextId, End{}));
        endIndex = track.events.size() - 1;
        changed = true;
    }

    if (endIndex + 1 < track.events.size()) {
        track.events.erase(track.events.begin() + static_cast<std::ptrdiff_t>(endIndex + 1), track.events.end());
        changed = true;
    }

    Duration currentDuration{
        .ticks = kMinDurationTicks,
        .quantization = std::nullopt,
        .velocity = std::nullopt,
    };
    uint32_t tick = 0;
    std::optional<NspcRowEvent> lastTimedEvent = std::nullopt;
    bool trimmed = false;

    for (size_t i = 0; i < endIndex; ++i) {
        const NspcEvent& event = track.events[i].event;
        if (const auto* duration = std::get_if<Duration>(&event)) {
            currentDuration = *duration;
            currentDuration.ticks = std::max<uint8_t>(duration->ticks, kMinDurationTicks);
            continue;
        }

        const auto rowEvent = toRowEvent(event);
        if (!rowEvent.has_value()) {
            continue;
        }

        const uint32_t startTick = tick;
        const uint32_t endTick = startTick + currentDuration.ticks;

        if (targetTick <= startTick) {
            track.events.erase(track.events.begin() + static_cast<std::ptrdiff_t>(i),
                               track.events.begin() + static_cast<std::ptrdiff_t>(endIndex));
            endIndex = i;
            eraseOrphanDurationBeforeIndex(track.events, endIndex);
            changed = true;
            tick = targetTick;
            trimmed = true;
            break;
        }

        if (targetTick < endTick) {
            const uint32_t beforeLen = targetTick - startTick;
            size_t eventIndex = i;
            const uint8_t splitTicks = clampTicks(beforeLen);
            bool durationWouldChange = false;
            if (eventIndex > 0) {
                if (const auto* existingDuration = std::get_if<Duration>(&track.events[eventIndex - 1].event)) {
                    durationWouldChange = existingDuration->ticks != splitTicks;
                }
            }
            const bool insertedDuration = ensureDurationBeforeEvent(track.events, eventIndex, splitTicks, nextId);
            if (insertedDuration) {
                if (auto* inserted = std::get_if<Duration>(&track.events[eventIndex - 1].event)) {
                    inserted->quantization = currentDuration.quantization;
                    inserted->velocity = currentDuration.velocity;
                }
                ++endIndex;
            }
            changed = changed || insertedDuration || durationWouldChange;

            const size_t eraseStart = eventIndex + 1;
            if (eraseStart < endIndex) {
                track.events.erase(track.events.begin() + static_cast<std::ptrdiff_t>(eraseStart),
                                   track.events.begin() + static_cast<std::ptrdiff_t>(endIndex));
                changed = true;
            }
            endIndex = eraseStart;
            tick = targetTick;
            trimmed = true;
            break;
        }

        tick = endTick;
        lastTimedEvent = *rowEvent;
    }

    if (!trimmed && targetTick > tick) {
        const NspcRowEvent fillEvent =
            lastTimedEvent.has_value() ? continuationEvent(*lastTimedEvent) : NspcRowEvent{Tie{}};
        size_t insertIndex = endIndex;
        uint32_t gap = targetTick - tick;
        while (gap > 0) {
            const uint8_t chunk = clampTicks(gap);
            insertDurationEvent(track.events, insertIndex, chunk, nextId);
            ++insertIndex;
            track.events.insert(track.events.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                                makeEntry(nextId, toEvent(fillEvent)));
            ++insertIndex;
            gap -= chunk;
            changed = true;
        }
    }

    return changed;
}

bool trackHasSubroutineCalls(const NspcTrack& track) {
    for (const auto& entry : track.events) {
        if (std::holds_alternative<End>(entry.event)) {
            break;
        }

        const auto* vcmd = std::get_if<Vcmd>(&entry.event);
        if (!vcmd) {
            continue;
        }
        if (std::holds_alternative<VcmdSubroutineCall>(vcmd->vcmd)) {
            return true;
        }
    }
    return false;
}

std::optional<size_t> findTargetSpanIndex(const std::vector<RowSpan>& spans, uint32_t row) {
    for (size_t i = 0; i < spans.size(); ++i) {
        const auto& span = spans[i];
        const uint32_t endTick = span.startTick + span.length;
        if (row >= span.startTick && row < endTick) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<size_t> findPreviousSpanIndex(const std::vector<RowSpan>& spans, size_t targetIndex) {
    if (targetIndex == 0 || targetIndex >= spans.size()) {
        return std::nullopt;
    }
    return targetIndex - 1;
}

void eraseOrphanDurationBeforeIndex(std::vector<NspcEventEntry>& events, size_t erasedIndex) {
    if (erasedIndex == 0) {
        return;
    }

    const size_t maybeDurationIndex = erasedIndex - 1;
    if (maybeDurationIndex >= events.size() || !isDurationEvent(events[maybeDurationIndex].event)) {
        return;
    }

    bool durationIsRedundant = false;
    for (size_t scan = maybeDurationIndex + 1; scan < events.size(); ++scan) {
        const auto& scanEvent = events[scan].event;
        if (isDurationEvent(scanEvent) || isEndEvent(scanEvent)) {
            durationIsRedundant = true;
            break;
        }
        if (isTimedRowEvent(scanEvent)) {
            break;
        }
    }

    if (durationIsRedundant) {
        events.erase(events.begin() + static_cast<std::ptrdiff_t>(maybeDurationIndex));
    }
}

bool mergeSpanIntoPrevious(NspcSong& song, const RowSpan& previous, const RowSpan& target, bool requireContinuationMatch) {
    if (previous.source.owner != target.source.owner || previous.source.ownerId != target.source.ownerId) {
        return false;
    }
    if (previous.startTick + previous.length != target.startTick) {
        return false;
    }
    if (requireContinuationMatch && !isContinuationOf(previous.event, target.event)) {
        return false;
    }

    auto* mergedEvents = resolveOwnerEvents(song, target.source.owner, target.source.ownerId);
    if (!mergedEvents) {
        return false;
    }

    auto prevStreamIndex = resolveEventIndex(*mergedEvents, previous.source);
    auto curStreamIndex = resolveEventIndex(*mergedEvents, target.source);
    if (!prevStreamIndex.has_value() || !curStreamIndex.has_value() || *prevStreamIndex >= *curStreamIndex) {
        return false;
    }

    for (size_t i = *prevStreamIndex + 1; i < *curStreamIndex; ++i) {
        const auto& event = mergedEvents->at(i).event;
        if (!isDurationEvent(event)) {
            return false;
        }
    }

    const uint32_t mergedLength = previous.length + target.length;
    if (mergedLength > kMaxDurationTicks) {
        return false;
    }

    const uint8_t mergedTicks = clampTicks(mergedLength);
    bool updatedPrevDuration = false;
    if (*prevStreamIndex > 0) {
        if (auto* prevDuration = std::get_if<Duration>(&mergedEvents->at(*prevStreamIndex - 1).event)) {
            prevDuration->ticks = mergedTicks;
            updatedPrevDuration = true;
        }
    }

    if (!updatedPrevDuration) {
        NspcEventId nextId = nextEventId(song);
        insertDurationEvent(*mergedEvents, *prevStreamIndex, mergedTicks, nextId);
        syncNextEventId(song, nextId);
        if (*curStreamIndex >= *prevStreamIndex) {
            ++(*curStreamIndex);
        }
    }

    if (*curStreamIndex >= mergedEvents->size()) {
        return false;
    }

    const size_t erasedIndex = *curStreamIndex;
    mergedEvents->erase(mergedEvents->begin() + static_cast<std::ptrdiff_t>(erasedIndex));
    eraseOrphanDurationBeforeIndex(*mergedEvents, erasedIndex);
    return true;
}

bool compactContinuationAtRow(NspcSong& song, const NspcEditorLocation& location) {
    const auto channel = flattenChannel(song, location);
    if (!channel.has_value()) {
        return false;
    }

    const auto spans = collectRowSpans(*channel);
    const auto targetIndex = findTargetSpanIndex(spans, location.row);
    if (!targetIndex.has_value() || *targetIndex == 0) {
        return false;
    }

    const RowSpan& target = spans[*targetIndex];
    if (target.startTick != location.row) {
        return false;
    }

    const RowSpan& previous = spans[*targetIndex - 1];
    return mergeSpanIntoPrevious(song, previous, target, true);
}

std::vector<ResolvedRef> resolveRefs(NspcSong& song, const std::vector<NspcEventRef>& refs) {
    std::vector<ResolvedRef> resolved;
    resolved.reserve(refs.size());

    for (const auto& ref : refs) {
        auto* events = resolveOwnerEvents(song, ref.owner, ref.ownerId);
        if (!events) {
            continue;
        }
        const auto index = resolveEventIndex(*events, ref);
        if (!index.has_value()) {
            continue;
        }
        resolved.push_back(ResolvedRef{
            .events = events,
            .index = *index,
            .ref = ref,
        });
    }

    return resolved;
}

void dedupeRefs(std::vector<NspcEventRef>& refs) {
    std::vector<NspcEventRef> deduped;
    deduped.reserve(refs.size());
    for (const auto& ref : refs) {
        const bool alreadyPresent = std::any_of(deduped.begin(), deduped.end(),
                                                [&](const NspcEventRef& existing) { return sameEventRef(existing, ref); });
        if (!alreadyPresent) {
            deduped.push_back(ref);
        }
    }
    refs = std::move(deduped);
}

template <typename CommandPredicate>
std::vector<NspcEventRef> collectCommandRefsAtRow(const NspcFlatChannel& channel, uint32_t row,
                                                  CommandPredicate&& predicate, bool includeSubroutineCalls = false) {
    std::vector<NspcEventRef> refs;
    for (const auto& flatEvent : channel.events) {
        if (flatEvent.tick != row) {
            continue;
        }
        if (flatEvent.source.ownerId < 0) {
            continue;
        }
        const auto* vcmd = std::get_if<Vcmd>(&flatEvent.event);
        if (!vcmd) {
            continue;
        }
        if (!includeSubroutineCalls && std::holds_alternative<VcmdSubroutineCall>(vcmd->vcmd)) {
            continue;
        }
        if (!predicate(*vcmd)) {
            continue;
        }
        refs.push_back(flatEvent.source);
    }
    dedupeRefs(refs);
    return refs;
}

std::optional<NspcEventRef> findAnchorAtRow(const NspcFlatChannel& channel, uint32_t row) {
    for (const auto& flatEvent : channel.events) {
        if (flatEvent.tick != row) {
            continue;
        }
        if (flatEvent.source.ownerId < 0) {
            continue;
        }
        if (const auto* vcmd = std::get_if<Vcmd>(&flatEvent.event)) {
            if (std::holds_alternative<VcmdSubroutineCall>(vcmd->vcmd)) {
                continue;
            }
        }
        return flatEvent.source;
    }
    return std::nullopt;
}

std::optional<int> findSubroutineCallIdAtRow(const NspcFlatChannel& channel, uint32_t row) {
    for (const auto& flatEvent : channel.events) {
        if (flatEvent.tick != row) {
            continue;
        }
        const auto* vcmd = std::get_if<Vcmd>(&flatEvent.event);
        if (!vcmd) {
            continue;
        }
        const auto* call = std::get_if<VcmdSubroutineCall>(&vcmd->vcmd);
        if (!call) {
            continue;
        }
        return call->subroutineId;
    }
    return std::nullopt;
}

bool setFirstTimedEventInSubroutine(NspcSong& song, int subroutineId, const NspcRowEvent& event) {
    auto* events = resolveOwnerEvents(song, NspcEventOwner::Subroutine, subroutineId);
    if (!events) {
        return false;
    }

    for (auto& entry : *events) {
        if (isEndEvent(entry.event)) {
            break;
        }
        if (!isTimedRowEvent(entry.event)) {
            continue;
        }
        entry.event = toEvent(event);
        return true;
    }

    return false;
}

bool eraseResolvedRefs(std::vector<ResolvedRef> refs, const std::optional<Vcmd>& replacement) {
    if (refs.empty()) {
        return false;
    }

    std::optional<ResolvedRef> keep;
    if (replacement.has_value()) {
        keep = refs.front();
    }

    std::ranges::sort(refs, [](const ResolvedRef& lhs, const ResolvedRef& rhs) {
        if (lhs.events == rhs.events) {
            return lhs.index > rhs.index;
        }
        return lhs.events < rhs.events;
    });

    if (keep.has_value()) {
        keep->events->at(keep->index).event = Vcmd{*replacement};
    }

    for (const auto& ref : refs) {
        if (keep.has_value() && ref.events == keep->events && ref.index == keep->index) {
            continue;
        }
        if (ref.index >= ref.events->size()) {
            continue;
        }
        ref.events->erase(ref.events->begin() + static_cast<std::ptrdiff_t>(ref.index));
    }

    return true;
}

bool appendResolvedRef(std::vector<ResolvedRef> refs, const Vcmd& effect, NspcEventId& nextId) {
    if (refs.empty()) {
        return false;
    }

    auto* targetEvents = refs.front().events;
    size_t insertIndex = refs.front().index + 1;
    for (const auto& ref : refs) {
        if (ref.events != targetEvents) {
            continue;
        }
        insertIndex = std::max(insertIndex, ref.index + 1);
    }

    if (!targetEvents || insertIndex > targetEvents->size()) {
        return false;
    }

    targetEvents->insert(targetEvents->begin() + static_cast<std::ptrdiff_t>(insertIndex), makeEntry(nextId, Vcmd{effect}));
    return true;
}

NspcEventEntry cloneEntryWithNewId(const NspcEventEntry& entry, NspcEventId& nextId) {
    NspcEventEntry clone = entry;
    clone.id = nextId++;
    return clone;
}

bool inlineSubroutineCallsInEvents(std::vector<NspcEventEntry>& events, int subroutineId, std::span<const NspcEventEntry> subroutineEvents,
                                   NspcEventId& nextId) {
    bool changed = false;
    std::vector<NspcEventEntry> inlined;
    inlined.reserve(events.size());

    for (const auto& entry : events) {
        const auto* vcmd = std::get_if<Vcmd>(&entry.event);
        const auto* call = (vcmd != nullptr) ? std::get_if<VcmdSubroutineCall>(&vcmd->vcmd) : nullptr;
        if (call == nullptr || call->subroutineId != subroutineId) {
            inlined.push_back(entry);
            continue;
        }

        const int iterations = static_cast<int>(call->count);
        for (int iteration = 0; iteration < iterations; ++iteration) {
            for (const auto& subEntry : subroutineEvents) {
                if (isEndEvent(subEntry.event)) {
                    continue;
                }
                inlined.push_back(cloneEntryWithNewId(subEntry, nextId));
            }
        }
        changed = true;
    }

    if (changed) {
        events = std::move(inlined);
    }
    return changed;
}

void remapSubroutineCallIds(std::vector<NspcEventEntry>& events, const std::unordered_map<int, int>& remap,
                            const std::unordered_map<int, uint16_t>& addrBySubroutineId) {
    for (auto& entry : events) {
        auto* vcmd = std::get_if<Vcmd>(&entry.event);
        if (!vcmd) {
            continue;
        }
        auto* call = std::get_if<VcmdSubroutineCall>(&vcmd->vcmd);
        if (!call) {
            continue;
        }

        if (const auto remapIt = remap.find(call->subroutineId); remapIt != remap.end()) {
            call->subroutineId = remapIt->second;
        }
        if (const auto addrIt = addrBySubroutineId.find(call->subroutineId); addrIt != addrBySubroutineId.end()) {
            call->originalAddr = addrIt->second;
        }
    }
}

std::optional<std::pair<size_t, size_t>> resolveTrackSliceForRowRange(const NspcSong& song, const NspcEditorLocation& location,
                                                                       uint32_t startRow, uint32_t endRow, int trackId) {
    if (startRow > endRow) {
        std::swap(startRow, endRow);
    }

    const auto channel = flattenChannel(song, location);
    if (!channel.has_value() || channel->trackId != trackId) {
        return std::nullopt;
    }

    std::optional<uint32_t> firstTick;
    std::optional<uint32_t> lastTick;
    for (const auto& flatEvent : channel->events) {
        if (flatEvent.tick < startRow || flatEvent.tick > endRow) {
            continue;
        }
        if (flatEvent.source.owner != NspcEventOwner::Track || flatEvent.source.ownerId != trackId) {
            continue;
        }
        if (!isTimedRowEvent(flatEvent.event)) {
            continue;
        }
        if (!firstTick.has_value()) {
            firstTick = flatEvent.tick;
        }
        lastTick = flatEvent.tick;
    }

    if (!firstTick.has_value() || !lastTick.has_value()) {
        return std::nullopt;
    }

    std::optional<size_t> startIndex;
    std::optional<size_t> endIndex;
    for (const auto& flatEvent : channel->events) {
        if (flatEvent.source.owner != NspcEventOwner::Track || flatEvent.source.ownerId != trackId) {
            continue;
        }
        if (flatEvent.tick == *firstTick) {
            startIndex = startIndex.has_value() ? std::min(*startIndex, flatEvent.source.eventIndex) : flatEvent.source.eventIndex;
        }
        if (flatEvent.tick == *lastTick) {
            endIndex = endIndex.has_value() ? std::max(*endIndex, flatEvent.source.eventIndex) : flatEvent.source.eventIndex;
        }
    }

    if (!startIndex.has_value() || !endIndex.has_value() || *endIndex < *startIndex) {
        return std::nullopt;
    }

    return std::pair<size_t, size_t>{*startIndex, *endIndex + 1};
}

template <typename CommandPredicate>
bool editCommandAtRow(NspcSong& song, const NspcEditorLocation& location, std::optional<Vcmd> replacement,
                      CommandPredicate&& predicate, bool includeSubroutineCalls = false) {
    const bool hadAssignedTrackBefore = channelHasAssignedTrack(song, location);
    const std::optional<uint32_t> baselineEndTick =
        hadAssignedTrackBefore ? std::nullopt : patternEndTick(song, location.patternId);
    auto maybe_extend_new_track = [&](bool result) -> bool {
        if (!result || hadAssignedTrackBefore || !baselineEndTick.has_value()) {
            return result;
        }
        if (*baselineEndTick > location.row) {
            (void)extendChannelToTick(song, location, *baselineEndTick);
        }
        return result;
    };

    const auto channel = flattenChannel(song, location);
    if (!channel.has_value()) {
        return false;
    }

    auto commandRefs = collectCommandRefsAtRow(*channel, location.row, predicate, includeSubroutineCalls);
    if (!commandRefs.empty()) {
        auto resolved = resolveRefs(song, commandRefs);
        const bool changed = eraseResolvedRefs(std::move(resolved), replacement);
        if (changed && !replacement.has_value()) {
            return compactContinuationAtRow(song, location) || changed;
        }
        return changed;
    }

    if (!replacement.has_value()) {
        return false;
    }

    // If there's a direct anchor event at this row, insert before it (correct tick).
    auto anchorRef = findAnchorAtRow(*channel, location.row);
    if (anchorRef.has_value()) {
        auto* ownerEvents = resolveOwnerEvents(song, anchorRef->owner, anchorRef->ownerId);
        if (!ownerEvents) {
            return false;
        }
        auto index = resolveEventIndex(*ownerEvents, *anchorRef);
        if (!index.has_value()) {
            return false;
        }
        NspcEventId nid = nextEventId(song);
        ownerEvents->insert(ownerEvents->begin() + static_cast<std::ptrdiff_t>(*index),
                            makeEntry(nid, Vcmd{*replacement}));
        syncNextEventId(song, nid);
        return maybe_extend_new_track(true);
    }

    // No direct anchor — check if we're within a span.
    const auto spans = collectRowSpans(*channel);
    const auto targetSpanIndex = findTargetSpanIndex(spans, location.row);

    if (targetSpanIndex.has_value()) {
        const auto& span = spans[*targetSpanIndex];
        auto* ownerEvents = resolveOwnerEvents(song, span.source.owner, span.source.ownerId);
        if (!ownerEvents) {
            return false;
        }
        auto streamIndex = resolveEventIndex(*ownerEvents, span.source);
        if (!streamIndex.has_value()) {
            return false;
        }

        if (location.row == span.startTick) {
            // At span start: insert vcmd before the event.
            NspcEventId nid = nextEventId(song);
            ownerEvents->insert(ownerEvents->begin() + static_cast<std::ptrdiff_t>(*streamIndex),
                                makeEntry(nid, Vcmd{*replacement}));
            syncNextEventId(song, nid);
            return maybe_extend_new_track(true);
        }

        // Mid-span: split the span and insert a continuation event with the vcmd.
        size_t eventIdx = *streamIndex;
        NspcEventId nid = nextEventId(song);
        const uint32_t beforeLen = location.row - span.startTick;
        const uint32_t tailLen = span.length - beforeLen;
        const uint8_t originalTicks = clampTicks(span.length);

        ensureDurationBeforeEvent(*ownerEvents, eventIdx, clampTicks(beforeLen), nid);

        size_t insertIdx = eventIdx + 1;
        insertDurationEvent(*ownerEvents, insertIdx, clampTicks(tailLen), nid);
        ++insertIdx;
        ownerEvents->insert(ownerEvents->begin() + static_cast<std::ptrdiff_t>(insertIdx),
                            makeEntry(nid, Vcmd{*replacement}));
        ++insertIdx;
        ownerEvents->insert(ownerEvents->begin() + static_cast<std::ptrdiff_t>(insertIdx),
                            makeEntry(nid, toEvent(continuationEvent(span.event))));

        restoreDurationBeforeNextTimedEvent(*ownerEvents, insertIdx + 1, originalTicks, nid);
        syncNextEventId(song, nid);
        return maybe_extend_new_track(true);
    }

    // Beyond all spans: fill gap and row anchor with continuation event.
    NspcTrack* track = resolveChannelTrack(song, location, true);
    if (!track) {
        return false;
    }

    uint32_t endTick = 0;
    NspcRowEvent fillEvent = Tie{};
    if (!spans.empty()) {
        const auto& last = spans.back();
        endTick = last.startTick + last.length;
        fillEvent = continuationEvent(last.event);
    }

    NspcEventId nid = nextEventId(song);
    size_t insertIndex = findEndIndex(track->events);

    if (location.row > endTick) {
        uint32_t gap = location.row - endTick;
        while (gap > 0) {
            const uint8_t chunk = clampTicks(gap);
            insertDurationEvent(track->events, insertIndex, chunk, nid);
            ++insertIndex;
            track->events.insert(track->events.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                                 makeEntry(nid, toEvent(fillEvent)));
            ++insertIndex;
            gap -= chunk;
        }
    }

    insertDurationEvent(track->events, insertIndex, 1, nid);
    ++insertIndex;
    track->events.insert(track->events.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                         makeEntry(nid, Vcmd{*replacement}));
    ++insertIndex;
    track->events.insert(track->events.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                         makeEntry(nid, toEvent(fillEvent)));

    if (track->events.empty() || !std::holds_alternative<End>(track->events.back().event)) {
        track->events.push_back(makeEntry(nid, End{}));
    }
    syncNextEventId(song, nid);
    return maybe_extend_new_track(true);
}

}  // namespace

bool NspcEditor::setPatternLength(NspcSong& song, int patternId, uint32_t targetTick) {
    auto finalize = [&](bool changed) -> bool {
        if (changed) {
            song.setContentOrigin(NspcContentOrigin::UserProvided);
        }
        return changed;
    };

    if (patternId < 0) {
        return finalize(false);
    }

    NspcPattern* pattern = findPatternById(song, patternId);
    if (!pattern) {
        return finalize(false);
    }

    bool changed = false;
    bool hasAssignedTrack = false;
    NspcEventId nextId = nextEventId(song);
    std::vector<int> targetTrackIds;

    if (pattern->channelTrackIds.has_value()) {
        for (int channel = 0; channel < 8; ++channel) {
            const int trackId = pattern->channelTrackIds.value()[static_cast<size_t>(channel)];
            if (trackId < 0) {
                continue;
            }
            hasAssignedTrack = true;

            const bool alreadyVisited =
                std::find(targetTrackIds.begin(), targetTrackIds.end(), trackId) != targetTrackIds.end();
            if (alreadyVisited) {
                continue;
            }
            targetTrackIds.push_back(trackId);
        }
    }

    for (const int trackId : targetTrackIds) {
        NspcTrack* track = resolveTrackById(song, trackId, true);
        if (!track) {
            continue;
        }
        if (trackHasSubroutineCalls(*track)) {
            return finalize(false);
        }
    }

    for (const int trackId : targetTrackIds) {
        NspcTrack* track = resolveTrackById(song, trackId, true);
        if (!track) {
            continue;
        }
        changed = setTrackLength(*track, targetTick, nextId) || changed;
    }

    if (!hasAssignedTrack && targetTick > 0) {
        NspcEditorLocation anchorLocation{
            .patternId = patternId,
            .channel = 0,
            .row = 0,
        };
        NspcTrack* track = resolveChannelTrack(song, anchorLocation, true);
        if (track) {
            if (trackHasSubroutineCalls(*track)) {
                return finalize(false);
            }
            changed = setTrackLength(*track, targetTick, nextId) || changed;
        }
    }

    syncNextEventId(song, nextId);
    return finalize(changed);
}

bool NspcEditor::insertTickAtRow(NspcSong& song, const NspcEditorLocation& location) {
    auto finalize = [&](bool changed) -> bool {
        if (changed) {
            song.setContentOrigin(NspcContentOrigin::UserProvided);
        }
        return changed;
    };

    NspcTrack* track = resolveChannelTrack(song, location, true);
    if (!track) {
        return finalize(false);
    }
    if (trackHasSubroutineCalls(*track)) {
        return finalize(false);
    }

    auto& events = track->events;
    const auto spans = collectTrackRowSpans(events);
    const auto targetSpanIndex = findTrackSpanAtRow(spans, location.row);
    NspcEventId nextId = nextEventId(song);
    bool changed = false;

    if (targetSpanIndex.has_value() && location.row > spans[*targetSpanIndex].startTick) {
        const auto& target = spans[*targetSpanIndex];
        const uint32_t beforeLen = location.row - target.startTick;
        const uint32_t tailLen = target.length - beforeLen;
        if (beforeLen == 0 || tailLen == 0) {
            return finalize(false);
        }

        size_t targetEventIndex = target.eventIndex;
        (void)ensureDurationBeforeEvent(events, targetEventIndex, clampTicks(beforeLen), nextId);
        if (targetEventIndex > 0) {
            if (auto* beforeDuration = std::get_if<Duration>(&events[targetEventIndex - 1].event)) {
                applyDurationState(*beforeDuration, target.duration, clampTicks(beforeLen));
            }
        }

        Duration tailDuration = target.duration;
        tailDuration.ticks = clampTicks(tailLen + 1);
        size_t insertIndex = targetEventIndex + 1;
        insertDurationEventWithState(events, insertIndex, tailDuration, nextId);
        ++insertIndex;
        events.insert(events.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                      makeEntry(nextId, toEvent(continuationEvent(target.event))));

        restoreDurationStateBeforeNextTimedEvent(events, insertIndex + 1, target.duration, nextId);
        changed = true;
    } else {
        if (targetSpanIndex.has_value() && location.row == spans[*targetSpanIndex].startTick) {
            const auto& target = spans[*targetSpanIndex];
            const NspcRowEvent fillEvent = continuationEventForInsertedRow(spans, location.row);
            size_t insertIndex = target.eventIndex;
            if (insertIndex > 0 && isDurationEvent(events[insertIndex - 1].event)) {
                --insertIndex;
            }

            Duration inserted{
                .ticks = 1,
                .quantization = std::nullopt,
                .velocity = std::nullopt,
            };
            insertDurationEventWithState(events, insertIndex, inserted, nextId);
            ++insertIndex;
            events.insert(events.begin() + static_cast<std::ptrdiff_t>(insertIndex), makeEntry(nextId, toEvent(fillEvent)));

            restoreDurationStateBeforeNextTimedEvent(events, insertIndex + 1, target.duration, nextId);
            changed = true;
        } else if (const auto insertPoint = findTrackInsertionPointAtRow(events, location.row); insertPoint.has_value()) {
            const NspcRowEvent fillEvent = continuationEventForInsertedRow(spans, location.row);
            size_t insertIndex = insertPoint->eventIndex;

            Duration inserted{
                .ticks = 1,
                .quantization = std::nullopt,
                .velocity = std::nullopt,
            };
            insertDurationEventWithState(events, insertIndex, inserted, nextId);
            ++insertIndex;
            events.insert(events.begin() + static_cast<std::ptrdiff_t>(insertIndex), makeEntry(nextId, toEvent(fillEvent)));

            restoreDurationStateBeforeNextTimedEvent(events, insertIndex + 1, insertPoint->duration, nextId);
            changed = true;
        } else {
            changed = extendChannelToTick(song, location, location.row + 1);
        }
    }

    syncNextEventId(song, nextId);
    return finalize(changed);
}

bool NspcEditor::removeTickAtRow(NspcSong& song, const NspcEditorLocation& location) {
    auto finalize = [&](bool changed) -> bool {
        if (changed) {
            song.setContentOrigin(NspcContentOrigin::UserProvided);
        }
        return changed;
    };

    NspcTrack* track = resolveChannelTrack(song, location, false);
    if (!track) {
        return finalize(false);
    }
    if (trackHasSubroutineCalls(*track)) {
        return finalize(false);
    }

    auto& events = track->events;
    bool changed = eraseTrackCommandsAtRow(events, location.row);

    uint32_t endTick = 0;
    auto spans = collectTrackRowSpans(events, &endTick);
    if (spans.empty()) {
        return finalize(changed);
    }

    auto targetSpanIndex = findTrackSpanAtRow(spans, location.row);
    if (!targetSpanIndex.has_value() && endTick > 0 && location.row == endTick) {
        targetSpanIndex = spans.size() - 1;
    }
    if (!targetSpanIndex.has_value()) {
        return finalize(changed);
    }

    const auto target = spans[*targetSpanIndex];
    NspcEventId nextId = nextEventId(song);

    if (target.length <= 1) {
        if (target.eventIndex >= events.size() || !isTimedRowEvent(events[target.eventIndex].event)) {
            return finalize(changed);
        }
        events.erase(events.begin() + static_cast<std::ptrdiff_t>(target.eventIndex));
        eraseOrphanDurationBeforeIndex(events, target.eventIndex);
        changed = true;
    } else {
        const uint32_t newLength = target.length - 1;
        size_t targetEventIndex = target.eventIndex;
        (void)ensureDurationBeforeEvent(events, targetEventIndex, clampTicks(newLength), nextId);
        if (targetEventIndex > 0) {
            if (auto* duration = std::get_if<Duration>(&events[targetEventIndex - 1].event)) {
                applyDurationState(*duration, target.duration, clampTicks(newLength));
            }
        }
        restoreDurationStateBeforeNextTimedEvent(events, targetEventIndex + 1, target.duration, nextId);
        changed = true;
    }

    syncNextEventId(song, nextId);
    return finalize(changed);
}

bool NspcEditor::setRowEvent(NspcSong& song, const NspcEditorLocation& location, const NspcRowEvent& event) {
    const bool hadAssignedTrackBefore = channelHasAssignedTrack(song, location);
    const std::optional<uint32_t> baselineEndTick =
        hadAssignedTrackBefore ? std::nullopt : patternEndTick(song, location.patternId);
    auto maybe_extend_new_track = [&](bool result) -> bool {
        if (!result || hadAssignedTrackBefore || !baselineEndTick.has_value()) {
            return result;
        }
        if (*baselineEndTick > location.row) {
            (void)extendChannelToTick(song, location, *baselineEndTick);
        }
        return result;
    };
    auto finalize = [&](bool changed) -> bool {
        if (changed) {
            song.setContentOrigin(NspcContentOrigin::UserProvided);
        }
        return changed;
    };

    NspcTrack* channelTrack = resolveChannelTrack(song, location, true);
    if (!channelTrack) {
        return finalize(false);
    }

    const auto channel = flattenChannel(song, location);
    if (!channel.has_value()) {
        return finalize(false);
    }

    std::vector<RowSpan> spans = collectRowSpans(*channel);
    const auto targetIndex = findTargetSpanIndex(spans, location.row);

    if (!targetIndex.has_value()) {
        if (const auto subroutineId = findSubroutineCallIdAtRow(*channel, location.row); subroutineId.has_value()) {
            return finalize(maybe_extend_new_track(setFirstTimedEventInSubroutine(song, *subroutineId, event)));
        }

        uint32_t endTick = 0;
        NspcRowEvent fillEvent = Tie{};
        if (!spans.empty()) {
            const auto& last = spans.back();
            endTick = last.startTick + last.length;
            fillEvent = continuationEvent(last.event);
        }
        if (location.row < endTick) {
            return finalize(false);
        }

        uint32_t gap = location.row - endTick;
        NspcEventId nextId = nextEventId(song);
        size_t insertIndex = findEndIndex(channelTrack->events);

        while (gap > 0) {
            const uint8_t chunk = clampTicks(gap);
            insertDurationEvent(channelTrack->events, insertIndex, chunk, nextId);
            ++insertIndex;
            channelTrack->events.insert(channelTrack->events.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                                        makeEntry(nextId, toEvent(fillEvent)));
            ++insertIndex;
            gap -= chunk;
        }

        insertDurationEvent(channelTrack->events, insertIndex, 1, nextId);
        ++insertIndex;
        channelTrack->events.insert(channelTrack->events.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                                    makeEntry(nextId, toEvent(event)));

        if (channelTrack->events.empty() || !std::holds_alternative<End>(channelTrack->events.back().event)) {
            channelTrack->events.push_back(makeEntry(nextId, End{}));
        }
        syncNextEventId(song, nextId);
        const bool changed = maybe_extend_new_track(true);
        if (changed) {
            (void)compactContinuationAtRow(song, location);
        }
        return finalize(changed);
    }

    const RowSpan& target = spans[*targetIndex];
    auto* ownerEvents = resolveOwnerEvents(song, target.source.owner, target.source.ownerId);
    if (!ownerEvents) {
        return finalize(false);
    }

    auto streamIndex = resolveEventIndex(*ownerEvents, target.source);
    if (!streamIndex.has_value()) {
        return finalize(false);
    }

    if (location.row == target.startTick) {
        ownerEvents->at(*streamIndex).event = toEvent(event);
        const bool changed = maybe_extend_new_track(true);
        if (changed) {
            (void)compactContinuationAtRow(song, location);
        }
        return finalize(changed);
    }

    if (sameRowEvent(event, continuationEvent(target.event))) {
        return finalize(false);
    }

    const uint32_t beforeLen = location.row - target.startTick;
    const uint32_t tailLen = target.length - beforeLen;
    if (beforeLen == 0 || tailLen == 0) {
        return finalize(false);
    }

    size_t targetEventIndex = *streamIndex;
    NspcEventId nextId = nextEventId(song);
    const uint8_t originalTicks = clampTicks(target.length);
    (void)ensureDurationBeforeEvent(*ownerEvents, targetEventIndex, clampTicks(beforeLen), nextId);

    size_t insertIndex = targetEventIndex + 1;
    insertDurationEvent(*ownerEvents, insertIndex, clampTicks(tailLen), nextId);
    ++insertIndex;
    ownerEvents->insert(ownerEvents->begin() + static_cast<std::ptrdiff_t>(insertIndex),
                        makeEntry(nextId, toEvent(event)));

    restoreDurationBeforeNextTimedEvent(*ownerEvents, insertIndex + 1, originalTicks, nextId);
    syncNextEventId(song, nextId);
    const bool changed = maybe_extend_new_track(true);
    if (changed) {
        (void)compactContinuationAtRow(song, location);
    }
    return finalize(changed);
}

bool NspcEditor::deleteRowEvent(NspcSong& song, const NspcEditorLocation& location) {
    auto finalize = [&](bool changed) -> bool {
        if (changed) {
            song.setContentOrigin(NspcContentOrigin::UserProvided);
        }
        return changed;
    };

    const auto channel = flattenChannel(song, location);
    if (!channel.has_value()) {
        return finalize(false);
    }

    std::vector<RowSpan> spans = collectRowSpans(*channel);
    const auto targetIndex = findTargetSpanIndex(spans, location.row);
    if (!targetIndex.has_value()) {
        return finalize(false);
    }

    const RowSpan& target = spans[*targetIndex];
    if (location.row != target.startTick) {
        // Continuation rows are implicit; deleting them should not rewrite the anchor event.
        return finalize(false);
    }

    auto* targetEvents = resolveOwnerEvents(song, target.source.owner, target.source.ownerId);
    if (!targetEvents) {
        return finalize(false);
    }
    auto targetStreamIndex = resolveEventIndex(*targetEvents, target.source);
    if (!targetStreamIndex.has_value()) {
        return finalize(false);
    }

    const auto previousIndex = findPreviousSpanIndex(spans, *targetIndex);

    // Ties are special:
    // - Keep first-row tie anchors (no previous row event) intact.
    // - Keep auto continuation ties that were introduced by max-duration overflow intact.
    // - Otherwise deleting a tie removes the boundary by merging it into the previous span.
    if (std::holds_alternative<Tie>(toEvent(target.event))) {
        if (!previousIndex.has_value()) {
            return finalize(false);
        }

        const RowSpan& previous = spans[*previousIndex];
        const bool previousContinuesAsTie = sameRowEvent(continuationEvent(previous.event), Tie{});
        const bool overflowContinuationTie = previousContinuesAsTie && previous.length >= 0xFF;
        if (overflowContinuationTie) {
            return finalize(false);
        }

        return finalize(mergeSpanIntoPrevious(song, previous, target, false));
    }

    if (!previousIndex.has_value()) {
        // First-row deletes should become Tie so they read as neutral continuation.
        if (sameRowEvent(target.event, Tie{})) {
            return finalize(false);
        }
        targetEvents->at(*targetStreamIndex).event = Tie{};
        return finalize(true);
    }

    const RowSpan& previous = spans[*previousIndex];

    if (mergeSpanIntoPrevious(song, previous, target, false)) {
        return finalize(true);
    }

    targetEvents->at(*targetStreamIndex).event = toEvent(continuationEvent(previous.event));
    NspcEditorLocation compactLocation = location;
    compactLocation.row = target.startTick;
    (void)compactContinuationAtRow(song, compactLocation);
    return finalize(true);
}

bool NspcEditor::setInstrumentAtRow(NspcSong& song, const NspcEditorLocation& location,
                                    std::optional<uint8_t> instrument) {
    std::optional<Vcmd> replacement = std::nullopt;
    if (instrument.has_value()) {
        replacement = Vcmd{VcmdInst{.instrumentIndex = *instrument}};
    }
    const bool changed = editCommandAtRow(song, location, replacement, isInstrumentCommand);
    if (changed) {
        song.setContentOrigin(NspcContentOrigin::UserProvided);
    }
    return changed;
}

bool NspcEditor::setVolumeAtRow(NspcSong& song, const NspcEditorLocation& location, std::optional<uint8_t> volume) {
    std::optional<Vcmd> replacement = std::nullopt;
    if (volume.has_value()) {
        replacement = Vcmd{VcmdVolume{.volume = *volume}};
    }
    const bool changed = editCommandAtRow(song, location, replacement, isVolumeCommand);
    if (changed) {
        song.setContentOrigin(NspcContentOrigin::UserProvided);
    }
    return changed;
}

bool NspcEditor::setQvAtRow(NspcSong& song, const NspcEditorLocation& location, std::optional<uint8_t> qv) {
    const bool hadAssignedTrackBefore = channelHasAssignedTrack(song, location);
    const std::optional<uint32_t> baselineEndTick =
        hadAssignedTrackBefore ? std::nullopt : patternEndTick(song, location.patternId);
    auto maybe_extend_new_track = [&](bool result) -> bool {
        if (!result || hadAssignedTrackBefore || !baselineEndTick.has_value()) {
            return result;
        }
        if (*baselineEndTick > location.row) {
            (void)extendChannelToTick(song, location, *baselineEndTick);
        }
        return result;
    };

    auto finalize = [&](bool changed) -> bool {
        if (changed) {
            song.setContentOrigin(NspcContentOrigin::UserProvided);
        }
        return changed;
    };

    const auto channel = flattenChannel(song, location);
    if (!channel.has_value()) {
        return finalize(false);
    }

    const auto spans = collectRowSpans(*channel);
    const auto targetIndex = findTargetSpanIndex(spans, location.row);
    if (!targetIndex.has_value()) {
        if (!qv.has_value()) {
            return finalize(false);
        }

        NspcTrack* track = resolveChannelTrack(song, location, true);
        if (!track) {
            return finalize(false);
        }

        uint32_t endTick = 0;
        NspcRowEvent fillEvent = Tie{};
        if (!spans.empty()) {
            const auto& last = spans.back();
            endTick = last.startTick + last.length;
            fillEvent = continuationEvent(last.event);
        }

        NspcEventId nid = nextEventId(song);
        size_t insertIndex = findEndIndex(track->events);

        if (location.row > endTick) {
            uint32_t gap = location.row - endTick;
            while (gap > 0) {
                const uint8_t chunk = clampTicks(gap);
                insertDurationEvent(track->events, insertIndex, chunk, nid);
                ++insertIndex;
                track->events.insert(track->events.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                                     makeEntry(nid, toEvent(fillEvent)));
                ++insertIndex;
                gap -= chunk;
            }
        }

        insertDurationEvent(track->events, insertIndex, 1, nid);
        auto* insertedDuration = std::get_if<Duration>(&track->events[insertIndex].event);
        if (!insertedDuration) {
            return finalize(false);
        }
        insertedDuration->quantization = static_cast<uint8_t>((*qv >> 4) & 0x07);
        insertedDuration->velocity = static_cast<uint8_t>(*qv & 0x0F);

        ++insertIndex;
        track->events.insert(track->events.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                             makeEntry(nid, toEvent(fillEvent)));

        if (track->events.empty() || !std::holds_alternative<End>(track->events.back().event)) {
            track->events.push_back(makeEntry(nid, End{}));
        }

        syncNextEventId(song, nid);
        return finalize(maybe_extend_new_track(true));
    }

    const RowSpan& target = spans[*targetIndex];
    auto* ownerEvents = resolveOwnerEvents(song, target.source.owner, target.source.ownerId);
    if (!ownerEvents) {
        return finalize(false);
    }

    auto rowEventIndex = resolveEventIndex(*ownerEvents, target.source);
    if (!rowEventIndex.has_value()) {
        return finalize(false);
    }

    if (location.row != target.startTick) {
        // Continuation rows should not rewrite the anchor note's QV.
        if (!qv.has_value()) {
            return finalize(false);
        }

        const uint32_t beforeLen = location.row - target.startTick;
        const uint32_t tailLen = target.length - beforeLen;
        if (beforeLen == 0 || tailLen == 0) {
            return finalize(false);
        }

        size_t targetEventIndex = *rowEventIndex;
        NspcEventId nid = nextEventId(song);
        const uint8_t originalTicks = clampTicks(target.length);
        (void)ensureDurationBeforeEvent(*ownerEvents, targetEventIndex, clampTicks(beforeLen), nid);

        size_t insertIndex = targetEventIndex + 1;
        insertDurationEvent(*ownerEvents, insertIndex, clampTicks(tailLen), nid);
        auto* splitDuration = std::get_if<Duration>(&ownerEvents->at(insertIndex).event);
        if (!splitDuration) {
            return finalize(false);
        }
        splitDuration->quantization = static_cast<uint8_t>((*qv >> 4) & 0x07);
        splitDuration->velocity = static_cast<uint8_t>(*qv & 0x0F);

        ++insertIndex;
        ownerEvents->insert(ownerEvents->begin() + static_cast<std::ptrdiff_t>(insertIndex),
                            makeEntry(nid, toEvent(continuationEvent(target.event))));

        restoreDurationBeforeNextTimedEvent(*ownerEvents, insertIndex + 1, originalTicks, nid);
        syncNextEventId(song, nid);
        return finalize(maybe_extend_new_track(true));
    }

    bool insertedDuration = false;
    size_t durationIndex = 0;
    if (*rowEventIndex > 0 && std::holds_alternative<Duration>(ownerEvents->at(*rowEventIndex - 1).event)) {
        durationIndex = *rowEventIndex - 1;
    } else {
        NspcEventId nid = nextEventId(song);
        insertDurationEvent(*ownerEvents, *rowEventIndex, clampTicks(target.length), nid);
        syncNextEventId(song, nid);
        insertedDuration = true;
        durationIndex = *rowEventIndex;
    }

    auto* duration = std::get_if<Duration>(&ownerEvents->at(durationIndex).event);
    if (!duration) {
        return finalize(false);
    }

    bool changed = insertedDuration;
    if (qv.has_value()) {
        const uint8_t quant = static_cast<uint8_t>((*qv >> 4) & 0x07);
        const uint8_t velocity = static_cast<uint8_t>(*qv & 0x0F);
        if (!duration->quantization.has_value() || !duration->velocity.has_value() || *duration->quantization != quant ||
            *duration->velocity != velocity) {
            duration->quantization = quant;
            duration->velocity = velocity;
            changed = true;
        }
    } else {
        if (duration->quantization.has_value() || duration->velocity.has_value()) {
            duration->quantization.reset();
            duration->velocity.reset();
            changed = true;
        }
    }
    return finalize(maybe_extend_new_track(changed));
}

bool NspcEditor::setEffectAtRow(NspcSong& song, const NspcEditorLocation& location, const Vcmd& effect) {
    const bool changed = editCommandAtRow(song, location, effect, isEffectCommand);
    if (changed) {
        song.setContentOrigin(NspcContentOrigin::UserProvided);
    }
    return changed;
}

bool NspcEditor::addEffectAtRow(NspcSong& song, const NspcEditorLocation& location, const Vcmd& effect) {
    auto finalize = [&](bool changed) -> bool {
        if (changed) {
            song.setContentOrigin(NspcContentOrigin::UserProvided);
        }
        return changed;
    };

    const auto channel = flattenChannel(song, location);
    if (!channel.has_value()) {
        return finalize(false);
    }

    auto commandRefs = collectCommandRefsAtRow(*channel, location.row, isEffectCommand);
    if (!commandRefs.empty()) {
        auto resolved = resolveRefs(song, commandRefs);
        NspcEventId nid = nextEventId(song);
        const bool appended = appendResolvedRef(std::move(resolved), effect, nid);
        syncNextEventId(song, nid);
        return finalize(appended);
    }

    return finalize(editCommandAtRow(song, location, Vcmd{effect}, isEffectCommand));
}

bool NspcEditor::clearEffectsAtRow(NspcSong& song, const NspcEditorLocation& location, bool preserveSubroutineCalls) {
    const bool changed =
        editCommandAtRow(song, location, std::nullopt, isEffectCommand, !preserveSubroutineCalls);
    if (changed) {
        song.setContentOrigin(NspcContentOrigin::UserProvided);
    }
    return changed;
}

bool NspcEditor::createSubroutineFromRowRange(NspcSong& song, const NspcEditorLocation& location, uint32_t startRow,
                                              uint32_t endRow) {
    auto finalize = [&](bool changed) -> bool {
        if (changed) {
            song.setContentOrigin(NspcContentOrigin::UserProvided);
        }
        return changed;
    };

    NspcTrack* track = resolveChannelTrack(song, location, false);
    if (!track) {
        return finalize(false);
    }

    const auto slice = resolveTrackSliceForRowRange(song, location, startRow, endRow, track->id);
    if (!slice.has_value()) {
        return finalize(false);
    }

    const auto [sliceStart, sliceEndExclusive] = *slice;
    if (sliceStart >= track->events.size() || sliceEndExclusive > track->events.size() || sliceStart >= sliceEndExclusive) {
        return finalize(false);
    }

    for (size_t i = sliceStart; i < sliceEndExclusive; ++i) {
        if (isEndEvent(track->events[i].event)) {
            return finalize(false);
        }
    }

    NspcEventId nextId = nextEventId(song);
    std::vector<NspcEventEntry> subroutineBody;
    subroutineBody.reserve(sliceEndExclusive - sliceStart + 1);
    for (size_t i = sliceStart; i < sliceEndExclusive; ++i) {
        subroutineBody.push_back(cloneEntryWithNewId(track->events[i], nextId));
    }
    subroutineBody.push_back(makeEntry(nextId, End{}));

    auto& subroutines = song.subroutines();
    const int newSubroutineId = static_cast<int>(subroutines.size());
    subroutines.push_back(NspcSubroutine{
        .id = newSubroutineId,
        .events = std::move(subroutineBody),
        .originalAddr = 0,
    });

    NspcEventEntry callEntry = makeEntry(nextId, Vcmd{VcmdSubroutineCall{
                                                 .subroutineId = newSubroutineId,
                                                 .originalAddr = 0,
                                                 .count = 1,
                                             }});
    track->events.erase(track->events.begin() + static_cast<std::ptrdiff_t>(sliceStart),
                        track->events.begin() + static_cast<std::ptrdiff_t>(sliceEndExclusive));
    track->events.insert(track->events.begin() + static_cast<std::ptrdiff_t>(sliceStart), std::move(callEntry));

    syncNextEventId(song, nextId);
    return finalize(true);
}

bool NspcEditor::flattenSubroutineOnChannel(NspcSong& song, const NspcEditorLocation& location, int subroutineId) {
    auto finalize = [&](bool changed) -> bool {
        if (changed) {
            song.setContentOrigin(NspcContentOrigin::UserProvided);
        }
        return changed;
    };

    if (subroutineId < 0) {
        return finalize(false);
    }

    NspcTrack* track = resolveChannelTrack(song, location, false);
    const NspcSubroutine* targetSubroutine = findSubroutineById(song, subroutineId);
    if (!track || !targetSubroutine) {
        return finalize(false);
    }

    NspcEventId nextId = nextEventId(song);
    const bool changed =
        inlineSubroutineCallsInEvents(track->events, subroutineId, targetSubroutine->events, nextId);
    syncNextEventId(song, nextId);
    return finalize(changed);
}

bool NspcEditor::deleteSubroutine(NspcSong& song, int subroutineId) {
    auto finalize = [&](bool changed) -> bool {
        if (changed) {
            song.setContentOrigin(NspcContentOrigin::UserProvided);
        }
        return changed;
    };

    if (subroutineId < 0) {
        return finalize(false);
    }

    const NspcSubroutine* targetSubroutine = findSubroutineById(song, subroutineId);
    if (!targetSubroutine) {
        return finalize(false);
    }

    // Copy subroutine events locally so we don't rely on a pointer into song.subroutines()
    // during the inlining loops (which mutate the vector's elements).
    const std::vector<NspcEventEntry> targetEvents = targetSubroutine->events;

    NspcEventId nextId = nextEventId(song);
    bool changed = false;

    for (auto& track : song.tracks()) {
        changed = inlineSubroutineCallsInEvents(track.events, subroutineId, targetEvents, nextId) || changed;
    }

    for (auto& subroutine : song.subroutines()) {
        if (subroutine.id == subroutineId) {
            continue;
        }
        changed = inlineSubroutineCallsInEvents(subroutine.events, subroutineId, targetEvents, nextId) || changed;
    }

    const auto& existingSubroutines = song.subroutines();
    std::vector<NspcSubroutine> reindexedSubroutines;
    reindexedSubroutines.reserve(existingSubroutines.size());
    std::unordered_map<int, int> idRemap;
    std::unordered_map<int, uint16_t> addrBySubroutineId;

    for (const auto& subroutine : existingSubroutines) {
        if (subroutine.id == subroutineId) {
            changed = true;
            continue;
        }

        NspcSubroutine copy = subroutine;
        const int newId = static_cast<int>(reindexedSubroutines.size());
        copy.id = newId;
        idRemap[subroutine.id] = newId;
        addrBySubroutineId[newId] = copy.originalAddr;
        reindexedSubroutines.push_back(std::move(copy));
        changed = true;
    }

    if (!changed) {
        return finalize(false);
    }

    song.subroutines() = std::move(reindexedSubroutines);

    for (auto& track : song.tracks()) {
        remapSubroutineCallIds(track.events, idRemap, addrBySubroutineId);
    }
    for (auto& subroutine : song.subroutines()) {
        remapSubroutineCallIds(subroutine.events, idRemap, addrBySubroutineId);
    }

    syncNextEventId(song, nextId);
    return finalize(true);
}

}  // namespace ntrak::nspc
