#include "ntrak/ui/ControlPanel.hpp"

#include "ntrak/app/App.hpp"
#include "ntrak/nspc/NspcCompile.hpp"
#include "ntrak/nspc/NspcFlatten.hpp"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <format>
#include <memory>
#include <numeric>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace ntrak::ui {
namespace {

struct TriggerSequenceState {
    std::unordered_map<int, uint8_t> jumpTimesRemaining;
};

struct SequenceAdvanceResult {
    int row = -1;
    bool reachedEnd = false;
};

using OrderedVcmdKey = uint16_t;
using OrderedVcmdState = std::vector<std::pair<OrderedVcmdKey, nspc::Vcmd>>;

constexpr OrderedVcmdKey kExtensionStateKeyBase = 0x100;

struct PlaybackSetupState {
    std::array<OrderedVcmdState, 8> channel{};
    OrderedVcmdState global{};
    std::array<std::optional<nspc::Duration>, 8> channelDuration{};
    std::array<std::optional<uint8_t>, 8> channelQv{};
};

[[nodiscard]] std::string lowerAsciiCopy(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return out;
}

[[nodiscard]] bool containsIgnoreCase(std::string_view haystack, std::string_view needle) {
    const std::string loweredHaystack = lowerAsciiCopy(haystack);
    const std::string loweredNeedle = lowerAsciiCopy(needle);
    return loweredHaystack.find(loweredNeedle) != std::string::npos;
}

[[nodiscard]] std::unordered_set<uint8_t> collectLegatoExtensionIds(const nspc::NspcEngineConfig& engineConfig) {
    std::unordered_set<uint8_t> ids;
    for (const auto& extension : engineConfig.extensions) {
        const bool extensionLooksLegato =
            containsIgnoreCase(extension.name, "legato") || containsIgnoreCase(extension.description, "legato");
        for (const auto& vcmd : extension.vcmds) {
            if (!extensionLooksLegato && !containsIgnoreCase(vcmd.name, "legato") &&
                !containsIgnoreCase(vcmd.description, "legato")) {
                continue;
            }
            ids.insert(vcmd.id);
        }
    }
    return ids;
}

std::optional<int> resolveSequenceTargetIndex(const nspc::SequenceTarget& target, size_t sequenceSize) {
    if (!target.index.has_value()) {
        return std::nullopt;
    }
    const int index = *target.index;
    if (index < 0 || index >= static_cast<int>(sequenceSize)) {
        return std::nullopt;
    }
    return index;
}

std::optional<int> patternIdFromSequenceRow(const nspc::NspcSong& song, int sequenceRow) {
    const auto& sequence = song.sequence();
    if (sequenceRow < 0 || sequenceRow >= static_cast<int>(sequence.size())) {
        return std::nullopt;
    }
    if (const auto* play = std::get_if<nspc::PlayPattern>(&sequence[static_cast<size_t>(sequenceRow)])) {
        return play->patternId;
    }
    return std::nullopt;
}

std::optional<int> findSequenceRowForPatternId(const nspc::NspcSong& song, int patternId, int preferredRow) {
    const auto& sequence = song.sequence();
    if (preferredRow >= 0 && preferredRow < static_cast<int>(sequence.size())) {
        if (const auto* play = std::get_if<nspc::PlayPattern>(&sequence[static_cast<size_t>(preferredRow)])) {
            if (play->patternId == patternId) {
                return preferredRow;
            }
        }
    }

    for (int row = 0; row < static_cast<int>(sequence.size()); ++row) {
        if (const auto* play = std::get_if<nspc::PlayPattern>(&sequence[static_cast<size_t>(row)])) {
            if (play->patternId == patternId) {
                return row;
            }
        }
    }
    return std::nullopt;
}

void preserveSelectionFromPlayback(app::AppState& appState) {
    if (!appState.project.has_value()) {
        return;
    }

    auto& songs = appState.project->songs();
    if (appState.selectedSongIndex < 0 || appState.selectedSongIndex >= static_cast<int>(songs.size())) {
        return;
    }

    const auto& song = songs[static_cast<size_t>(appState.selectedSongIndex)];
    const int liveRow = appState.playback.sequenceRow.load(std::memory_order_relaxed);
    const int livePattern = appState.playback.patternId.load(std::memory_order_relaxed);

    std::optional<int> resolvedPattern = patternIdFromSequenceRow(song, liveRow);
    if (!resolvedPattern.has_value() && livePattern >= 0) {
        const bool patternExists = std::any_of(song.patterns().begin(), song.patterns().end(),
                                               [livePattern](const nspc::NspcPattern& pattern) {
                                                   return pattern.id == livePattern;
                                               });
        if (patternExists) {
            resolvedPattern = livePattern;
        }
    }

    if (!resolvedPattern.has_value()) {
        return;
    }

    appState.selectedPatternId = *resolvedPattern;
    if (const auto row = findSequenceRowForPatternId(song, *resolvedPattern, liveRow); row.has_value()) {
        appState.selectedSequenceRow = *row;
    }
}

std::optional<int> patternIdFromSequenceRow(const std::vector<nspc::NspcSequenceOp>& sequence, int sequenceRow) {
    if (sequenceRow < 0 || sequenceRow >= static_cast<int>(sequence.size())) {
        return std::nullopt;
    }
    if (const auto* play = std::get_if<nspc::PlayPattern>(&sequence[static_cast<size_t>(sequenceRow)])) {
        return play->patternId;
    }
    return std::nullopt;
}

SequenceAdvanceResult advanceSequenceRowFromPatternTrigger(const std::vector<nspc::NspcSequenceOp>& sequence,
                                                           int currentRow, TriggerSequenceState& state) {
    if (sequence.empty()) {
        return SequenceAdvanceResult{.row = -1, .reachedEnd = true};
    }

    const int maxRow = static_cast<int>(sequence.size()) - 1;
    int nextRow = currentRow + 1;
    if (nextRow < 0) {
        nextRow = 0;
    }
    if (nextRow > maxRow) {
        return SequenceAdvanceResult{.row = std::clamp(currentRow, 0, maxRow), .reachedEnd = true};
    }

    const int maxHops = std::max(16, static_cast<int>(sequence.size()) * 8);
    for (int hop = 0; hop < maxHops; ++hop) {
        if (nextRow < 0 || nextRow > maxRow) {
            return SequenceAdvanceResult{.row = std::clamp(currentRow, 0, maxRow), .reachedEnd = true};
        }

        const auto& op = sequence[static_cast<size_t>(nextRow)];
        if (std::holds_alternative<nspc::PlayPattern>(op)) {
            return SequenceAdvanceResult{.row = nextRow, .reachedEnd = false};
        }
        if (const auto* alwaysJump = std::get_if<nspc::AlwaysJump>(&op)) {
            if (const auto target = resolveSequenceTargetIndex(alwaysJump->target, sequence.size()); target.has_value()) {
                nextRow = *target;
            } else {
                ++nextRow;
            }
            continue;
        }
        if (const auto* jumpTimes = std::get_if<nspc::JumpTimes>(&op)) {
            const int jumpRow = nextRow;
            auto [it, inserted] = state.jumpTimesRemaining.emplace(jumpRow, jumpTimes->count);
            if (it->second > 0) {
                --(it->second);
                if (const auto target = resolveSequenceTargetIndex(jumpTimes->target, sequence.size());
                    target.has_value()) {
                    nextRow = *target;
                    continue;
                }
            }
            state.jumpTimesRemaining.erase(jumpRow);
            nextRow = jumpRow + 1;
            continue;
        }
        if (std::holds_alternative<nspc::EndSequence>(op)) {
            return SequenceAdvanceResult{.row = std::clamp(currentRow, 0, maxRow), .reachedEnd = true};
        }

        // Fast-forward flags and unknown/non-playback op: skip forward.
        ++nextRow;
    }

    return SequenceAdvanceResult{.row = std::clamp(currentRow, 0, maxRow), .reachedEnd = false};
}

std::optional<uint8_t> seedableVcmdOpcode(const nspc::Vcmd& cmd) {
    return std::visit(
        nspc::overloaded{
            [](const std::monostate&) -> std::optional<uint8_t> { return std::nullopt; },
            [](const nspc::VcmdInst&) -> std::optional<uint8_t> { return nspc::VcmdInst::id; },
            [](const nspc::VcmdPanning&) -> std::optional<uint8_t> { return nspc::VcmdPanning::id; },
            [](const nspc::VcmdPanFade&) -> std::optional<uint8_t> { return nspc::VcmdPanFade::id; },
            [](const nspc::VcmdVibratoOn&) -> std::optional<uint8_t> { return nspc::VcmdVibratoOn::id; },
            [](const nspc::VcmdVibratoOff&) -> std::optional<uint8_t> { return nspc::VcmdVibratoOff::id; },
            [](const nspc::VcmdGlobalVolume&) -> std::optional<uint8_t> { return nspc::VcmdGlobalVolume::id; },
            [](const nspc::VcmdGlobalVolumeFade&) -> std::optional<uint8_t> { return nspc::VcmdGlobalVolumeFade::id; },
            [](const nspc::VcmdTempo&) -> std::optional<uint8_t> { return nspc::VcmdTempo::id; },
            [](const nspc::VcmdTempoFade&) -> std::optional<uint8_t> { return nspc::VcmdTempoFade::id; },
            [](const nspc::VcmdGlobalTranspose&) -> std::optional<uint8_t> { return nspc::VcmdGlobalTranspose::id; },
            [](const nspc::VcmdPerVoiceTranspose&) -> std::optional<uint8_t> {
                return nspc::VcmdPerVoiceTranspose::id;
            },
            [](const nspc::VcmdTremoloOn&) -> std::optional<uint8_t> { return nspc::VcmdTremoloOn::id; },
            [](const nspc::VcmdTremoloOff&) -> std::optional<uint8_t> { return nspc::VcmdTremoloOff::id; },
            [](const nspc::VcmdVolume&) -> std::optional<uint8_t> { return nspc::VcmdVolume::id; },
            [](const nspc::VcmdVolumeFade&) -> std::optional<uint8_t> { return nspc::VcmdVolumeFade::id; },
            [](const nspc::VcmdSubroutineCall&) -> std::optional<uint8_t> { return std::nullopt; },
            [](const nspc::VcmdVibratoFadeIn&) -> std::optional<uint8_t> { return nspc::VcmdVibratoFadeIn::id; },
            [](const nspc::VcmdPitchEnvelopeTo&) -> std::optional<uint8_t> { return nspc::VcmdPitchEnvelopeTo::id; },
            [](const nspc::VcmdPitchEnvelopeFrom&) -> std::optional<uint8_t> {
                return nspc::VcmdPitchEnvelopeFrom::id;
            },
            [](const nspc::VcmdPitchEnvelopeOff&) -> std::optional<uint8_t> { return nspc::VcmdPitchEnvelopeOff::id; },
            [](const nspc::VcmdFineTune&) -> std::optional<uint8_t> { return nspc::VcmdFineTune::id; },
            [](const nspc::VcmdEchoOn&) -> std::optional<uint8_t> { return nspc::VcmdEchoOn::id; },
            [](const nspc::VcmdEchoOff&) -> std::optional<uint8_t> { return nspc::VcmdEchoOff::id; },
            [](const nspc::VcmdEchoParams&) -> std::optional<uint8_t> { return nspc::VcmdEchoParams::id; },
            [](const nspc::VcmdEchoVolumeFade&) -> std::optional<uint8_t> { return nspc::VcmdEchoVolumeFade::id; },
            [](const nspc::VcmdPitchSlideToNote&) -> std::optional<uint8_t> { return nspc::VcmdPitchSlideToNote::id; },
            [](const nspc::VcmdPercussionBaseInstrument&) -> std::optional<uint8_t> {
                return nspc::VcmdPercussionBaseInstrument::id;
            },
            [](const nspc::VcmdNOP&) -> std::optional<uint8_t> { return std::nullopt; },
            [](const nspc::VcmdMuteChannel&) -> std::optional<uint8_t> { return std::nullopt; },
            [](const nspc::VcmdFastForwardOn&) -> std::optional<uint8_t> { return std::nullopt; },
            [](const nspc::VcmdFastForwardOff&) -> std::optional<uint8_t> { return std::nullopt; },
            [](const nspc::VcmdUnused&) -> std::optional<uint8_t> { return std::nullopt; },
            [](const nspc::VcmdExtension&) -> std::optional<uint8_t> { return std::nullopt; },
        },
        cmd.vcmd);
}

std::optional<nspc::Vcmd> normalizeSetupVcmd(const nspc::Vcmd& cmd,
                                             const std::unordered_set<uint8_t>& legatoExtensionIds) {
    return std::visit(
        nspc::overloaded{
            [](const std::monostate&) -> std::optional<nspc::Vcmd> { return std::nullopt; },
            [](const nspc::VcmdInst& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdPanning& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdPanFade& v) -> std::optional<nspc::Vcmd> {
                return nspc::Vcmd{nspc::VcmdPanning{.panning = v.target}};
            },
            [](const nspc::VcmdVibratoOn& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdVibratoOff& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdGlobalVolume& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdGlobalVolumeFade& v) -> std::optional<nspc::Vcmd> {
                return nspc::Vcmd{nspc::VcmdGlobalVolume{.volume = v.target}};
            },
            [](const nspc::VcmdTempo& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdTempoFade& v) -> std::optional<nspc::Vcmd> {
                return nspc::Vcmd{nspc::VcmdTempo{.tempo = v.target}};
            },
            [](const nspc::VcmdGlobalTranspose& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdPerVoiceTranspose& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdTremoloOn& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdTremoloOff& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdVolume& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdVolumeFade& v) -> std::optional<nspc::Vcmd> {
                return nspc::Vcmd{nspc::VcmdVolume{.volume = v.target}};
            },
            [](const nspc::VcmdSubroutineCall&) -> std::optional<nspc::Vcmd> { return std::nullopt; },
            [](const nspc::VcmdVibratoFadeIn&) -> std::optional<nspc::Vcmd> { return std::nullopt; },
            [](const nspc::VcmdPitchEnvelopeTo& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdPitchEnvelopeFrom& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdPitchEnvelopeOff& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdFineTune& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdEchoOn& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdEchoOff& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdEchoParams& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdEchoVolumeFade&) -> std::optional<nspc::Vcmd> { return std::nullopt; },
            [](const nspc::VcmdPitchSlideToNote&) -> std::optional<nspc::Vcmd> { return std::nullopt; },
            [](const nspc::VcmdPercussionBaseInstrument& v) -> std::optional<nspc::Vcmd> { return nspc::Vcmd{v}; },
            [](const nspc::VcmdNOP&) -> std::optional<nspc::Vcmd> { return std::nullopt; },
            [](const nspc::VcmdMuteChannel&) -> std::optional<nspc::Vcmd> { return std::nullopt; },
            [](const nspc::VcmdFastForwardOn&) -> std::optional<nspc::Vcmd> { return std::nullopt; },
            [](const nspc::VcmdFastForwardOff&) -> std::optional<nspc::Vcmd> { return std::nullopt; },
            [](const nspc::VcmdUnused&) -> std::optional<nspc::Vcmd> { return std::nullopt; },
            [&legatoExtensionIds](const nspc::VcmdExtension& v) -> std::optional<nspc::Vcmd> {
                if (!legatoExtensionIds.contains(v.id)) {
                    return std::nullopt;
                }
                return nspc::Vcmd{v};
            },
        },
        cmd.vcmd);
}

bool isGlobalStateOpcode(uint8_t opcode) {
    switch (opcode) {
    case nspc::VcmdGlobalVolume::id:
    case nspc::VcmdGlobalVolumeFade::id:
    case nspc::VcmdTempo::id:
    case nspc::VcmdTempoFade::id:
    case nspc::VcmdGlobalTranspose::id:
    case nspc::VcmdEchoOn::id:
    case nspc::VcmdEchoOff::id:
    case nspc::VcmdEchoParams::id:
    case nspc::VcmdEchoVolumeFade::id:
        return true;
    default:
        return false;
    }
}

bool isChannelStateOpcode(uint8_t opcode) {
    switch (opcode) {
    case nspc::VcmdInst::id:
    case nspc::VcmdPanning::id:
    case nspc::VcmdPanFade::id:
    case nspc::VcmdVibratoOn::id:
    case nspc::VcmdVibratoOff::id:
    case nspc::VcmdPerVoiceTranspose::id:
    case nspc::VcmdTremoloOn::id:
    case nspc::VcmdTremoloOff::id:
    case nspc::VcmdVolume::id:
    case nspc::VcmdVolumeFade::id:
    case nspc::VcmdVibratoFadeIn::id:
    case nspc::VcmdPitchEnvelopeTo::id:
    case nspc::VcmdPitchEnvelopeFrom::id:
    case nspc::VcmdPitchEnvelopeOff::id:
    case nspc::VcmdFineTune::id:
    case nspc::VcmdPitchSlideToNote::id:
    case nspc::VcmdPercussionBaseInstrument::id:
        return true;
    default:
        return false;
    }
}

std::optional<uint8_t> setupStateKeyForOpcode(uint8_t opcode) {
    switch (opcode) {
    case nspc::VcmdVibratoOn::id:
    case nspc::VcmdVibratoOff::id:
        return nspc::VcmdVibratoOn::id;
    case nspc::VcmdTremoloOn::id:
    case nspc::VcmdTremoloOff::id:
        return nspc::VcmdTremoloOn::id;
    case nspc::VcmdPitchEnvelopeTo::id:
    case nspc::VcmdPitchEnvelopeFrom::id:
    case nspc::VcmdPitchEnvelopeOff::id:
        return nspc::VcmdPitchEnvelopeTo::id;
    case nspc::VcmdEchoOn::id:
    case nspc::VcmdEchoOff::id:
        return nspc::VcmdEchoOn::id;
    default:
        return opcode;
    }
}

std::optional<OrderedVcmdKey> setupStateKeyForVcmd(const nspc::Vcmd& cmd,
                                                   const std::unordered_set<uint8_t>& legatoExtensionIds) {
    if (const auto* extension = std::get_if<nspc::VcmdExtension>(&cmd.vcmd)) {
        if (!legatoExtensionIds.contains(extension->id)) {
            return std::nullopt;
        }
        return static_cast<OrderedVcmdKey>(kExtensionStateKeyBase + extension->id);
    }

    const auto opcode = seedableVcmdOpcode(cmd);
    if (!opcode.has_value()) {
        return std::nullopt;
    }
    const auto key = setupStateKeyForOpcode(*opcode);
    return static_cast<OrderedVcmdKey>(key.value_or(*opcode));
}

void updateOrderedState(OrderedVcmdState& state, OrderedVcmdKey key, const nspc::Vcmd& cmd) {
    const auto it = std::find_if(state.begin(), state.end(), [key](const auto& entry) { return entry.first == key; });
    if (it != state.end()) {
        state.erase(it);
    }
    state.emplace_back(key, cmd);
}

enum class SetupScope {
    Global,
    Channel,
};

struct IndexedVcmdEvent {
    uint32_t tick = 0;
    int channel = 0;
    size_t order = 0;
    OrderedVcmdKey key = 0;
    SetupScope scope = SetupScope::Channel;
    nspc::Vcmd cmd{};
};

PlaybackSetupState collectPlaybackSetupState(const nspc::NspcSong& song, int sequenceStartRow,
                                             const std::unordered_set<uint8_t>& legatoExtensionIds) {
    PlaybackSetupState state;
    if (sequenceStartRow < 0) {
        return state;
    }

    const auto& sequence = song.sequence();
    if (sequence.empty()) {
        return state;
    }

    TriggerSequenceState sequenceState;
    int currentRow = -1;
    const int maxPatternSteps = std::max(32, static_cast<int>(sequence.size()) * 64);
    for (int step = 0; step < maxPatternSteps; ++step) {
        const SequenceAdvanceResult next = advanceSequenceRowFromPatternTrigger(sequence, currentRow, sequenceState);
        if (next.reachedEnd || next.row < 0 || next.row >= static_cast<int>(sequence.size())) {
            break;
        }
        if (next.row == sequenceStartRow) {
            break;
        }

        currentRow = next.row;
        const auto* play = std::get_if<nspc::PlayPattern>(&sequence[static_cast<size_t>(currentRow)]);
        if (!play) {
            continue;
        }

        nspc::NspcFlattenOptions flattenOptions{};
        flattenOptions.clipToEarliestTrackEnd = false;
        const auto flatPattern = nspc::flattenPatternById(song, play->patternId, flattenOptions);
        if (!flatPattern.has_value()) {
            continue;
        }

        std::vector<IndexedVcmdEvent> orderedEvents;
        for (int ch = 0; ch < 8; ++ch) {
            const auto& events = flatPattern->channels[static_cast<size_t>(ch)].events;
            for (size_t i = 0; i < events.size(); ++i) {
                if (const auto* duration = std::get_if<nspc::Duration>(&events[i].event)) {
                    const bool hasQv = duration->quantization.has_value() || duration->velocity.has_value();
                    if (hasQv) {
                        const uint8_t q = static_cast<uint8_t>(duration->quantization.value_or(0) & 0x07);
                        const uint8_t v = static_cast<uint8_t>(duration->velocity.value_or(0) & 0x0F);
                        state.channelQv[static_cast<size_t>(ch)] = static_cast<uint8_t>((q << 4) | v);
                    }

                    nspc::Duration effectiveDuration = *duration;
                    if (!hasQv && state.channelQv[static_cast<size_t>(ch)].has_value()) {
                        const uint8_t qv = *state.channelQv[static_cast<size_t>(ch)];
                        effectiveDuration.quantization = static_cast<uint8_t>((qv >> 4) & 0x07);
                        effectiveDuration.velocity = static_cast<uint8_t>(qv & 0x0F);
                    }
                    state.channelDuration[static_cast<size_t>(ch)] = effectiveDuration;

                    continue;
                }
                const auto* vcmd = std::get_if<nspc::Vcmd>(&events[i].event);
                if (!vcmd) {
                    continue;
                }
                const auto normalized = normalizeSetupVcmd(*vcmd, legatoExtensionIds);
                if (!normalized.has_value()) {
                    continue;
                }
                const auto key = setupStateKeyForVcmd(*normalized, legatoExtensionIds);
                if (!key.has_value()) {
                    continue;
                }
                SetupScope scope = SetupScope::Channel;
                if (!std::holds_alternative<nspc::VcmdExtension>(normalized->vcmd)) {
                    const auto opcode = seedableVcmdOpcode(*normalized);
                    if (!opcode.has_value()) {
                        continue;
                    }
                    if (isGlobalStateOpcode(*opcode)) {
                        scope = SetupScope::Global;
                    } else if (!isChannelStateOpcode(*opcode)) {
                        continue;
                    }
                }
                orderedEvents.push_back(IndexedVcmdEvent{
                    .tick = events[i].tick,
                    .channel = ch,
                    .order = i,
                    .key = *key,
                    .scope = scope,
                    .cmd = *normalized,
                });
            }
        }

        std::ranges::sort(orderedEvents, [](const IndexedVcmdEvent& lhs, const IndexedVcmdEvent& rhs) {
            if (lhs.tick != rhs.tick) {
                return lhs.tick < rhs.tick;
            }
            if (lhs.channel != rhs.channel) {
                return lhs.channel < rhs.channel;
            }
            return lhs.order < rhs.order;
        });

        for (const auto& event : orderedEvents) {
            if (event.scope == SetupScope::Global) {
                updateOrderedState(state.global, event.key, event.cmd);
            } else {
                updateOrderedState(state.channel[static_cast<size_t>(event.channel)], event.key, event.cmd);
            }
        }
    }

    return state;
}

nspc::NspcPattern* findPatternById(nspc::NspcSong& song, int patternId) {
    auto& patterns = song.patterns();
    const auto it =
        std::find_if(patterns.begin(), patterns.end(), [patternId](const nspc::NspcPattern& p) { return p.id == patternId; });
    if (it == patterns.end()) {
        return nullptr;
    }
    return &(*it);
}

nspc::NspcTrack* findTrackById(nspc::NspcSong& song, int trackId) {
    auto& tracks = song.tracks();
    const auto it = std::find_if(tracks.begin(), tracks.end(), [trackId](const nspc::NspcTrack& t) { return t.id == trackId; });
    if (it == tracks.end()) {
        return nullptr;
    }
    return &(*it);
}

int allocateTrackId(const nspc::NspcSong& song) {
    std::unordered_set<int> usedIds;
    usedIds.reserve(song.tracks().size() * 2 + 1);
    for (const auto& track : song.tracks()) {
        usedIds.insert(track.id);
    }
    int candidate = 0;
    while (usedIds.contains(candidate)) {
        ++candidate;
    }
    return candidate;
}

nspc::NspcEventId nextEventId(const nspc::NspcSong& song) {
    nspc::NspcEventId maxId = 0;
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

std::vector<nspc::Vcmd> extractStateCommands(const OrderedVcmdState& state) {
    std::vector<nspc::Vcmd> commands;
    commands.reserve(state.size());
    for (const auto& [opcode, cmd] : state) {
        (void)opcode;
        commands.push_back(cmd);
    }
    return commands;
}

void pruneCommandsOverriddenAtTrackStart(std::vector<nspc::Vcmd>& commands, const nspc::NspcTrack& track,
                                         const std::unordered_set<uint8_t>& legatoExtensionIds) {
    if (commands.empty()) {
        return;
    }

    std::unordered_set<OrderedVcmdKey> overriddenKeys;
    for (const auto& entry : track.events) {
        if (std::holds_alternative<nspc::Note>(entry.event) || std::holds_alternative<nspc::Tie>(entry.event) ||
            std::holds_alternative<nspc::Rest>(entry.event) || std::holds_alternative<nspc::Percussion>(entry.event) ||
            std::holds_alternative<nspc::End>(entry.event)) {
            break;
        }

        const auto* vcmd = std::get_if<nspc::Vcmd>(&entry.event);
        if (!vcmd) {
            continue;
        }
        const auto normalized = normalizeSetupVcmd(*vcmd, legatoExtensionIds);
        if (!normalized.has_value()) {
            continue;
        }
        const auto key = setupStateKeyForVcmd(*normalized, legatoExtensionIds);
        if (!key.has_value()) {
            continue;
        }
        overriddenKeys.insert(*key);
    }

    if (overriddenKeys.empty()) {
        return;
    }

    commands.erase(std::remove_if(commands.begin(), commands.end(),
                                  [&overriddenKeys, &legatoExtensionIds](const nspc::Vcmd& cmd) {
                                      const auto key = setupStateKeyForVcmd(cmd, legatoExtensionIds);
                                      return key.has_value() && overriddenKeys.contains(*key);
                                  }),
                   commands.end());
}

bool firstTrackEventNeedsDurationSeed(const nspc::NspcTrack& track) {
    for (const auto& entry : track.events) {
        if (std::holds_alternative<nspc::Duration>(entry.event)) {
            return false;
        }
        if (std::holds_alternative<nspc::Note>(entry.event) || std::holds_alternative<nspc::Tie>(entry.event) ||
            std::holds_alternative<nspc::Rest>(entry.event) || std::holds_alternative<nspc::Percussion>(entry.event)) {
            return true;
        }
        if (std::holds_alternative<nspc::End>(entry.event)) {
            return false;
        }
    }
    return false;
}

bool durationHasQv(const nspc::Duration& duration) {
    return duration.quantization.has_value() || duration.velocity.has_value();
}

uint8_t durationQvByte(const nspc::Duration& duration) {
    const uint8_t quant = static_cast<uint8_t>(duration.quantization.value_or(0) & 0x07);
    const uint8_t velocity = static_cast<uint8_t>(duration.velocity.value_or(0) & 0x0F);
    return static_cast<uint8_t>((quant << 4) | velocity);
}

void setDurationQvFromByte(nspc::Duration& duration, uint8_t qv) {
    duration.quantization = static_cast<uint8_t>((qv >> 4) & 0x07);
    duration.velocity = static_cast<uint8_t>(qv & 0x0F);
}

std::optional<size_t> firstDurationBeforeTimedEventIndex(const nspc::NspcTrack& track) {
    for (size_t i = 0; i < track.events.size(); ++i) {
        const auto& event = track.events[i].event;
        if (std::holds_alternative<nspc::Duration>(event)) {
            return i;
        }
        if (std::holds_alternative<nspc::Note>(event) || std::holds_alternative<nspc::Tie>(event) ||
            std::holds_alternative<nspc::Rest>(event) || std::holds_alternative<nspc::Percussion>(event) ||
            std::holds_alternative<nspc::End>(event)) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool trackHeadNeedsQvSeed(const nspc::NspcTrack& track) {
    for (const auto& entry : track.events) {
        if (const auto* duration = std::get_if<nspc::Duration>(&entry.event)) {
            return !durationHasQv(*duration);
        }
        if (std::holds_alternative<nspc::Note>(entry.event) || std::holds_alternative<nspc::Tie>(entry.event) ||
            std::holds_alternative<nspc::Rest>(entry.event) || std::holds_alternative<nspc::Percussion>(entry.event)) {
            return true;
        }
        if (std::holds_alternative<nspc::End>(entry.event)) {
            return false;
        }
    }
    return false;
}

void prependSetupToTrack(nspc::NspcTrack& track, const std::optional<nspc::Duration>& seedDuration,
                         const std::vector<nspc::Vcmd>& commands, nspc::NspcEventId& nextId) {
    if (!seedDuration.has_value() && commands.empty()) {
        return;
    }

    std::vector<nspc::NspcEventEntry> insertEntries;
    insertEntries.reserve(commands.size() + (seedDuration.has_value() ? 1u : 0u));

    if (seedDuration.has_value()) {
        insertEntries.push_back(nspc::NspcEventEntry{
            .id = nextId++,
            .event = nspc::NspcEvent{*seedDuration},
            .originalAddr = std::nullopt,
        });
    }
    for (const auto& cmd : commands) {
        insertEntries.push_back(nspc::NspcEventEntry{
            .id = nextId++,
            .event = nspc::NspcEvent{cmd},
            .originalAddr = std::nullopt,
        });
    }
    track.events.insert(track.events.begin(), insertEntries.begin(), insertEntries.end());
}

std::expected<void, std::string> injectPlaybackSetupIntoPattern(nspc::NspcSong& song, int sequenceStartRow,
                                                                const std::unordered_set<uint8_t>& legatoExtensionIds) {
    const auto patternId = patternIdFromSequenceRow(song, sequenceStartRow);
    if (!patternId.has_value()) {
        return std::unexpected(std::format("Sequence row {:02X} is not a PlayPattern", sequenceStartRow));
    }

    nspc::NspcPattern* pattern = findPatternById(song, *patternId);
    if (!pattern) {
        return std::unexpected(std::format("Pattern {:02X} not found", *patternId));
    }
    if (!pattern->channelTrackIds.has_value()) {
        return {};
    }

    const PlaybackSetupState setup = collectPlaybackSetupState(song, sequenceStartRow, legatoExtensionIds);
    const auto globalCommands = extractStateCommands(setup.global);
    std::array<std::vector<nspc::Vcmd>, 8> channelCommands{};
    std::array<std::optional<nspc::Duration>, 8> channelDurations = setup.channelDuration;
    for (int ch = 0; ch < 8; ++ch) {
        channelCommands[static_cast<size_t>(ch)] = extractStateCommands(setup.channel[static_cast<size_t>(ch)]);
    }

    bool hasAnySetup = !globalCommands.empty();
    if (!hasAnySetup) {
        for (int ch = 0; ch < 8; ++ch) {
            if (!channelCommands[static_cast<size_t>(ch)].empty() ||
                channelDurations[static_cast<size_t>(ch)].has_value()) {
                hasAnySetup = true;
                break;
            }
        }
    }
    if (!hasAnySetup) {
        for (int ch = 0; ch < 8; ++ch) {
            const int trackId = pattern->channelTrackIds.value()[static_cast<size_t>(ch)];
            if (trackId < 0) {
                continue;
            }
            const nspc::NspcTrack* track = findTrackById(song, trackId);
            if (!track) {
                continue;
            }
            if (trackHeadNeedsQvSeed(*track)) {
                hasAnySetup = true;
                break;
            }
        }
    }
    if (!hasAnySetup) {
        return {};
    }

    // Clone selected-pattern tracks so setup injection cannot affect other patterns/channels
    // that may share the same underlying track IDs.
    for (int ch = 0; ch < 8; ++ch) {
        const int sourceTrackId = pattern->channelTrackIds.value()[static_cast<size_t>(ch)];
        if (sourceTrackId < 0) {
            continue;
        }
        const nspc::NspcTrack* sourceTrack = findTrackById(song, sourceTrackId);
        if (!sourceTrack) {
            return std::unexpected(std::format("Track {:02X} not found for channel {}", sourceTrackId, ch + 1));
        }

        nspc::NspcTrack cloned = *sourceTrack;
        cloned.id = allocateTrackId(song);
        cloned.originalAddr = 0;
        song.tracks().push_back(std::move(cloned));
        pattern->channelTrackIds.value()[static_cast<size_t>(ch)] = song.tracks().back().id;
    }

    auto nextId = nextEventId(song);
    int globalSetupSeedChannel = -1;
    if (!globalCommands.empty()) {
        for (int ch = 0; ch < 8; ++ch) {
            const int trackId = pattern->channelTrackIds.value()[static_cast<size_t>(ch)];
            if (trackId >= 0) {
                globalSetupSeedChannel = ch;
                break;
            }
        }
    }

    for (int ch = 0; ch < 8; ++ch) {
        const int trackId = pattern->channelTrackIds.value()[static_cast<size_t>(ch)];
        if (trackId < 0) {
            continue;
        }
        nspc::NspcTrack* track = findTrackById(song, trackId);
        if (!track) {
            continue;
        }

        std::vector<nspc::Vcmd> commands;
        if (!globalCommands.empty() && ch == globalSetupSeedChannel) {
            commands.insert(commands.end(), globalCommands.begin(), globalCommands.end());
        }
        const auto& local = channelCommands[static_cast<size_t>(ch)];
        commands.insert(commands.end(), local.begin(), local.end());
        pruneCommandsOverriddenAtTrackStart(commands, *track, legatoExtensionIds);

        const auto setupDuration = channelDurations[static_cast<size_t>(ch)];
        const auto setupQv = setup.channelQv[static_cast<size_t>(ch)];
        const bool setupHasQv = setupQv.has_value() || (setupDuration.has_value() && durationHasQv(*setupDuration));
        const uint8_t fallbackQv =
            setupQv.value_or(setupHasQv ? durationQvByte(*setupDuration) : static_cast<uint8_t>(0x7F));

        if (const auto durationIndex = firstDurationBeforeTimedEventIndex(*track); durationIndex.has_value()) {
            auto* firstDuration = std::get_if<nspc::Duration>(&track->events[*durationIndex].event);
            if (firstDuration && !durationHasQv(*firstDuration)) {
                setDurationQvFromByte(*firstDuration, fallbackQv);
            }
        }

        std::optional<nspc::Duration> seedDuration = std::nullopt;
        if (firstTrackEventNeedsDurationSeed(*track)) {
            if (setupDuration.has_value()) {
                seedDuration = *setupDuration;
            } else {
                seedDuration = nspc::Duration{
                    .ticks = 1,
                    .quantization = std::nullopt,
                    .velocity = std::nullopt,
                };
            }
            if (!durationHasQv(*seedDuration)) {
                setDurationQvFromByte(*seedDuration, fallbackQv);
            }
        }
        prependSetupToTrack(*track, seedDuration, commands, nextId);
    }

    return {};
}

std::vector<nspc::NspcSequenceOp> buildPlayFromSequence(const std::vector<nspc::NspcSequenceOp>& sequence,
                                                         int sequenceStartRow) {
    std::vector<nspc::NspcSequenceOp> rebuilt;
    rebuilt.reserve(sequence.size() + 1);
    // Use a one-shot jump at row 0 so "Play From Pattern" is not dependent on fast-forward state.
    rebuilt.push_back(nspc::JumpTimes{
        .count = 1,
        .target =
            nspc::SequenceTarget{
                .index = sequenceStartRow + 1,
                .addr = 0,
            },
    });

    for (const auto& op : sequence) {
        rebuilt.push_back(std::visit(
            nspc::overloaded{
                [](const nspc::PlayPattern& value) -> nspc::NspcSequenceOp { return value; },
                [](const nspc::JumpTimes& value) -> nspc::NspcSequenceOp {
                    nspc::JumpTimes shifted = value;
                    if (shifted.target.index.has_value()) {
                        shifted.target.index = *shifted.target.index + 1;
                    }
                    return shifted;
                },
                [](const nspc::AlwaysJump& value) -> nspc::NspcSequenceOp {
                    nspc::AlwaysJump shifted = value;
                    if (shifted.target.index.has_value()) {
                        shifted.target.index = *shifted.target.index + 1;
                    }
                    return shifted;
                },
                [](const nspc::FastForwardOn& value) -> nspc::NspcSequenceOp { return value; },
                [](const nspc::FastForwardOff& value) -> nspc::NspcSequenceOp { return value; },
                [](const nspc::EndSequence& value) -> nspc::NspcSequenceOp { return value; },
            },
            op));
    }
    return rebuilt;
}

void resetPlaybackTracking(app::PlaybackTrackingState& playback) {
    playback.hooksInstalled.store(false, std::memory_order_relaxed);
    playback.awaitingFirstPatternTrigger.store(false, std::memory_order_relaxed);
    playback.pendingStopAtEnd.store(false, std::memory_order_relaxed);
    playback.eventSerial.store(0, std::memory_order_relaxed);
    playback.engineTickEvents.store(0, std::memory_order_relaxed);
    playback.sequenceRow.store(-1, std::memory_order_relaxed);
    playback.patternId.store(-1, std::memory_order_relaxed);
    playback.patternTick.store(-1, std::memory_order_relaxed);
}

emulation::SpcAddressAccess toWatchAccess(nspc::NspcEngineHookOperation op) {
    switch (op) {
    case nspc::NspcEngineHookOperation::Execute:
        return emulation::SpcAddressAccess::Execute;
    case nspc::NspcEngineHookOperation::Read:
        return emulation::SpcAddressAccess::Read;
    case nspc::NspcEngineHookOperation::Write:
        return emulation::SpcAddressAccess::Write;
    }
    return emulation::SpcAddressAccess::Write;
}

void applyPlaybackSnapshot(app::PlaybackTrackingState& playback, bool tickEvent, bool patternEvent,
                           const std::vector<nspc::NspcSequenceOp>* sequenceOps,
                           TriggerSequenceState* sequenceState, int initialSequenceRow, bool hasTickTrigger) {
    if (patternEvent) {
        // Trigger-driven sequence tracking advances on each pattern trigger.
        const bool firstPatternTrigger =
            playback.awaitingFirstPatternTrigger.exchange(false, std::memory_order_relaxed);
        const int priorTick = playback.patternTick.load(std::memory_order_relaxed);
        // Some engines can hit the pattern trigger address more than once before a tick advances.
        // Ignore duplicate callbacks so the tracked row does not jump ahead by one.
        if (hasTickTrigger && !firstPatternTrigger && priorTick == 0) {
            playback.eventSerial.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const int currentRow = playback.sequenceRow.load(std::memory_order_relaxed);
        if (firstPatternTrigger || currentRow < 0) {
            int startRow = initialSequenceRow;
            if (sequenceOps != nullptr && !sequenceOps->empty()) {
                startRow = std::clamp(startRow, 0, static_cast<int>(sequenceOps->size()) - 1);
            } else {
                startRow = std::max(startRow, 0);
            }
            playback.sequenceRow.store(startRow, std::memory_order_relaxed);
        } else if (sequenceOps != nullptr && sequenceState != nullptr && !sequenceOps->empty()) {
            const SequenceAdvanceResult next = advanceSequenceRowFromPatternTrigger(*sequenceOps, currentRow, *sequenceState);
            playback.sequenceRow.store(next.row, std::memory_order_relaxed);
            if (next.reachedEnd) {
                playback.hooksInstalled.store(false, std::memory_order_relaxed);
                playback.pendingStopAtEnd.store(true, std::memory_order_relaxed);
            }
        } else {
            playback.sequenceRow.store(currentRow + 1, std::memory_order_relaxed);
        }

        if (sequenceOps != nullptr && !sequenceOps->empty()) {
            const int trackedRow = playback.sequenceRow.load(std::memory_order_relaxed);
            const auto patternId = patternIdFromSequenceRow(*sequenceOps, trackedRow);
            playback.patternId.store(patternId.value_or(-1), std::memory_order_relaxed);
        } else {
            playback.patternId.store(-1, std::memory_order_relaxed);
        }

        playback.patternTick.store(0, std::memory_order_relaxed);
    }

    if (tickEvent) {
        int tick = playback.patternTick.load(std::memory_order_relaxed);
        playback.patternTick.store((tick < 0) ? 0 : (tick + 1), std::memory_order_relaxed);
    }

    if (tickEvent) {
        playback.engineTickEvents.fetch_add(1, std::memory_order_relaxed);
    }
    playback.eventSerial.fetch_add(1, std::memory_order_relaxed);
}

void installPlaybackHooks(const nspc::NspcEngineConfig& engine, emulation::SpcDsp& dsp,
                          app::PlaybackTrackingState& playback,
                          std::optional<std::vector<nspc::NspcSequenceOp>> sequenceSnapshot,
                          int initialSequenceRow) {
    dsp.clearAddressWatches();
    resetPlaybackTracking(playback);

    if (!engine.playbackHooks.has_value()) {
        return;
    }

    const nspc::NspcEnginePlaybackHooks hooks = *engine.playbackHooks;
    const bool hasTickTrigger = hooks.tickTrigger.has_value();
    auto sequenceOps = std::make_shared<const std::vector<nspc::NspcSequenceOp>>(
        sequenceSnapshot.value_or(std::vector<nspc::NspcSequenceOp>{}));
    auto triggerState = std::make_shared<TriggerSequenceState>();
    size_t installed = 0;

    auto add_trigger = [&](const std::optional<nspc::NspcEngineHookTrigger>& trigger, bool tickEvent,
                           bool patternEvent) {
        if (!trigger.has_value()) {
            return;
        }

        const uint16_t triggerCount = std::max<uint16_t>(1u, trigger->count);
        auto triggerHitCount = std::make_shared<uint16_t>(0);
        emulation::SpcAddressAccessWatch watch{};
        watch.access = toWatchAccess(trigger->operation);
        watch.address = trigger->address;
        watch.value = trigger->value;
        watch.includeDummy = trigger->includeDummy;
        const auto watchId =
            dsp.addAddressWatch(watch,
                                [&playback, tickEvent, patternEvent, sequenceOps,
                                 triggerState, initialSequenceRow, hasTickTrigger,
                                 triggerCount, triggerHitCount](
                                    const emulation::SpcAddressAccessEvent& event) {
            (void)event;
            *triggerHitCount = static_cast<uint16_t>(*triggerHitCount + 1u);
            if (*triggerHitCount < triggerCount) {
                return;
            }
            *triggerHitCount = 0;
            applyPlaybackSnapshot(playback, tickEvent, patternEvent, sequenceOps.get(), triggerState.get(),
                                  initialSequenceRow, hasTickTrigger);
        });
        if (watchId != 0) {
            ++installed;
        }
    };

    add_trigger(hooks.tickTrigger, true, false);
    add_trigger(hooks.patternTrigger, false, true);

    if (hooks.patternTrigger.has_value()) {
        int startRow = initialSequenceRow;
        if (!sequenceOps->empty()) {
            startRow = std::clamp(startRow, 0, static_cast<int>(sequenceOps->size()) - 1);
        } else {
            startRow = std::max(startRow, 0);
        }

        // Trigger-driven tracking starts from the first triggered pattern row.
        playback.sequenceRow.store(startRow, std::memory_order_relaxed);
        playback.awaitingFirstPatternTrigger.store(true, std::memory_order_relaxed);
        playback.patternTick.store(-1, std::memory_order_relaxed);
        if (!sequenceOps->empty()) {
            const auto patternId = patternIdFromSequenceRow(*sequenceOps, startRow);
            playback.patternId.store(patternId.value_or(-1), std::memory_order_relaxed);
        }
        triggerState->jumpTimesRemaining.clear();
    }

    playback.hooksInstalled.store(installed > 0, std::memory_order_relaxed);
}

bool hasAnyUserProvidedContent(const nspc::NspcProject& project) {
    const bool hasUserSongs = std::any_of(project.songs().begin(), project.songs().end(),
                                          [](const nspc::NspcSong& song) { return song.isUserProvided(); });
    if (hasUserSongs) {
        return true;
    }

    const bool hasUserInstruments = std::any_of(project.instruments().begin(), project.instruments().end(),
                                                [](const nspc::NspcInstrument& instrument) {
                                                    return instrument.contentOrigin == nspc::NspcContentOrigin::UserProvided;
                                                });
    if (hasUserInstruments) {
        return true;
    }

    return std::any_of(project.samples().begin(), project.samples().end(), [](const nspc::BrrSample& sample) {
        return sample.contentOrigin == nspc::NspcContentOrigin::UserProvided;
    });
}

[[nodiscard]] nspc::NspcBuildOptions buildOptionsFromAppState(const app::AppState& appState) {
    return nspc::NspcBuildOptions{
        .optimizeSubroutines = appState.optimizeSubroutinesOnBuild,
        .optimizerOptions = appState.optimizerOptions,
        .applyOptimizedSongToProject =
            appState.optimizeSubroutinesOnBuild && !appState.flattenSubroutinesOnLoad,
        .compactAramLayout = appState.compactAramLayoutOnBuild,
    };
}

std::optional<std::string> preflightUserContentBuild(const nspc::NspcProject& project,
                                                     const nspc::NspcBuildOptions& buildOptions) {
    if (!hasAnyUserProvidedContent(project)) {
        return std::nullopt;
    }

    auto preflightProject = project;
    auto preflightResult = nspc::buildUserContentUpload(preflightProject, buildOptions);
    if (!preflightResult.has_value()) {
        return preflightResult.error();
    }

    return std::nullopt;
}

struct PatchedSongBuildResult {
    std::vector<uint8_t> spcImage;
    std::vector<std::string> warnings;
    size_t patchCount = 0;
    size_t totalPatchBytes = 0;
};

std::optional<int> selectedSongIndexForPlayback(const app::AppState& appState) {
    if (!appState.project.has_value() || !appState.spcPlayer || appState.sourceSpcData.empty()) {
        return std::nullopt;
    }

    const auto& songs = appState.project->songs();
    const int selectedSongIndex = appState.selectedSongIndex;
    if (selectedSongIndex < 0 || selectedSongIndex >= static_cast<int>(songs.size())) {
        return std::nullopt;
    }

    return selectedSongIndex;
}

std::optional<int> selectedPlayFromSequenceRow(const app::AppState& appState, int songIndex) {
    if (!appState.project.has_value()) {
        return std::nullopt;
    }

    const auto& songs = appState.project->songs();
    if (songIndex < 0 || songIndex >= static_cast<int>(songs.size())) {
        return std::nullopt;
    }

    const auto& song = songs[static_cast<size_t>(songIndex)];
    const int selectedRow = appState.selectedSequenceRow;
    if (selectedRow < 0 || selectedRow >= static_cast<int>(song.sequence().size())) {
        return std::nullopt;
    }
    if (!std::holds_alternative<nspc::PlayPattern>(song.sequence()[static_cast<size_t>(selectedRow)])) {
        return std::nullopt;
    }

    return selectedRow;
}

std::optional<PatchedSongBuildResult> buildPatchedSongForPlayback(nspc::NspcProject& project, int songIndex,
                                                                  const nspc::NspcBuildOptions& buildOptions,
                                                                  const std::vector<uint8_t>& baseSpcData,
                                                                  std::string& statusOut) {
    std::vector<uint8_t> patchedImage = baseSpcData;
    std::vector<std::string> combinedWarnings;
    size_t patchCount = 0;
    size_t totalPatchBytes = 0;

    const auto applyUpload = [&](const nspc::NspcUploadList& upload, std::string_view stage) -> bool {
        auto patched = nspc::applyUploadToSpcImage(upload, patchedImage);
        if (!patched.has_value()) {
            statusOut = std::format("{} patch failed: {}", stage, patched.error());
            return false;
        }

        patchedImage = std::move(*patched);
        patchCount += upload.chunks.size();
        totalPatchBytes += std::accumulate(upload.chunks.begin(), upload.chunks.end(), static_cast<size_t>(0),
                                           [](size_t sum, const nspc::NspcUploadChunk& chunk) {
                                               return sum + chunk.bytes.size();
                                           });
        return true;
    };

    bool engineExtensionsApplied = false;
    if (hasAnyUserProvidedContent(project)) {
        auto userContentProject = project;
        auto userUploadResult = nspc::buildUserContentUpload(userContentProject, buildOptions);
        if (!userUploadResult.has_value()) {
            statusOut = std::format("Build failed: {}", userUploadResult.error());
            return std::nullopt;
        }

        if (!applyUpload(*userUploadResult, "User-content")) {
            return std::nullopt;
        }
        engineExtensionsApplied = buildOptions.includeEngineExtensions;
    }

    auto songBuildOptions = buildOptions;
    if (engineExtensionsApplied) {
        // User-content upload already wrote extension patches; avoid writing the same patch chunks twice.
        songBuildOptions.includeEngineExtensions = false;
    }

    auto compileResult = nspc::buildSongScopedUpload(project, songIndex, songBuildOptions);
    if (!compileResult.has_value()) {
        statusOut = std::format("Build failed: {}", compileResult.error());
        return std::nullopt;
    }
    combinedWarnings.insert(combinedWarnings.end(), compileResult->warnings.begin(), compileResult->warnings.end());

    if (!applyUpload(compileResult->upload, "Song")) {
        return std::nullopt;
    }

    return PatchedSongBuildResult{
        .spcImage = std::move(patchedImage),
        .warnings = std::move(combinedWarnings),
        .patchCount = patchCount,
        .totalPatchBytes = totalPatchBytes,
    };
}

void clearConfiguredEchoBuffer(emulation::SpcDsp& dsp, const nspc::NspcEngineConfig& engineConfig) {
    if (engineConfig.echoBuffer == 0 || engineConfig.echoBufferLen == 0) {
        return;
    }

    const uint32_t echoEnd = std::min<uint32_t>(engineConfig.echoBuffer, emulation::SpcDsp::AramSize);
    const uint32_t echoLen = std::min<uint32_t>(engineConfig.echoBufferLen, echoEnd);
    const uint32_t echoStart = echoEnd - echoLen;
    if (echoStart >= echoEnd) {
        return;
    }

    auto aram = dsp.aram();
    auto all = aram.all();
    std::fill_n(all.data() + echoStart, echoLen, static_cast<uint8_t>(0));
}

void setVoiceVolumesToZero(emulation::SpcDsp& dsp) { 
    for (uint8_t voice = 0; voice < 8; ++voice) { 
        dsp.writeDspRegister(static_cast<uint8_t>(voice * 0x10), 0x00); 
        dsp.writeDspRegister(static_cast<uint8_t>((voice * 0x10) + 1), 0x00);
    }
}

}  // namespace

ControlPanel::ControlPanel(app::AppState& appState) : appState_(appState) {
    appState_.playSong = [this]() { return doPlaySong(); };
    appState_.playFromPattern = [this]() { return doPlayFromPattern(); };
    appState_.stopPlayback = [this]() { doStop(); };
    appState_.isPlaying = [this]() { return doIsPlaying(); };
}

bool ControlPanel::playSpcImage(const std::vector<uint8_t>& spcImage, uint16_t entryPoint,
                                const nspc::NspcEngineConfig& engineConfig, int songIndex,
                                std::string statusText,
                                std::optional<std::vector<nspc::NspcSequenceOp>> trackingSequence,
                                int trackingStartRow) {
    auto& player = *appState_.spcPlayer;
    player.stop();
    resetPlaybackTracking(appState_.playback);
    player.spcDsp().clearAddressWatches();

    if (!player.loadFromMemory(spcImage.data(), static_cast<uint32_t>(spcImage.size()))) {
        status_ = "Failed to load SPC image into player";
        return false;
    }

    auto& dsp = player.spcDsp();
    
    // Start warmup from the configured engine entrypoint instead of the source SPC snapshot PC.
    // We intentionally do not call reset() here because that would wipe the uploaded song data.
    dsp.setPC(entryPoint);
    dsp.clearSampleBuffer();
    setVoiceVolumesToZero(dsp);

    constexpr uint64_t kEngineWarmupCycles = 140000;
    dsp.runCycles(kEngineWarmupCycles);

    player.setChannelMask(appState_.playback.channelMask);

    std::optional<std::vector<nspc::NspcSequenceOp>> sequenceSnapshot = std::move(trackingSequence);
    if (!sequenceSnapshot.has_value() && appState_.project.has_value()) {
        const auto& songs = appState_.project->songs();
        if (songIndex >= 0 && songIndex < static_cast<int>(songs.size())) {
            sequenceSnapshot = songs[static_cast<size_t>(songIndex)].sequence();
        }
    }

    installPlaybackHooks(engineConfig, dsp, appState_.playback, std::move(sequenceSnapshot), trackingStartRow);
    const uint8_t triggerValue =
        static_cast<uint8_t>((static_cast<uint32_t>(songIndex) + engineConfig.songTriggerOffset) & 0xFFu);
    dsp.writePort(engineConfig.songTriggerPort, triggerValue);
    player.play();

    status_ = std::move(statusText);
    return true;
}

bool ControlPanel::doPlaySong() {
    const auto selectedSongIndex = selectedSongIndexForPlayback(appState_);
    if (!selectedSongIndex.has_value()) {
        return false;
    }

    warnings_.clear();

    auto& project = *appState_.project;
    const int songIndex = *selectedSongIndex;
    const auto buildOptions = buildOptionsFromAppState(appState_);

    if (const auto preflightError = preflightUserContentBuild(project, buildOptions); preflightError.has_value()) {
        status_ = std::format("Build failed: {}", *preflightError);
        return false;
    }

    auto patchedBuild =
        buildPatchedSongForPlayback(project, songIndex, buildOptions, appState_.sourceSpcData, status_);
    if (!patchedBuild.has_value()) {
        return false;
    }

    warnings_ = std::move(patchedBuild->warnings);
    return playSpcImage(
        patchedBuild->spcImage, project.engineConfig().entryPoint, project.engineConfig(), songIndex,
        std::format("Playing song {:02X} | {} patches | {} bytes", songIndex, patchedBuild->patchCount,
                    patchedBuild->totalPatchBytes));
}

bool ControlPanel::doPlayFromPattern() {
    const auto selectedSongIndex = selectedSongIndexForPlayback(appState_);
    if (!selectedSongIndex.has_value()) {
        return false;
    }

    const int songIndex = *selectedSongIndex;
    const auto playFromSequenceRow = selectedPlayFromSequenceRow(appState_, songIndex);
    if (!playFromSequenceRow.has_value()) {
        return false;
    }

    warnings_.clear();

    auto& project = *appState_.project;
    const auto buildOptions = buildOptionsFromAppState(appState_);
    if (const auto preflightError = preflightUserContentBuild(project, buildOptions); preflightError.has_value()) {
        status_ = std::format("Build failed: {}", *preflightError);
        return false;
    }

    if (buildOptions.applyOptimizedSongToProject) {
        auto syncResult = nspc::buildSongScopedUpload(project, songIndex, buildOptions);
        if (!syncResult.has_value()) {
            status_ = std::format("Build failed: {}", syncResult.error());
            return false;
        }
    }

    const int startRow = *playFromSequenceRow;
    auto playbackProject = project;
    auto& playbackSong = playbackProject.songs()[static_cast<size_t>(songIndex)];
    const auto trackingSequence = project.songs()[static_cast<size_t>(songIndex)].sequence();
    const auto legatoExtensionIds = collectLegatoExtensionIds(project.engineConfig());

    const auto injectResult = injectPlaybackSetupIntoPattern(playbackSong, startRow, legatoExtensionIds);
    if (!injectResult.has_value()) {
        status_ = std::format("Play-from setup failed: {}", injectResult.error());
        return false;
    }

    playbackSong.sequence() = buildPlayFromSequence(playbackSong.sequence(), startRow);

    auto playbackBuildOptions = buildOptions;
    playbackBuildOptions.applyOptimizedSongToProject = false;
    auto patchedBuild =
        buildPatchedSongForPlayback(playbackProject, songIndex, playbackBuildOptions, appState_.sourceSpcData, status_);
    if (!patchedBuild.has_value()) {
        return false;
    }
    warnings_ = std::move(patchedBuild->warnings);

    const auto patternId =
        patternIdFromSequenceRow(project.songs()[static_cast<size_t>(songIndex)], startRow);
    const std::string patternText =
        patternId.has_value() ? std::format("{:02X}", *patternId) : std::string{"??"};
    return playSpcImage(
        patchedBuild->spcImage, project.engineConfig().entryPoint, project.engineConfig(), songIndex,
        std::format("Playing song {:02X} from row {:02X} (P{}) | {} patches | {} bytes", songIndex,
                    startRow, patternText, patchedBuild->patchCount, patchedBuild->totalPatchBytes),
        trackingSequence, startRow);
}

void ControlPanel::doStop() {
    if (!appState_.spcPlayer) {
        return;
    }
    preserveSelectionFromPlayback(appState_);
    appState_.spcPlayer->stop();
    appState_.spcPlayer->setChannelMask(0xFF);
    appState_.spcPlayer->spcDsp().clearAddressWatches();
    resetPlaybackTracking(appState_.playback);
    status_ = "Stopped";
}

bool ControlPanel::doIsPlaying() const {
    return appState_.spcPlayer && appState_.spcPlayer->isPlaying();
}

void ControlPanel::draw() {
    if (appState_.playback.pendingStopAtEnd.exchange(false, std::memory_order_relaxed)) {
        preserveSelectionFromPlayback(appState_);
        if (appState_.spcPlayer) {
            appState_.spcPlayer->stop();
            appState_.spcPlayer->setChannelMask(0xFF);
            appState_.spcPlayer->spcDsp().clearAddressWatches();
        }
        resetPlaybackTracking(appState_.playback);
        status_ = "Stopped at end of sequence";
    }

    ImGui::PushFont(ntrak::app::App::fonts().mono, 14.0f);
    ImGui::TextUnformatted("Playback");
    ImGui::Separator();

    const bool hasProject = appState_.project.has_value();
    const bool hasPlayer = static_cast<bool>(appState_.spcPlayer);
    const bool hasBaseSpc = !appState_.sourceSpcData.empty();
    const auto selectedSongIndex = selectedSongIndexForPlayback(appState_);
    const bool songSelectionValid = selectedSongIndex.has_value();

    if (!hasProject) {
        ImGui::TextDisabled("No project loaded");
        ImGui::TextDisabled("Import an SPC to build/play edited songs");
    }
    
    ImGui::PopFont();

    ImGui::Checkbox("Follow sequence", &appState_.playback.followPlayback);
    ImGui::Checkbox("Follow row", &appState_.playback.autoScroll);
    ImGui::Separator();

    const bool canPlay = hasProject && hasPlayer && hasBaseSpc && songSelectionValid;
    const auto playFromSequenceRow =
        selectedSongIndex.has_value() ? selectedPlayFromSequenceRow(appState_, *selectedSongIndex) : std::nullopt;

    ImGui::BeginDisabled(!canPlay);
    if (ImGui::Button("Play Song")) {
        (void)doPlaySong();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!(canPlay && playFromSequenceRow.has_value()));
    if (ImGui::Button("Play From Pattern")) {
        (void)doPlayFromPattern();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasPlayer);
    if (ImGui::Button("Stop")) {
        doStop();
    }
    ImGui::EndDisabled();

    // ImGui::SameLine();
    // ImGui::BeginDisabled(!songSelectionValid);
    // if (ImGui::Button("Verify Roundtrip")) {
    //     roundtripLines_.clear();
    //     roundtripStatus_.clear();

    //     auto& project = *appState_.project;
    //     const int songIndex = std::clamp(appState_.selectedSongIndex, 0, static_cast<int>(project.songs().size()) - 1);
    //     auto verifyResult = nspc::verifySongRoundTrip(project, songIndex);
    //     if (!verifyResult.has_value()) {
    //         roundtripStatus_ = std::format("Roundtrip verify failed: {}", verifyResult.error());
    //     } else {
    //         roundtripStatus_ = verifyResult->equivalent ? "Roundtrip verify: PASS" : "Roundtrip verify: FAIL";
    //         roundtripLines_ = verifyResult->messages;
    //     }
    // }
    // ImGui::EndDisabled();

    if (!status_.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", status_.c_str());
    }

    if (!warnings_.empty()) {
        if (ImGui::CollapsingHeader("Build Warnings")) {
            for (const auto& warning : warnings_) {
                ImGui::BulletText("%s", warning.c_str());
            }
        }
    }

    if (!roundtripStatus_.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", roundtripStatus_.c_str());
    }

    if (!roundtripLines_.empty()) {
        if (ImGui::CollapsingHeader("Roundtrip Report")) {
            for (const auto& line : roundtripLines_) {
                ImGui::BulletText("%s", line.c_str());
            }
        }
    }


}

}  // namespace ntrak::ui
