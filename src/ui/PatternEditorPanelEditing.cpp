#include "ntrak/ui/PatternEditorPanel.hpp"
#include "ntrak/ui/PatternEditorPanelUtils.hpp"

#include "ntrak/nspc/NspcCommand.hpp"
#include "ntrak/nspc/NspcCommandTransaction.hpp"
#include "ntrak/nspc/NspcEngine.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ntrak::ui {

using namespace detail;

namespace {

struct TrackerNoteKeyBinding {
    ImGuiKey key;
    int semitoneOffset;
};

constexpr std::array<TrackerNoteKeyBinding, 25> kTrackerNoteKeys = {{
    {ImGuiKey_Z, 0},  {ImGuiKey_S, 1},  {ImGuiKey_X, 2},  {ImGuiKey_D, 3},  {ImGuiKey_C, 4},
    {ImGuiKey_V, 5},  {ImGuiKey_G, 6},  {ImGuiKey_B, 7},  {ImGuiKey_H, 8},  {ImGuiKey_N, 9},
    {ImGuiKey_J, 10}, {ImGuiKey_M, 11}, {ImGuiKey_Q, 12}, {ImGuiKey_2, 13}, {ImGuiKey_W, 14},
    {ImGuiKey_3, 15}, {ImGuiKey_E, 16}, {ImGuiKey_R, 17}, {ImGuiKey_5, 18}, {ImGuiKey_T, 19},
    {ImGuiKey_6, 20}, {ImGuiKey_Y, 21}, {ImGuiKey_7, 22}, {ImGuiKey_U, 23}, {ImGuiKey_I, 24},
}};
constexpr uint8_t kDspDirReg = 0x5D;

uint16_t pitchMultiplierFromInstrument(const nspc::NspcInstrument& instrument) {
    uint16_t pitchMult = (static_cast<uint16_t>(instrument.basePitchMult) << 8u) | instrument.fracPitchMult;
    if (pitchMult == 0) {
        pitchMult = 0x0100;
    }
    return pitchMult;
}

enum class FxParamKind : uint8_t {
    UnsignedByte,
    SignedByte,
    Note,
    ChannelMask,
    FirIndex,
};

struct FxParamSpec {
    const char* label = "";
    const char* help = "";
    FxParamKind kind = FxParamKind::UnsignedByte;
    int minValue = 0;
    int maxValue = 0xFF;
    int defaultValue = 0;
};

struct FxEffectSpec {
    uint8_t id = 0;
    const char* name = "";
    const char* description = "";
    std::array<FxParamSpec, 3> params{};
    uint8_t paramCount = 0;
};

constexpr FxParamSpec makeFxParam(const char* label, const char* help, FxParamKind kind, int minValue, int maxValue,
                                  int defaultValue) {
    return FxParamSpec{
        .label = label,
        .help = help,
        .kind = kind,
        .minValue = minValue,
        .maxValue = maxValue,
        .defaultValue = defaultValue,
    };
}

constexpr std::array<FxParamSpec, 3> makeFxParams(const FxParamSpec& p0 = FxParamSpec{},
                                                   const FxParamSpec& p1 = FxParamSpec{},
                                                   const FxParamSpec& p2 = FxParamSpec{}) {
    return std::array<FxParamSpec, 3>{p0, p1, p2};
}

constexpr std::array<FxEffectSpec, 28> kFxEffectSpecs = {{
    FxEffectSpec{
        .id = nspc::VcmdPanning::id,
        .name = "Panning",
        .description = "Sets stereo position for the current voice.",
        .params = makeFxParams(makeFxParam("Pan", "Stereo position. 0 = left, 128 = center, 255 = right.",
                                           FxParamKind::UnsignedByte, 0, 255, 128)),
        .paramCount = 1,
    },
    FxEffectSpec{
        .id = nspc::VcmdPanFade::id,
        .name = "Pan Fade",
        .description = "Smoothly moves panning to a new target.",
        .params = makeFxParams(
            makeFxParam("Time", "How long the fade takes (in engine ticks).", FxParamKind::UnsignedByte, 0, 255, 16),
            makeFxParam("Target Pan", "Destination pan value.", FxParamKind::UnsignedByte, 0, 255, 128)),
        .paramCount = 2,
    },
    FxEffectSpec{
        .id = nspc::VcmdVibratoOn::id,
        .name = "Vibrato On",
        .description = "Enables pitch wobble (vibrato).",
        .params = makeFxParams(
            makeFxParam("Delay", "How long to wait before vibrato starts.", FxParamKind::UnsignedByte, 0, 255, 0),
            makeFxParam("Rate", "How fast the vibrato cycles.", FxParamKind::UnsignedByte, 0, 255, 8),
            makeFxParam("Depth", "How strong the pitch wobble is.", FxParamKind::UnsignedByte, 0, 255, 8)),
        .paramCount = 3,
    },
    FxEffectSpec{
        .id = nspc::VcmdVibratoOff::id,
        .name = "Vibrato Off",
        .description = "Disables vibrato.",
        .params = makeFxParams(),
        .paramCount = 0,
    },
    FxEffectSpec{
        .id = nspc::VcmdGlobalVolume::id,
        .name = "Global Volume",
        .description = "Sets master song volume for all channels.",
        .params = makeFxParams(makeFxParam("Volume", "Master volume level.", FxParamKind::UnsignedByte, 0, 255, 127)),
        .paramCount = 1,
    },
    FxEffectSpec{
        .id = nspc::VcmdGlobalVolumeFade::id,
        .name = "Global Volume Fade",
        .description = "Fades master song volume over time.",
        .params = makeFxParams(
            makeFxParam("Time", "How long the fade takes.", FxParamKind::UnsignedByte, 0, 255, 16),
            makeFxParam("Target Volume", "Destination master volume.", FxParamKind::UnsignedByte, 0, 255, 127)),
        .paramCount = 2,
    },
    FxEffectSpec{
        .id = nspc::VcmdTempo::id,
        .name = "Tempo",
        .description = "Sets song tempo immediately.",
        .params = makeFxParams(makeFxParam("Tempo", "Playback speed value.", FxParamKind::UnsignedByte, 0, 255, 96)),
        .paramCount = 1,
    },
    FxEffectSpec{
        .id = nspc::VcmdTempoFade::id,
        .name = "Tempo Fade",
        .description = "Fades song tempo over time.",
        .params = makeFxParams(
            makeFxParam("Time", "How long the tempo fade takes.", FxParamKind::UnsignedByte, 0, 255, 16),
            makeFxParam("Target Tempo", "Destination tempo value.", FxParamKind::UnsignedByte, 0, 255, 96)),
        .paramCount = 2,
    },
    FxEffectSpec{
        .id = nspc::VcmdGlobalTranspose::id,
        .name = "Global Transpose",
        .description = "Shifts all note pitches up or down in semitones.",
        .params = makeFxParams(makeFxParam("Semitones", "Negative lowers pitch, positive raises pitch.",
                                           FxParamKind::SignedByte, -128, 127, 0)),
        .paramCount = 1,
    },
    FxEffectSpec{
        .id = nspc::VcmdPerVoiceTranspose::id,
        .name = "Voice Transpose",
        .description = "Shifts pitch for this channel in semitones.",
        .params = makeFxParams(makeFxParam("Semitones", "Negative lowers pitch, positive raises pitch.",
                                           FxParamKind::SignedByte, -128, 127, 0)),
        .paramCount = 1,
    },
    FxEffectSpec{
        .id = nspc::VcmdTremoloOn::id,
        .name = "Tremolo On",
        .description = "Enables periodic volume wobble (tremolo).",
        .params = makeFxParams(
            makeFxParam("Delay", "How long to wait before tremolo starts.", FxParamKind::UnsignedByte, 0, 255, 0),
            makeFxParam("Rate", "How fast volume wobble cycles.", FxParamKind::UnsignedByte, 0, 255, 8),
            makeFxParam("Depth", "How strong the volume wobble is.", FxParamKind::UnsignedByte, 0, 255, 8)),
        .paramCount = 3,
    },
    FxEffectSpec{
        .id = nspc::VcmdTremoloOff::id,
        .name = "Tremolo Off",
        .description = "Disables tremolo.",
        .params = makeFxParams(),
        .paramCount = 0,
    },
    FxEffectSpec{
        .id = nspc::VcmdVolumeFade::id,
        .name = "Volume Fade",
        .description = "Fades current channel volume over time.",
        .params = makeFxParams(
            makeFxParam("Time", "How long the fade takes.", FxParamKind::UnsignedByte, 0, 255, 16),
            makeFxParam("Target Volume", "Destination channel volume.", FxParamKind::UnsignedByte, 0, 255, 127)),
        .paramCount = 2,
    },
    FxEffectSpec{
        .id = nspc::VcmdSubroutineCall::id,
        .name = "Subroutine Call",
        .description = "Calls a subroutine a configurable number of times.",
        .params = makeFxParams(
            makeFxParam("Address Lo", "Low byte of subroutine address.", FxParamKind::UnsignedByte, 0, 255, 0),
            makeFxParam("Address Hi", "High byte of subroutine address.", FxParamKind::UnsignedByte, 0, 255, 0),
            makeFxParam("Count", "Iteration count.", FxParamKind::UnsignedByte, 0, 255, 1)),
        .paramCount = 3,
    },
    FxEffectSpec{
        .id = nspc::VcmdVibratoFadeIn::id,
        .name = "Vibrato Fade In",
        .description = "Gradually increases vibrato depth.",
        .params = makeFxParams(makeFxParam("Time", "How long vibrato takes to fully fade in.",
                                           FxParamKind::UnsignedByte, 0, 255, 16)),
        .paramCount = 1,
    },
    FxEffectSpec{
        .id = nspc::VcmdPitchEnvelopeTo::id,
        .name = "Pitch Envelope To",
        .description = "Slides pitch toward a target note amount.",
        .params = makeFxParams(
            makeFxParam("Delay", "Wait before the envelope starts.", FxParamKind::UnsignedByte, 0, 255, 0),
            makeFxParam("Length", "How long the envelope lasts.", FxParamKind::UnsignedByte, 0, 255, 8),
            makeFxParam("Target", "Envelope target value.", FxParamKind::UnsignedByte, 0, 255, 0)),
        .paramCount = 3,
    },
    FxEffectSpec{
        .id = nspc::VcmdPitchEnvelopeFrom::id,
        .name = "Pitch Envelope From",
        .description = "Starts from an offset, then returns to normal pitch.",
        .params = makeFxParams(
            makeFxParam("Delay", "Wait before the envelope starts.", FxParamKind::UnsignedByte, 0, 255, 0),
            makeFxParam("Length", "How long the envelope lasts.", FxParamKind::UnsignedByte, 0, 255, 8),
            makeFxParam("Start", "Starting envelope value.", FxParamKind::UnsignedByte, 0, 255, 0)),
        .paramCount = 3,
    },
    FxEffectSpec{
        .id = nspc::VcmdPitchEnvelopeOff::id,
        .name = "Pitch Envelope Off",
        .description = "Disables pitch envelope processing.",
        .params = makeFxParams(),
        .paramCount = 0,
    },
    FxEffectSpec{
        .id = nspc::VcmdFineTune::id,
        .name = "Fine Tune",
        .description = "Fine pitch adjustment in signed semitone units.",
        .params = makeFxParams(makeFxParam("Tune", "Negative lowers pitch, positive raises pitch.",
                                           FxParamKind::SignedByte, -128, 127, 0)),
        .paramCount = 1,
    },
    FxEffectSpec{
        .id = nspc::VcmdEchoOn::id,
        .name = "Echo On",
        .description = "Enables echo for selected channels and sets echo volume.",
        .params = makeFxParams(
            makeFxParam("Channel Mask", "Bit mask of channels receiving echo.", FxParamKind::ChannelMask, 0, 255, 0xFF),
            makeFxParam("Left Volume", "Echo send level for left speaker.", FxParamKind::UnsignedByte, 0, 255, 64),
            makeFxParam("Right Volume", "Echo send level for right speaker.", FxParamKind::UnsignedByte, 0, 255, 64)),
        .paramCount = 3,
    },
    FxEffectSpec{
        .id = nspc::VcmdEchoOff::id,
        .name = "Echo Off",
        .description = "Disables echo.",
        .params = makeFxParams(),
        .paramCount = 0,
    },
    FxEffectSpec{
        .id = nspc::VcmdEchoParams::id,
        .name = "Echo Parameters",
        .description = "Sets echo delay, feedback amount, and FIR filter preset.",
        .params = makeFxParams(
            makeFxParam("Delay", "Echo delay length.", FxParamKind::UnsignedByte, 0, 255, 3),
            makeFxParam("Feedback", "How much echo feeds back into itself.", FxParamKind::UnsignedByte, 0, 255, 64),
            makeFxParam("FIR Index", "Filter preset index.", FxParamKind::FirIndex, 0, 255, 0)),
        .paramCount = 3,
    },
    FxEffectSpec{
        .id = nspc::VcmdEchoVolumeFade::id,
        .name = "Echo Volume Fade",
        .description = "Fades echo volume over time.",
        .params = makeFxParams(
            makeFxParam("Time", "How long the fade takes.", FxParamKind::UnsignedByte, 0, 255, 16),
            makeFxParam("Left Target", "Destination left echo volume.", FxParamKind::UnsignedByte, 0, 255, 64),
            makeFxParam("Right Target", "Destination right echo volume.", FxParamKind::UnsignedByte, 0, 255, 64)),
        .paramCount = 3,
    },
    FxEffectSpec{
        .id = nspc::VcmdPitchSlideToNote::id,
        .name = "Pitch Slide To Note",
        .description = "Slides pitch to a target note after an optional delay.",
        .params = makeFxParams(
            makeFxParam("Delay", "Wait before the slide starts.", FxParamKind::UnsignedByte, 0, 255, 0),
            makeFxParam("Length", "How long the slide lasts.", FxParamKind::UnsignedByte, 0, 255, 8),
            makeFxParam("Target Note", "Destination note pitch.", FxParamKind::Note, 0, 0x47, 0x30)),
        .paramCount = 3,
    },
    FxEffectSpec{
        .id = nspc::VcmdPercussionBaseInstrument::id,
        .name = "Percussion Base Instrument",
        .description = "Sets the base instrument used by percussion notes.",
        .params = makeFxParams(
            makeFxParam("Instrument", "Instrument index to use as percussion base.", FxParamKind::UnsignedByte, 0, 255, 0)),
        .paramCount = 1,
    },
    FxEffectSpec{
        .id = nspc::VcmdMuteChannel::id,
        .name = "Mute Channel",
        .description = "Immediately mutes this channel.",
        .params = makeFxParams(),
        .paramCount = 0,
    },
    FxEffectSpec{
        .id = nspc::VcmdFastForwardOn::id,
        .name = "Fast Forward On",
        .description = "Enables fast-forward mode (engine-specific behavior).",
        .params = makeFxParams(),
        .paramCount = 0,
    },
    FxEffectSpec{
        .id = nspc::VcmdFastForwardOff::id,
        .name = "Fast Forward Off",
        .description = "Disables fast-forward mode (engine-specific behavior).",
        .params = makeFxParams(),
        .paramCount = 0,
    },
}};

const FxEffectSpec* findFxEffectSpec(uint8_t id) {
    const auto it =
        std::find_if(kFxEffectSpecs.begin(), kFxEffectSpecs.end(), [id](const FxEffectSpec& spec) { return spec.id == id; });
    if (it == kFxEffectSpecs.end()) {
        return nullptr;
    }
    return &(*it);
}

int decodeFxParamValue(const FxParamSpec& spec, uint8_t rawValue) {
    if (spec.kind == FxParamKind::SignedByte) {
        return static_cast<int>(static_cast<int8_t>(rawValue));
    }
    return static_cast<int>(rawValue);
}

uint8_t encodeFxParamValue(const FxParamSpec& spec, int editorValue) {
    const int clamped = std::clamp(editorValue, spec.minValue, spec.maxValue);
    if (spec.kind == FxParamKind::SignedByte) {
        return static_cast<uint8_t>(static_cast<int8_t>(clamped));
    }
    return static_cast<uint8_t>(clamped & 0xFF);
}

std::string formatFxParamValue(const FxParamSpec& spec, uint8_t rawValue) {
    switch (spec.kind) {
    case FxParamKind::SignedByte:
        return std::format("{} (hex {:02X})", static_cast<int>(static_cast<int8_t>(rawValue)), rawValue);
    case FxParamKind::Note:
        return std::format("{} (hex {:02X})", noteToString(static_cast<int>(rawValue)), rawValue);
    case FxParamKind::ChannelMask:
        return std::format("mask ${:02X}", rawValue);
    case FxParamKind::FirIndex:
        return std::format("index {} (hex {:02X})", rawValue, rawValue);
    case FxParamKind::UnsignedByte:
    default:
        return std::format("{} (hex {:02X})", rawValue, rawValue);
    }
}

std::optional<nspc::Vcmd> buildVcmdFromRaw(uint8_t id, const std::array<uint8_t, 4>& params) {
    if (id == nspc::VcmdNOP::id) {
        const uint16_t nopBytes = static_cast<uint16_t>(params[0]) | (static_cast<uint16_t>(params[1]) << 8u);
        return nspc::Vcmd{nspc::VcmdNOP{.nopBytes = nopBytes}};
    }
    if (id == nspc::VcmdUnused::id) {
        return nspc::Vcmd{nspc::VcmdUnused{}};
    }
    return nspc::constructVcmd(id, params.data());
}

struct InstrumentRemapScopeIds {
    std::unordered_set<int> trackIds;
    std::unordered_set<int> subroutineIds;
};

const nspc::NspcInstrument* findInstrumentById(const std::vector<nspc::NspcInstrument>& instruments, int id) {
    const auto it =
        std::find_if(instruments.begin(), instruments.end(), [id](const nspc::NspcInstrument& instrument) { return instrument.id == id; });
    if (it == instruments.end()) {
        return nullptr;
    }
    return &(*it);
}

std::string instrumentDisplayLabel(const std::vector<nspc::NspcInstrument>& instruments, int id) {
    if (const auto* inst = findInstrumentById(instruments, id); inst != nullptr) {
        return std::format("${:02X} {}", id & 0xFF, inst->name.empty() ? "(unnamed)" : inst->name);
    }
    return std::format("${:02X} (missing)", id & 0xFF);
}

bool remapInEventStream(std::vector<nspc::NspcEventEntry>& events, const std::array<int, 256>& remap) {
    bool changed = false;
    for (auto& entry : events) {
        auto* vcmd = std::get_if<nspc::Vcmd>(&entry.event);
        if (!vcmd) {
            continue;
        }
        if (auto* inst = std::get_if<nspc::VcmdInst>(&vcmd->vcmd)) {
            const int mapped = remap[inst->instrumentIndex];
            if (mapped >= 0 && mapped != inst->instrumentIndex) {
                inst->instrumentIndex = static_cast<uint8_t>(mapped);
                changed = true;
            }
            continue;
        }
        if (auto* base = std::get_if<nspc::VcmdPercussionBaseInstrument>(&vcmd->vcmd)) {
            const int mapped = remap[base->index];
            if (mapped >= 0 && mapped != base->index) {
                base->index = static_cast<uint8_t>(mapped);
                changed = true;
            }
        }
    }
    return changed;
}

void collectSubroutineCalls(const std::vector<nspc::NspcEventEntry>& events, std::unordered_set<int>& outSubroutineIds) {
    for (const auto& entry : events) {
        const auto* vcmd = std::get_if<nspc::Vcmd>(&entry.event);
        if (!vcmd) {
            continue;
        }
        const auto* call = std::get_if<nspc::VcmdSubroutineCall>(&vcmd->vcmd);
        if (!call) {
            continue;
        }
        if (call->subroutineId >= 0) {
            outSubroutineIds.insert(call->subroutineId);
        }
    }
}

InstrumentRemapScopeIds collectInstrumentRemapScopeIds(const nspc::NspcSong& song, std::optional<int> channelScope) {
    InstrumentRemapScopeIds ids;

    if (!channelScope.has_value()) {
        for (const auto& track : song.tracks()) {
            ids.trackIds.insert(track.id);
        }
        for (const auto& subroutine : song.subroutines()) {
            ids.subroutineIds.insert(subroutine.id);
        }
        return ids;
    }

    const int channel = std::clamp(*channelScope, 0, 7);
    for (const auto& pattern : song.patterns()) {
        if (!pattern.channelTrackIds.has_value()) {
            continue;
        }
        const int trackId = pattern.channelTrackIds.value()[static_cast<size_t>(channel)];
        if (trackId >= 0) {
            ids.trackIds.insert(trackId);
        }
    }

    std::unordered_map<int, const nspc::NspcSubroutine*> subroutineById;
    subroutineById.reserve(song.subroutines().size());
    for (const auto& subroutine : song.subroutines()) {
        subroutineById[subroutine.id] = &subroutine;
    }

    std::vector<int> stack;
    stack.reserve(32);

    for (const auto& track : song.tracks()) {
        if (!ids.trackIds.contains(track.id)) {
            continue;
        }
        std::unordered_set<int> directCalls;
        collectSubroutineCalls(track.events, directCalls);
        for (const int subroutineId : directCalls) {
            if (ids.subroutineIds.insert(subroutineId).second) {
                stack.push_back(subroutineId);
            }
        }
    }

    while (!stack.empty()) {
        const int subroutineId = stack.back();
        stack.pop_back();

        const auto it = subroutineById.find(subroutineId);
        if (it == subroutineById.end()) {
            continue;
        }

        std::unordered_set<int> nestedCalls;
        collectSubroutineCalls(it->second->events, nestedCalls);
        for (const int nestedId : nestedCalls) {
            if (ids.subroutineIds.insert(nestedId).second) {
                stack.push_back(nestedId);
            }
        }
    }

    return ids;
}

std::array<int, 256> countUsedInstruments(const nspc::NspcSong& song, std::optional<int> channelScope) {
    std::array<int, 256> counts{};
    counts.fill(0);

    const InstrumentRemapScopeIds scopeIds = collectInstrumentRemapScopeIds(song, channelScope);
    auto collect = [&](const std::vector<nspc::NspcEventEntry>& events) {
        for (const auto& entry : events) {
            const auto* vcmd = std::get_if<nspc::Vcmd>(&entry.event);
            if (!vcmd) {
                continue;
            }
            if (const auto* inst = std::get_if<nspc::VcmdInst>(&vcmd->vcmd)) {
                ++counts[inst->instrumentIndex];
            } else if (const auto* base = std::get_if<nspc::VcmdPercussionBaseInstrument>(&vcmd->vcmd)) {
                ++counts[base->index];
            }
        }
    };

    for (const auto& track : song.tracks()) {
        if (scopeIds.trackIds.contains(track.id)) {
            collect(track.events);
        }
    }
    for (const auto& subroutine : song.subroutines()) {
        if (scopeIds.subroutineIds.contains(subroutine.id)) {
            collect(subroutine.events);
        }
    }

    return counts;
}

bool flatPatternHasAnyTimedEvents(const std::optional<nspc::NspcFlatPattern>& flatPattern) {
    if (!flatPattern.has_value()) {
        return false;
    }

    for (const auto& channel : flatPattern->channels) {
        for (const auto& event : channel.events) {
            if (std::holds_alternative<nspc::Note>(event.event) || std::holds_alternative<nspc::Tie>(event.event) ||
                std::holds_alternative<nspc::Rest>(event.event) ||
                std::holds_alternative<nspc::Percussion>(event.event)) {
                return true;
            }
        }
    }

    return false;
}

class SetPatternLengthCommand final : public nspc::NspcCommand {
public:
    SetPatternLengthCommand(int patternId, uint32_t targetTick) : patternId_(patternId), targetTick_(targetTick) {}

    bool execute(nspc::NspcSong& song) override {
        if (capturedBefore_) {
            restore(song, afterState_);
            return true;
        }

        beforeState_ = capture(song);
        nspc::NspcEditor editor;
        const bool changed = editor.setPatternLength(song, patternId_, targetTick_);
        if (!changed) {
            return false;
        }

        afterState_ = capture(song);
        capturedBefore_ = true;
        return true;
    }

    bool undo(nspc::NspcSong& song) override {
        if (!capturedBefore_) {
            return false;
        }
        restore(song, beforeState_);
        return true;
    }

    [[nodiscard]] std::string description() const override {
        return std::format("Set Pattern Length {}", targetTick_);
    }

private:
    struct SongState {
        std::vector<nspc::NspcPattern> patterns;
        std::vector<nspc::NspcTrack> tracks;
        nspc::NspcContentOrigin contentOrigin = nspc::NspcContentOrigin::EngineProvided;
    };

    static SongState capture(const nspc::NspcSong& song) {
        return SongState{
            .patterns = song.patterns(),
            .tracks = song.tracks(),
            .contentOrigin = song.contentOrigin(),
        };
    }

    static void restore(nspc::NspcSong& song, const SongState& state) {
        song.patterns() = state.patterns;
        song.tracks() = state.tracks;
        song.setContentOrigin(state.contentOrigin);
    }

    int patternId_ = -1;
    uint32_t targetTick_ = 0;
    SongState beforeState_{};
    SongState afterState_{};
    bool capturedBefore_ = false;
};

class SongInstrumentRemapCommand final : public nspc::NspcCommand {
public:
    SongInstrumentRemapCommand(std::vector<std::pair<uint8_t, uint8_t>> mappings, std::optional<int> channelScope)
        : channelScope_(channelScope) {
        remap_.fill(-1);
        for (const auto& mapping : mappings) {
            remap_[mapping.first] = mapping.second;
        }
    }

    bool execute(nspc::NspcSong& song) override {
        if (capturedBefore_) {
            restore(song, afterState_);
            return true;
        }

        beforeState_ = capture(song);
        const bool changed = apply(song);
        if (!changed) {
            return false;
        }

        afterState_ = capture(song);
        capturedBefore_ = true;
        return true;
    }

    bool undo(nspc::NspcSong& song) override {
        if (!capturedBefore_) {
            return false;
        }
        restore(song, beforeState_);
        return true;
    }

    [[nodiscard]] std::string description() const override {
        if (channelScope_.has_value()) {
            return std::format("Remap Song Instruments (Ch {})", *channelScope_ + 1);
        }
        return "Remap Song Instruments";
    }

private:
    struct SongState {
        std::vector<nspc::NspcTrack> tracks;
        std::vector<nspc::NspcSubroutine> subroutines;
        nspc::NspcContentOrigin contentOrigin = nspc::NspcContentOrigin::EngineProvided;
    };

    static SongState capture(const nspc::NspcSong& song) {
        return SongState{
            .tracks = song.tracks(),
            .subroutines = song.subroutines(),
            .contentOrigin = song.contentOrigin(),
        };
    }

    static void restore(nspc::NspcSong& song, const SongState& state) {
        song.tracks() = state.tracks;
        song.subroutines() = state.subroutines;
        song.setContentOrigin(state.contentOrigin);
    }

    bool apply(nspc::NspcSong& song) const {
        const InstrumentRemapScopeIds scopeIds = collectInstrumentRemapScopeIds(song, channelScope_);
        bool changed = false;

        for (auto& track : song.tracks()) {
            if (scopeIds.trackIds.contains(track.id)) {
                changed = remapInEventStream(track.events, remap_) || changed;
            }
        }
        for (auto& subroutine : song.subroutines()) {
            if (scopeIds.subroutineIds.contains(subroutine.id)) {
                changed = remapInEventStream(subroutine.events, remap_) || changed;
            }
        }

        if (changed) {
            song.setContentOrigin(nspc::NspcContentOrigin::UserProvided);
        }
        return changed;
    }

    std::array<int, 256> remap_{};
    std::optional<int> channelScope_;
    SongState beforeState_{};
    SongState afterState_{};
    bool capturedBefore_ = false;
};

class SongMutationCommand final : public nspc::NspcCommand {
public:
    using Mutator = std::function<bool(nspc::NspcSong&)>;

    SongMutationCommand(std::string description, Mutator mutator)
        : description_(std::move(description)), mutator_(std::move(mutator)) {}

    bool execute(nspc::NspcSong& song) override {
        if (capturedBefore_) {
            restore(song, afterState_);
            return true;
        }

        beforeState_ = capture(song);
        const bool changed = mutator_(song);
        if (!changed) {
            return false;
        }

        afterState_ = capture(song);
        capturedBefore_ = true;
        return true;
    }

    bool undo(nspc::NspcSong& song) override {
        if (!capturedBefore_) {
            return false;
        }
        restore(song, beforeState_);
        return true;
    }

    [[nodiscard]] std::string description() const override {
        return description_;
    }

private:
    struct SongState {
        std::vector<nspc::NspcPattern> patterns;
        std::vector<nspc::NspcTrack> tracks;
        std::vector<nspc::NspcSubroutine> subroutines;
        nspc::NspcContentOrigin contentOrigin = nspc::NspcContentOrigin::EngineProvided;
    };

    static SongState capture(const nspc::NspcSong& song) {
        return SongState{
            .patterns = song.patterns(),
            .tracks = song.tracks(),
            .subroutines = song.subroutines(),
            .contentOrigin = song.contentOrigin(),
        };
    }

    static void restore(nspc::NspcSong& song, const SongState& state) {
        song.patterns() = state.patterns;
        song.tracks() = state.tracks;
        song.subroutines() = state.subroutines;
        song.setContentOrigin(state.contentOrigin);
    }

    std::string description_;
    Mutator mutator_;
    SongState beforeState_{};
    SongState afterState_{};
    bool capturedBefore_ = false;
};

}  // namespace

PatternEditorPanel::EffectChip PatternEditorPanel::makeEffectChipFromVcmd(const nspc::Vcmd& cmd) const {
    EffectChip chip{
        .label = vcmdChipText(cmd),
        .tooltip = vcmdTooltipText(cmd),
        .category = vcmdCategory(cmd),
    };
    if (const auto raw = rawVcmdBytes(cmd); raw.has_value()) {
        chip.id = raw->id;
        chip.params = raw->params;
        chip.paramCount = raw->paramCount;
    }
    if (const auto* call = std::get_if<nspc::VcmdSubroutineCall>(&cmd.vcmd); call != nullptr) {
        chip.subroutineId = call->subroutineId;
        chip.label = std::format("Sub{}x{}", call->subroutineId, static_cast<uint16_t>(call->count));
    } else if (const auto* ext = std::get_if<nspc::VcmdExtension>(&cmd.vcmd); ext != nullptr) {
        if (const auto* info = extensionVcmdInfoForCurrentEngine(ext->id); info != nullptr && !info->name.empty()) {
            std::string shortName = info->name.substr(0, 3);
            std::string params;
            for (uint8_t i = 0; i < ext->paramCount && i < ext->params.size(); ++i) {
                params += std::format(" {}", hex2(ext->params[i]));
            }
            chip.label = std::format("{}{}", shortName, params);
        }
    }
    return chip;
}

std::optional<nspc::Vcmd> PatternEditorPanel::reconstructVcmdFromEffectChip(const EffectChip& chip) const {
    if (chip.id == nspc::VcmdSubroutineCall::id) {
        int subroutineId = chip.subroutineId.value_or(-1);
        if (subroutineId < 0) {
            const uint16_t address =
                static_cast<uint16_t>(chip.params[0]) | (static_cast<uint16_t>(chip.params[1]) << 8u);
            const auto resolvedId = resolveSubroutineIdForAddress(address);
            if (!resolvedId.has_value()) {
                return std::nullopt;
            }
            subroutineId = *resolvedId;
        }

        uint16_t originalAddr =
            static_cast<uint16_t>(chip.params[0]) | (static_cast<uint16_t>(chip.params[1]) << 8u);
        if (const auto resolvedAddr = resolveSubroutineAddressForId(subroutineId); resolvedAddr.has_value()) {
            originalAddr = *resolvedAddr;
        }

        return nspc::Vcmd{nspc::VcmdSubroutineCall{
            .subroutineId = subroutineId,
            .originalAddr = originalAddr,
            .count = chip.params[2],
        }};
    }

    if (auto vcmd = buildVcmdFromRawForCurrentEngine(chip.id, chip.params, chip.paramCount)) {
        return vcmd;
    }

    // Fallback: preserve raw bytes as VcmdExtension so unknown effects aren't silently dropped.
    return nspc::Vcmd{nspc::VcmdExtension{
        .id = chip.id,
        .params = chip.params,
        .paramCount = chip.paramCount,
    }};
}

const nspc::NspcEngineExtensionVcmd* PatternEditorPanel::extensionVcmdInfoForCurrentEngine(uint8_t id) const {
    if (!appState_.project.has_value()) {
        return nullptr;
    }
    return nspc::findEngineExtensionVcmd(appState_.project->engineConfig(), id, true);
}

std::optional<uint8_t> PatternEditorPanel::extensionParamCountForCurrentEngine(uint8_t id) const {
    if (const auto* extension = extensionVcmdInfoForCurrentEngine(id); extension != nullptr) {
        return extension->paramCount;
    }
    return std::nullopt;
}

std::optional<int> PatternEditorPanel::resolveSubroutineIdForAddress(uint16_t address) const {
    if (!appState_.project.has_value()) {
        return std::nullopt;
    }
    const auto& songs = appState_.project->songs();
    if (appState_.selectedSongIndex < 0 || appState_.selectedSongIndex >= static_cast<int>(songs.size())) {
        return std::nullopt;
    }
    const auto& subroutines = songs[static_cast<size_t>(appState_.selectedSongIndex)].subroutines();
    const auto it = std::find_if(subroutines.begin(), subroutines.end(),
                                 [address](const nspc::NspcSubroutine& subroutine) { return subroutine.originalAddr == address; });
    if (it == subroutines.end()) {
        return std::nullopt;
    }
    return it->id;
}

std::optional<uint16_t> PatternEditorPanel::resolveSubroutineAddressForId(int subroutineId) const {
    if (!appState_.project.has_value()) {
        return std::nullopt;
    }
    const auto& songs = appState_.project->songs();
    if (appState_.selectedSongIndex < 0 || appState_.selectedSongIndex >= static_cast<int>(songs.size())) {
        return std::nullopt;
    }
    const auto& subroutines = songs[static_cast<size_t>(appState_.selectedSongIndex)].subroutines();
    const auto it =
        std::find_if(subroutines.begin(), subroutines.end(),
                     [subroutineId](const nspc::NspcSubroutine& subroutine) { return subroutine.id == subroutineId; });
    if (it == subroutines.end()) {
        return std::nullopt;
    }
    return it->originalAddr;
}

bool PatternEditorPanel::rebuildSubroutineChip(EffectChip& chip) const {
    if (chip.id != nspc::VcmdSubroutineCall::id) {
        return false;
    }

    if (!chip.subroutineId.has_value() || *chip.subroutineId < 0) {
        const uint16_t address = static_cast<uint16_t>(chip.params[0]) | (static_cast<uint16_t>(chip.params[1]) << 8u);
        const auto resolvedId = resolveSubroutineIdForAddress(address);
        if (!resolvedId.has_value()) {
            return false;
        }
        chip.subroutineId = *resolvedId;
    }

    uint16_t address = static_cast<uint16_t>(chip.params[0]) | (static_cast<uint16_t>(chip.params[1]) << 8u);
    if (const auto resolvedAddr = resolveSubroutineAddressForId(*chip.subroutineId); resolvedAddr.has_value()) {
        address = *resolvedAddr;
    }

    const nspc::Vcmd callVcmd{nspc::VcmdSubroutineCall{
        .subroutineId = *chip.subroutineId,
        .originalAddr = address,
        .count = chip.params[2],
    }};
    chip = makeEffectChipFromVcmd(callVcmd);
    return true;
}

std::optional<uint8_t> PatternEditorPanel::fxParamCountForCurrentEngine(uint8_t id) const {
    if (const auto extensionParamCount = extensionParamCountForCurrentEngine(id); extensionParamCount.has_value()) {
        return extensionParamCount;
    }
    if (isEditableFxId(id)) {
        return static_cast<uint8_t>(nspc::vcmdParamByteCount(id));
    }
    return std::nullopt;
}

std::optional<std::pair<uint8_t, size_t>> PatternEditorPanel::decodeTypedFxLeadForCurrentEngine(
    std::string_view hexDigits) const {
    if (hexDigits.size() < 2) {
        return std::nullopt;
    }

    const auto parseByte = [](std::string_view value) -> uint8_t {
        return static_cast<uint8_t>(parseHexValue(value) & 0xFF);
    };

    const uint8_t firstId = parseByte(hexDigits.substr(0, 2));
    if (firstId == 0xFF) {
        if (hexDigits.size() < 4) {
            return std::nullopt;
        }

        const uint8_t extensionId = parseByte(hexDigits.substr(2, 2));
        if (extensionParamCountForCurrentEngine(extensionId).has_value()) {
            return std::pair<uint8_t, size_t>{extensionId, 4};
        }
        return std::nullopt;
    }

    return std::pair<uint8_t, size_t>{firstId, 2};
}

bool PatternEditorPanel::drawFxTypePickerCombo(const char* label, uint8_t& selectedId) const {
    auto displayLabelForId = [&](uint8_t id) -> std::string {
        if (const auto* extension = extensionVcmdInfoForCurrentEngine(id); extension != nullptr) {
            if (!extension->name.empty()) {
                return std::format("{} (Ext ${:02X})", extension->name, id);
            }
            return std::format("Extension ${:02X}", id);
        }
        if (const auto* spec = findFxEffectSpec(id); spec != nullptr) {
            return spec->name;
        }
        if (const char* shortName = nspc::vcmdNameForId(id); shortName != nullptr) {
            return std::format("{} (${:02X})", shortName, id);
        }
        return std::format("${:02X}", id);
    };

    const std::string preview = displayLabelForId(selectedId);
    if (!ImGui::BeginCombo(label, preview.c_str())) {
        return false;
    }

    bool changed = false;
    std::unordered_set<uint8_t> seenIds;

    if (appState_.project.has_value()) {
        for (const auto& extension : appState_.project->engineConfig().extensions) {
            if (!extension.enabled) {
                continue;
            }
            for (const auto& extensionVcmd : extension.vcmds) {
                if (!seenIds.insert(extensionVcmd.id).second) {
                    continue;
                }
                const std::string itemLabel = displayLabelForId(extensionVcmd.id);
                const bool isSelected = (selectedId == extensionVcmd.id);
                if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                    selectedId = extensionVcmd.id;
                    changed = true;
                }
                if (ImGui::IsItemHovered() && !extensionVcmd.description.empty()) {
                    ImGui::SetTooltip("%s", extensionVcmd.description.c_str());
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }
    }

    for (const auto& spec : kFxEffectSpecs) {
        if (seenIds.contains(spec.id)) {
            continue;
        }
        const bool isSelected = (selectedId == spec.id);
        if (ImGui::Selectable(spec.name, isSelected)) {
            selectedId = spec.id;
            changed = true;
        }
        if (ImGui::IsItemHovered() && spec.description[0] != '\0') {
            ImGui::SetTooltip("%s", spec.description);
        }
        if (isSelected) {
            ImGui::SetItemDefaultFocus();
        }
    }

    ImGui::EndCombo();
    return changed;
}

bool PatternEditorPanel::isEditableFxIdForCurrentEngine(uint8_t id) const {
    return fxParamCountForCurrentEngine(id).has_value();
}

std::optional<nspc::Vcmd> PatternEditorPanel::buildVcmdFromRawForCurrentEngine(
    uint8_t id, const std::array<uint8_t, 4>& params, std::optional<uint8_t> explicitParamCount) const {
    if (const auto* extension = extensionVcmdInfoForCurrentEngine(id); extension != nullptr) {
        const uint8_t paramCount = explicitParamCount.value_or(extension->paramCount);
        if (paramCount != extension->paramCount || paramCount > params.size()) {
            return std::nullopt;
        }
        return nspc::Vcmd{nspc::VcmdExtension{
            .id = id,
            .params = params,
            .paramCount = paramCount,
        }};
    }

    if (id == nspc::VcmdSubroutineCall::id) {
        const uint16_t address = static_cast<uint16_t>(params[0]) | (static_cast<uint16_t>(params[1]) << 8u);
        const auto subroutineId = resolveSubroutineIdForAddress(address);
        if (!subroutineId.has_value()) {
            return std::nullopt;
        }
        return nspc::Vcmd{nspc::VcmdSubroutineCall{
            .subroutineId = *subroutineId,
            .originalAddr = address,
            .count = params[2],
        }};
    }

    return buildVcmdFromRaw(id, params);
}

void PatternEditorPanel::requestFxEditorOpen(int row, int channel, int effectIndex) {
    if (row < 0 || channel < 0 || channel >= kChannels) {
        return;
    }
    fxEditorOpenRequested_ = true;
    fxEditorRequestRow_ = row;
    fxEditorRequestChannel_ = channel;
    fxEditorRequestEffectIndex_ = effectIndex;
}

void PatternEditorPanel::openFxEditorForCell(size_t row, int channel, int effectIndex) {
    if (row >= rows_.size() || channel < 0 || channel >= kChannels) {
        return;
    }
    fxEditorRow_ = static_cast<int>(row);
    fxEditorChannel_ = channel;
    fxEditorEffects_ = rows_[row][static_cast<size_t>(channel)].effects;
    if (effectIndex >= 0 && effectIndex < static_cast<int>(fxEditorEffects_.size())) {
        fxEditorSelectedIndex_ = effectIndex;
    } else {
        fxEditorSelectedIndex_ = fxEditorEffects_.empty() ? -1 : 0;
    }
    fxEditorStatus_.clear();
    if (fxEditorSelectedIndex_ >= 0) {
        const auto& fx = fxEditorEffects_[static_cast<size_t>(fxEditorSelectedIndex_)];
        if (isEditableFxIdForCurrentEngine(fx.id)) {
            fxEditorAddEffectId_ = fx.id;
        } else if (!kFxEffectSpecs.empty()) {
            fxEditorAddEffectId_ = kFxEffectSpecs.front().id;
        }
    } else if (!kFxEffectSpecs.empty()) {
        fxEditorAddEffectId_ = kFxEffectSpecs.front().id;
    }
}

bool PatternEditorPanel::applyFxEditorChanges(nspc::NspcSong& song, int patternId) {
    if (fxEditorRow_ < 0 || fxEditorChannel_ < 0 || fxEditorChannel_ >= kChannels) {
        return false;
    }
    nspc::NspcEditorLocation location{
        .patternId = patternId,
        .channel = fxEditorChannel_,
        .row = static_cast<uint32_t>(fxEditorRow_),
    };

    if (fxEditorEffects_.empty()) {
        auto cmd = std::make_unique<nspc::SetEffectsCommand>(location, std::vector<nspc::Vcmd>{}, false);
        (void)appState_.commandHistory.execute(song, std::move(cmd));
        return true;
    }

    std::vector<nspc::Vcmd> rebuilt;
    rebuilt.reserve(fxEditorEffects_.size());
    for (const auto& fx : fxEditorEffects_) {
        const auto vcmd = reconstructVcmdFromEffectChip(fx);
        if (!vcmd.has_value()) {
            return false;
        }
        rebuilt.push_back(*vcmd);
    }

    auto cmd = std::make_unique<nspc::SetEffectsCommand>(location, std::move(rebuilt), false);
    bool updated = appState_.commandHistory.execute(song, std::move(cmd));
    return updated;
}

void PatternEditorPanel::prepareFxEditorPopupRequest() {
    if (fxEditorOpenRequested_) {
        const int requestChannel = std::clamp(fxEditorRequestChannel_, 0, kChannels - 1);
        const int maxRow = std::max(0, static_cast<int>(rows_.size()) - 1);
        const int requestRow = std::clamp(fxEditorRequestRow_, 0, maxRow);
        openFxEditorForCell(static_cast<size_t>(requestRow), requestChannel, fxEditorRequestEffectIndex_);
        ImGui::OpenPopup("FX Editor");
        fxEditorOpenRequested_ = false;
        fxEditorRequestEffectIndex_ = -1;
    }
}

bool PatternEditorPanel::beginFxEditorPopupModal() {
    bool keepOpen = true;
    if (!ImGui::BeginPopupModal("FX Editor", &keepOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        return false;
    }
    if (!keepOpen) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return false;
    }
    return true;
}

bool PatternEditorPanel::hasSelectedFxEditorEffect() const {
    return fxEditorSelectedIndex_ >= 0 && fxEditorSelectedIndex_ < static_cast<int>(fxEditorEffects_.size());
}

void PatternEditorPanel::normalizeFxEditorSelection() {
    if (!hasSelectedFxEditorEffect() && !fxEditorEffects_.empty()) {
        fxEditorSelectedIndex_ = 0;
    }
}

bool PatternEditorPanel::rebuildFxEditorChipFromRaw(EffectChip& chip) {
    if (chip.id == nspc::VcmdSubroutineCall::id) {
        return rebuildSubroutineChip(chip);
    }

    const auto rebuilt = buildVcmdFromRawForCurrentEngine(chip.id, chip.params, chip.paramCount);
    if (!rebuilt.has_value()) {
        return false;
    }
    chip = makeEffectChipFromVcmd(*rebuilt);
    return true;
}

std::optional<PatternEditorPanel::EffectChip> PatternEditorPanel::createDefaultFxEditorChipForId(uint8_t effectId) const {
    if (const auto* extension = extensionVcmdInfoForCurrentEngine(effectId); extension != nullptr) {
        std::array<uint8_t, 4> params{};
        const auto vcmd = buildVcmdFromRawForCurrentEngine(effectId, params, extension->paramCount);
        if (!vcmd.has_value()) {
            return std::nullopt;
        }
        return makeEffectChipFromVcmd(*vcmd);
    }

    if (effectId == nspc::VcmdSubroutineCall::id) {
        if (!appState_.project.has_value()) {
            return std::nullopt;
        }
        const auto& songs = appState_.project->songs();
        if (appState_.selectedSongIndex < 0 || appState_.selectedSongIndex >= static_cast<int>(songs.size())) {
            return std::nullopt;
        }
        const auto& subroutines = songs[static_cast<size_t>(appState_.selectedSongIndex)].subroutines();
        if (subroutines.empty()) {
            return std::nullopt;
        }
        const auto& subroutine = subroutines.front();
        return makeEffectChipFromVcmd(nspc::Vcmd{nspc::VcmdSubroutineCall{
            .subroutineId = subroutine.id,
            .originalAddr = subroutine.originalAddr,
            .count = 1,
        }});
    }

    const auto* spec = findFxEffectSpec(effectId);
    if (spec == nullptr) {
        return std::nullopt;
    }
    std::array<uint8_t, 4> params{};
    for (size_t i = 0; i < spec->paramCount; ++i) {
        params[i] = encodeFxParamValue(spec->params[i], spec->params[i].defaultValue);
    }
    const auto vcmd = buildVcmdFromRawForCurrentEngine(spec->id, params, static_cast<uint8_t>(spec->paramCount));
    if (!vcmd.has_value()) {
        return std::nullopt;
    }
    return makeEffectChipFromVcmd(*vcmd);
}

std::string PatternEditorPanel::fxEditorEffectName(const EffectChip& chip) const {
    if (chip.id == nspc::VcmdSubroutineCall::id) {
        return std::format("Sub {} x{}", chip.subroutineId.value_or(-1), static_cast<uint16_t>(chip.params[2]));
    }
    if (const auto* extension = extensionVcmdInfoForCurrentEngine(chip.id); extension != nullptr) {
        if (!extension->name.empty()) {
            return std::format("{} (Ext ${:02X})", extension->name, static_cast<int>(chip.id));
        }
        return std::format("Extension (${:02X})", static_cast<int>(chip.id));
    }
    if (const auto* spec = findFxEffectSpec(chip.id); spec != nullptr) {
        return spec->name;
    }
    if (const char* shortName = nspc::vcmdNameForId(chip.id); shortName != nullptr) {
        return std::format("Unknown {} (${:02X})", shortName, static_cast<int>(chip.id));
    }
    return std::format("Unknown (${:02X})", static_cast<int>(chip.id));
}

std::string PatternEditorPanel::fxEditorEffectSummary(const EffectChip& chip) const {
    if (const auto* extension = extensionVcmdInfoForCurrentEngine(chip.id); extension != nullptr) {
        std::string summary = extension->description;
        if (summary.empty()) {
            summary = "Extension command";
        }
        summary += std::format("\nRaw: FF {}", vcmdInlineText(chip.id, chip.params, chip.paramCount));
        return summary;
    }
    if (chip.id == nspc::VcmdSubroutineCall::id) {
        const int subroutineId = chip.subroutineId.value_or(-1);
        const uint16_t address = static_cast<uint16_t>(chip.params[0]) | (static_cast<uint16_t>(chip.params[1]) << 8u);
        return std::format("Subroutine {} at ${:04X}, iterations {}", subroutineId, address,
                           static_cast<uint16_t>(chip.params[2]));
    }
    if (const auto* spec = findFxEffectSpec(chip.id); spec != nullptr) {
        std::string summary;
        for (size_t i = 0; i < spec->paramCount; ++i) {
            if (!summary.empty()) {
                summary += ", ";
            }
            summary += spec->params[i].label;
            summary += ": ";
            summary += formatFxParamValue(spec->params[i], chip.params[i]);
        }
        if (summary.empty()) {
            return "No parameters";
        }
        return summary;
    }
    return std::format("Raw bytes: {}", vcmdInlineText(chip.id, chip.params, chip.paramCount));
}

void PatternEditorPanel::drawFxEditorEffectList() {
    if (ImGui::BeginChild("fx_editor_list", ImVec2(600.0f, 170.0f), true)) {
        if (fxEditorEffects_.empty()) {
            ImGui::TextDisabled("No effects on this row. Add one below.");
        } else {
            for (size_t i = 0; i < fxEditorEffects_.size(); ++i) {
                const auto& fx = fxEditorEffects_[i];
                const std::string label = std::format("{:02d}. {}##fx_popup_{}", static_cast<int>(i) + 1,
                                                      fxEditorEffectName(fx), i);
                if (ImGui::Selectable(label.c_str(), fxEditorSelectedIndex_ == static_cast<int>(i),
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    fxEditorSelectedIndex_ = static_cast<int>(i);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n\n%s", fx.tooltip.c_str(), fxEditorEffectSummary(fx).c_str());
                }
            }
        }
    }
    ImGui::EndChild();
}

void PatternEditorPanel::drawFxEditorSelectedEffectSection() {
    if (!hasSelectedFxEditorEffect()) {
        ImGui::TextDisabled("Select an effect to edit it.");
        return;
    }

    auto& selected = fxEditorEffects_[static_cast<size_t>(fxEditorSelectedIndex_)];

    ImGui::TextUnformatted("Selected Effect");

    uint8_t selectedTypeId = selected.id;
    if (drawFxTypePickerCombo("Effect Type", selectedTypeId) && selectedTypeId != selected.id) {
        if (const auto newChip = createDefaultFxEditorChipForId(selectedTypeId); newChip.has_value()) {
            selected = *newChip;
            fxEditorStatus_.clear();
        } else {
            fxEditorStatus_ = "Failed to switch effect type";
        }
    }

    const auto* selectedExtension = extensionVcmdInfoForCurrentEngine(selected.id);
    const auto* selectedSpec = (selectedExtension == nullptr) ? findFxEffectSpec(selected.id) : nullptr;

    if (selected.id == nspc::VcmdSubroutineCall::id) {
        ImGui::TextWrapped("Calls a subroutine at this row.");
        ImGui::Separator();

        if (!appState_.project.has_value()) {
            ImGui::TextDisabled("Project not loaded.");
        } else {
            const auto& songs = appState_.project->songs();
            if (appState_.selectedSongIndex < 0 || appState_.selectedSongIndex >= static_cast<int>(songs.size())) {
                ImGui::TextDisabled("Song selection is out of range.");
            } else {
                const auto& subroutines = songs[static_cast<size_t>(appState_.selectedSongIndex)].subroutines();
                if (subroutines.empty()) {
                    ImGui::TextDisabled("This song has no subroutines.");
                } else {
                    int currentSubroutineId = selected.subroutineId.value_or(subroutines.front().id);
                    const auto currentIt = std::find_if(
                        subroutines.begin(), subroutines.end(),
                        [currentSubroutineId](const nspc::NspcSubroutine& subroutine) { return subroutine.id == currentSubroutineId; });
                    if (currentIt == subroutines.end()) {
                        currentSubroutineId = subroutines.front().id;
                    }

                    const auto selectedLabel = [&]() -> std::string {
                        const auto it = std::find_if(
                            subroutines.begin(), subroutines.end(),
                            [currentSubroutineId](const nspc::NspcSubroutine& subroutine) { return subroutine.id == currentSubroutineId; });
                        if (it == subroutines.end()) {
                            return std::format("Sub {}", currentSubroutineId);
                        }
                        return std::format("Sub {} (${:#06X})", it->id, static_cast<unsigned>(it->originalAddr));
                    };

                    if (ImGui::BeginCombo("Subroutine", selectedLabel().c_str())) {
                        for (const auto& subroutine : subroutines) {
                            const bool isSelected = (subroutine.id == currentSubroutineId);
                            const std::string label =
                                std::format("Sub {} (${:#06X})", subroutine.id, static_cast<unsigned>(subroutine.originalAddr));
                            if (ImGui::Selectable(label.c_str(), isSelected)) {
                                selected.subroutineId = subroutine.id;
                                selected.params[0] = static_cast<uint8_t>(subroutine.originalAddr & 0xFF);
                                selected.params[1] = static_cast<uint8_t>((subroutine.originalAddr >> 8u) & 0xFF);
                                selected.paramCount = 3;
                                if (!rebuildSubroutineChip(selected)) {
                                    fxEditorStatus_ = "Failed to update subroutine call";
                                } else {
                                    fxEditorStatus_.clear();
                                }
                            }
                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
            }
        }

        int iterationCount = static_cast<int>(selected.params[2]);
        if (ImGui::SliderInt("Iterations", &iterationCount, 0, 255)) {
            selected.params[2] = static_cast<uint8_t>(iterationCount & 0xFF);
            selected.paramCount = 3;
            if (!rebuildSubroutineChip(selected)) {
                fxEditorStatus_ = "Failed to update iterations";
            } else {
                fxEditorStatus_.clear();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("0x%02X", selected.params[2]);
    } else if (selectedExtension != nullptr) {
        if (!selectedExtension->description.empty()) {
            ImGui::TextWrapped("%s", selectedExtension->description.c_str());
        } else {
            ImGui::TextDisabled("Extension command.");
        }
        ImGui::Separator();

        const uint8_t expectedParamCount = static_cast<uint8_t>(std::min<uint8_t>(selectedExtension->paramCount, 4u));
        if (selected.paramCount != expectedParamCount) {
            selected.paramCount = expectedParamCount;
            if (!rebuildFxEditorChipFromRaw(selected)) {
                fxEditorStatus_ = "Failed to update extension parameter count";
            }
        }

        if (selected.paramCount == 0) {
            ImGui::TextDisabled("No parameters for this effect.");
        } else {
            for (size_t i = 0; i < selected.paramCount; ++i) {
                int editorValue = selected.params[i];
                const std::string controlLabel = std::format("Param {}##fx_ext_param_{}", i + 1, i);
                if (ImGui::SliderInt(controlLabel.c_str(), &editorValue, 0, 255)) {
                    selected.params[i] = static_cast<uint8_t>(editorValue & 0xFF);
                    if (!rebuildFxEditorChipFromRaw(selected)) {
                        fxEditorStatus_ = "Failed to update parameter";
                    } else {
                        fxEditorStatus_.clear();
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled("0x%02X", selected.params[i]);
            }
        }
    } else if (selectedSpec != nullptr) {

        ImGui::TextWrapped("%s", selectedSpec->description);
        ImGui::Separator();

        if (selectedSpec->paramCount == 0) {
            ImGui::TextDisabled("No parameters for this effect.");
        } else {
            for (size_t i = 0; i < selectedSpec->paramCount; ++i) {
                const auto& paramSpec = selectedSpec->params[i];
                int editorValue = decodeFxParamValue(paramSpec, selected.params[i]);
                const std::string controlLabel = std::format("{}##fx_param_{}", paramSpec.label, i);
                if (ImGui::SliderInt(controlLabel.c_str(), &editorValue, paramSpec.minValue, paramSpec.maxValue)) {
                    selected.params[i] = encodeFxParamValue(paramSpec, editorValue);
                    if (!rebuildFxEditorChipFromRaw(selected)) {
                        fxEditorStatus_ = "Failed to update parameter";
                    } else {
                        fxEditorStatus_.clear();
                    }
                }
                if (ImGui::IsItemHovered() && paramSpec.help[0] != '\0') {
                    ImGui::SetTooltip("%s", paramSpec.help);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", formatFxParamValue(paramSpec, selected.params[i]).c_str());
            }
        }
    } else {
        ImGui::TextDisabled("This effect is unknown. You can keep, move, or delete it.");
        ImGui::TextDisabled("Raw command: %s", vcmdInlineText(selected.id, selected.params, selected.paramCount).c_str());
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%s", selected.tooltip.c_str());
}

void PatternEditorPanel::drawFxEditorAddSection() {
    auto setFallbackAddId = [&]() {
        if (appState_.project.has_value()) {
            for (const auto& extension : appState_.project->engineConfig().extensions) {
                if (!extension.enabled) {
                    continue;
                }
                for (const auto& extensionVcmd : extension.vcmds) {
                    fxEditorAddEffectId_ = extensionVcmd.id;
                    return;
                }
            }
        }
        if (!kFxEffectSpecs.empty()) {
            fxEditorAddEffectId_ = static_cast<int>(kFxEffectSpecs.front().id);
        }
    };

    if (!isEditableFxIdForCurrentEngine(static_cast<uint8_t>(fxEditorAddEffectId_))) {
        setFallbackAddId();
    }

    if (!isEditableFxIdForCurrentEngine(static_cast<uint8_t>(fxEditorAddEffectId_))) {
        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Add New Effect");
    uint8_t addId = static_cast<uint8_t>(fxEditorAddEffectId_);
    if (drawFxTypePickerCombo("Type##fx_add_type", addId)) {
        fxEditorAddEffectId_ = addId;
    }

    ImGui::SameLine();
    const char* addLabel = hasSelectedFxEditorEffect() ? "Add After Selected" : "Add";
    if (ImGui::Button(addLabel)) {
        if (const auto newChip = createDefaultFxEditorChipForId(addId); newChip.has_value()) {
            const size_t insertIndex =
                hasSelectedFxEditorEffect() ? static_cast<size_t>(fxEditorSelectedIndex_ + 1) : fxEditorEffects_.size();
            fxEditorEffects_.insert(fxEditorEffects_.begin() + static_cast<std::ptrdiff_t>(insertIndex), *newChip);
            fxEditorSelectedIndex_ = static_cast<int>(insertIndex);
            fxEditorStatus_.clear();
        } else {
            fxEditorStatus_ = "Failed to create new effect";
        }
    }

    if (const auto* addExtension = extensionVcmdInfoForCurrentEngine(addId); addExtension != nullptr) {
        ImGui::TextDisabled("%s", addExtension->description.empty() ? "Extension command." : addExtension->description.c_str());
    } else if (const auto* addSpec = findFxEffectSpec(addId); addSpec != nullptr) {
        ImGui::TextDisabled("%s", addSpec->description);
    }
}

void PatternEditorPanel::drawFxEditorEditActions() {
    ImGui::BeginDisabled(!hasSelectedFxEditorEffect());
    if (ImGui::Button("Delete")) {
        fxEditorEffects_.erase(fxEditorEffects_.begin() + static_cast<std::ptrdiff_t>(fxEditorSelectedIndex_));
        if (fxEditorEffects_.empty()) {
            fxEditorSelectedIndex_ = -1;
        } else {
            fxEditorSelectedIndex_ = std::clamp(fxEditorSelectedIndex_, 0, static_cast<int>(fxEditorEffects_.size()) - 1);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Move Up") && hasSelectedFxEditorEffect() && fxEditorSelectedIndex_ > 0) {
        std::swap(fxEditorEffects_[static_cast<size_t>(fxEditorSelectedIndex_)],
                  fxEditorEffects_[static_cast<size_t>(fxEditorSelectedIndex_ - 1)]);
        --fxEditorSelectedIndex_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Move Down") && hasSelectedFxEditorEffect() &&
        fxEditorSelectedIndex_ + 1 < static_cast<int>(fxEditorEffects_.size())) {
        std::swap(fxEditorEffects_[static_cast<size_t>(fxEditorSelectedIndex_)],
                  fxEditorEffects_[static_cast<size_t>(fxEditorSelectedIndex_ + 1)]);
        ++fxEditorSelectedIndex_;
    }
    ImGui::EndDisabled();
}

bool PatternEditorPanel::applyFxEditorPopupChanges(nspc::NspcSong& song, int patternId, bool closeAfterApply) {
    if (!applyFxEditorChanges(song, patternId)) {
        fxEditorStatus_ = "Failed to apply effects";
        return false;
    }

    rebuildPatternRows(song, patternId);
    selectedRow_ = fxEditorRow_;
    selectedChannel_ = fxEditorChannel_;
    selectedItem_ = 4;
    hexInput_.clear();
    if (closeAfterApply) {
        ImGui::CloseCurrentPopup();
        return true;
    }

    fxEditorStatus_ = "Applied";
    openFxEditorForCell(static_cast<size_t>(std::max(fxEditorRow_, 0)), fxEditorChannel_);
    return true;
}

void PatternEditorPanel::drawFxEditorPopup(nspc::NspcSong& song, int patternId) {
    prepareFxEditorPopupRequest();

    if (!beginFxEditorPopupModal()) {
        return;
    }

    ImGui::Text("Row %04X | Ch %d", std::max(fxEditorRow_, 0), std::max(fxEditorChannel_, 0) + 1);
    ImGui::TextDisabled("Pick an effect by name, then adjust its parameters.");
    ImGui::Separator();

    normalizeFxEditorSelection();
    drawFxEditorEffectList();

    ImGui::Spacing();
    drawFxEditorSelectedEffectSection();
    drawFxEditorAddSection();

    ImGui::Spacing();
    drawFxEditorEditActions();

    ImGui::Separator();
    if (ImGui::Button("Apply")) {
        (void)applyFxEditorPopupChanges(song, patternId, false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply & Close")) {
        (void)applyFxEditorPopupChanges(song, patternId, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
    }

    if (!fxEditorStatus_.empty()) {
        ImGui::TextDisabled("%s", fxEditorStatus_.c_str());
    }
    ImGui::EndPopup();
}

size_t PatternEditorPanel::selectionIndex(int row, int channel, int item) const {
    const size_t flatColumnCount = static_cast<size_t>(kChannels * kEditItems);
    return static_cast<size_t>(row) * flatColumnCount + static_cast<size_t>(channel * kEditItems + item);
}

void PatternEditorPanel::ensureSelectionStorage() {
    const size_t desiredSize = rows_.size() * static_cast<size_t>(kChannels * kEditItems);
    if (selectedCells_.size() == desiredSize) {
        return;
    }
    selectedCells_.assign(desiredSize, 0);
    if (desiredSize == 0) {
        selectionAnchorValid_ = false;
        mouseSelecting_ = false;
    }
}

void PatternEditorPanel::clearCellSelection() {
    std::fill(selectedCells_.begin(), selectedCells_.end(), 0);
}

bool PatternEditorPanel::hasCellSelection() const {
    return std::any_of(selectedCells_.begin(), selectedCells_.end(), [](uint8_t value) { return value != 0; });
}

bool PatternEditorPanel::isCellSelected(int row, int channel, int item) const {
    if (row < 0 || row >= static_cast<int>(rows_.size())) {
        return false;
    }
    if (channel < 0 || channel >= kChannels) {
        return false;
    }
    if (item < 0 || item >= kEditItems) {
        return false;
    }
    if (selectedCells_.empty()) {
        return false;
    }
    return selectedCells_[selectionIndex(row, channel, item)] != 0;
}

void PatternEditorPanel::setCellSelected(int row, int channel, int item, bool selected) {
    if (row < 0 || row >= static_cast<int>(rows_.size())) {
        return;
    }
    if (channel < 0 || channel >= kChannels) {
        return;
    }
    if (item < 0 || item >= kEditItems) {
        return;
    }
    if (selectedCells_.empty()) {
        return;
    }
    selectedCells_[selectionIndex(row, channel, item)] = selected ? 1 : 0;
}

void PatternEditorPanel::selectSingleCell(int row, int channel, int item, bool resetAnchor) {
    if (row < 0 || row >= static_cast<int>(rows_.size())) {
        return;
    }
    if (channel < 0 || channel >= kChannels) {
        return;
    }
    if (item < 0 || item >= kEditItems) {
        return;
    }
    ensureSelectionStorage();
    clearCellSelection();
    setCellSelected(row, channel, item, true);
    selectedRow_ = row;
    selectedChannel_ = channel;
    selectedItem_ = item;
    if (resetAnchor) {
        selectionAnchorValid_ = true;
        selectionAnchor_ = SelectionCell{.row = row, .channel = channel, .item = item};
    }
}

void PatternEditorPanel::selectRange(const SelectionCell& anchor, const SelectionCell& focus, bool additive) {
    if (rows_.empty()) {
        return;
    }
    ensureSelectionStorage();
    if (!additive) {
        clearCellSelection();
    }

    const int minRow = std::clamp(std::min(anchor.row, focus.row), 0, static_cast<int>(rows_.size()) - 1);
    const int maxRow = std::clamp(std::max(anchor.row, focus.row), 0, static_cast<int>(rows_.size()) - 1);
    const int anchorCol = std::clamp(anchor.channel * kEditItems + anchor.item, 0, kChannels * kEditItems - 1);
    const int focusCol = std::clamp(focus.channel * kEditItems + focus.item, 0, kChannels * kEditItems - 1);
    const int minCol = std::min(anchorCol, focusCol);
    const int maxCol = std::max(anchorCol, focusCol);

    for (int row = minRow; row <= maxRow; ++row) {
        for (int flatCol = minCol; flatCol <= maxCol; ++flatCol) {
            const int channel = flatCol / kEditItems;
            const int item = flatCol % kEditItems;
            setCellSelected(row, channel, item, true);
        }
    }

    selectedRow_ = std::clamp(focus.row, 0, static_cast<int>(rows_.size()) - 1);
    selectedChannel_ = std::clamp(focus.channel, 0, kChannels - 1);
    selectedItem_ = std::clamp(focus.item, 0, kEditItems - 1);
}

void PatternEditorPanel::handleCellSelectionInput(int row, int channel, int item, bool clicked, bool hovered) {
    if (rows_.empty()) {
        return;
    }

    if (clicked) {
        const ImGuiIO& io = ImGui::GetIO();
        const bool additive = io.KeyCtrl || io.KeySuper;
        const bool extend = io.KeyShift;
        const SelectionCell clickedCell{.row = row, .channel = channel, .item = item};

        if (extend && selectionAnchorValid_) {
            selectRange(selectionAnchor_, clickedCell, additive);
        } else if (additive) {
            ensureSelectionStorage();
            const bool selected = isCellSelected(row, channel, item);
            setCellSelected(row, channel, item, !selected);
            selectedRow_ = row;
            selectedChannel_ = channel;
            selectedItem_ = item;
            if (!selectionAnchorValid_) {
                selectionAnchorValid_ = true;
                selectionAnchor_ = clickedCell;
            }
        } else {
            selectSingleCell(row, channel, item, true);
        }

        hexInput_.clear();
        mouseSelecting_ = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        mouseSelectionAdditive_ = additive;
        mouseSelectionAnchor_ = (extend && selectionAnchorValid_) ? selectionAnchor_ : clickedCell;
        return;
    }

    if (mouseSelecting_ && ImGui::IsMouseDown(ImGuiMouseButton_Left) && hovered) {
        selectRange(mouseSelectionAnchor_, SelectionCell{.row = row, .channel = channel, .item = item},
                    mouseSelectionAdditive_);
    }

    if (mouseSelecting_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        mouseSelecting_ = false;
    }
}

void PatternEditorPanel::updateSelectionFromCursor(bool extending) {
    if (rows_.empty()) {
        return;
    }
    if (!extending) {
        selectSingleCell(selectedRow_, selectedChannel_, selectedItem_, true);
        return;
    }

    if (!selectionAnchorValid_) {
        selectionAnchorValid_ = true;
        selectionAnchor_ = SelectionCell{.row = selectedRow_, .channel = selectedChannel_, .item = selectedItem_};
    }

    selectRange(selectionAnchor_,
                SelectionCell{.row = selectedRow_, .channel = selectedChannel_, .item = selectedItem_}, false);
}

std::optional<nspc::NspcRowEvent> PatternEditorPanel::parseRowEventFromCell(const PatternCell& cell) const {
    if (isTieMarker(cell.note)) {
        return nspc::Tie{};
    }
    if (isRestMarker(cell.note)) {
        return nspc::Rest{};
    }
    if (cell.note == "..." || cell.note == "---") {
        return std::nullopt;
    }
    if (cell.note.size() == 3 && cell.note[0] == 'P') {
        const auto idx = parseHexByte(std::string_view(cell.note).substr(1, 2));
        if (idx.has_value()) {
            return nspc::Percussion{.index = *idx};
        }
        return std::nullopt;
    }
    if (cell.note.size() != 3 || cell.note[2] < '0' || cell.note[2] > '9') {
        return std::nullopt;
    }

    static constexpr std::array<std::string_view, 12> kNames = {
        "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-",
    };
    const std::string_view pitchName(cell.note.data(), 2);
    const auto it = std::find(kNames.begin(), kNames.end(), pitchName);
    if (it == kNames.end()) {
        return std::nullopt;
    }
    const int semitone = static_cast<int>(std::distance(kNames.begin(), it));
    const int octave = cell.note[2] - '0';
    const int pitch = std::clamp(octave * 12 + semitone, 0, 0x47);
    return nspc::Note{static_cast<uint8_t>(pitch)};
}

std::optional<uint8_t> PatternEditorPanel::parseHexByte(std::string_view text) const {
    if (text.size() != 2) {
        return std::nullopt;
    }
    std::array<char, 2> upper{};
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (std::isxdigit(ch) == 0) {
            return std::nullopt;
        }
        upper[i] = static_cast<char>(std::toupper(ch));
    }
    return static_cast<uint8_t>(parseHexValue(std::string_view(upper.data(), upper.size())));
}

bool PatternEditorPanel::copyCellSelectionToClipboard() {
    if (rows_.empty()) {
        return false;
    }

    ensureSelectionStorage();
    if (!hasCellSelection() && selectedRow_ >= 0 && selectedChannel_ >= 0) {
        selectSingleCell(selectedRow_, selectedChannel_, selectedItem_, true);
    }
    if (!hasCellSelection()) {
        return false;
    }

    int minRow = static_cast<int>(rows_.size());
    int minFlatCol = kChannels * kEditItems;
    for (int row = 0; row < static_cast<int>(rows_.size()); ++row) {
        for (int channel = 0; channel < kChannels; ++channel) {
            for (int item = 0; item < kEditItems; ++item) {
                if (!isCellSelected(row, channel, item)) {
                    continue;
                }
                minRow = std::min(minRow, row);
                minFlatCol = std::min(minFlatCol, channel * kEditItems + item);
            }
        }
    }

    clipboardCells_.clear();
    for (int row = 0; row < static_cast<int>(rows_.size()); ++row) {
        for (int channel = 0; channel < kChannels; ++channel) {
            for (int item = 0; item < kEditItems; ++item) {
                if (!isCellSelected(row, channel, item)) {
                    continue;
                }
                const auto& cell = rows_[static_cast<size_t>(row)][static_cast<size_t>(channel)];
                ClipboardCell clip{
                    .rowOffset = row - minRow,
                    .flatColumnOffset = (channel * kEditItems + item) - minFlatCol,
                };
                const bool showInstVol =
                    canShowInstVol(cell.note) || cell.instrument != ".." || cell.volume != ".." || cell.qv != "..";
                switch (item) {
                case 0:
                    clip.rowEvent = parseRowEventFromCell(cell);
                    break;
                case 1: {
                    const std::string_view text = showInstVol ? std::string_view(cell.instrument) : "..";
                    clip.byteValue = parseHexByte(text);
                    break;
                }
                case 2: {
                    const std::string_view text = showInstVol ? std::string_view(cell.volume) : "..";
                    clip.byteValue = parseHexByte(text);
                    break;
                }
                case 3:
                    clip.byteValue = parseHexByte(showInstVol ? std::string_view(cell.qv) : "..");
                    break;
                case 4:
                    clip.effects = cell.effects;
                    break;
                default:
                    break;
                }
                clipboardCells_.push_back(std::move(clip));
            }
        }
    }

    std::sort(clipboardCells_.begin(), clipboardCells_.end(), [](const ClipboardCell& lhs, const ClipboardCell& rhs) {
        if (lhs.rowOffset != rhs.rowOffset) {
            return lhs.rowOffset < rhs.rowOffset;
        }
        return lhs.flatColumnOffset < rhs.flatColumnOffset;
    });

    clipboardHasData_ = !clipboardCells_.empty();
    return clipboardHasData_;
}

bool PatternEditorPanel::pasteClipboardAtCursor(nspc::NspcSong& song, int patternId) {
    if (!clipboardHasData_ || clipboardCells_.empty()) {
        return false;
    }
    if (rows_.empty() || selectedRow_ < 0 || selectedChannel_ < 0) {
        return false;
    }

    // Wrap entire paste operation in a transaction
    nspc::NspcCommandTransaction txn(appState_.commandHistory, "Paste");

    const int baseRow = std::clamp(selectedRow_, 0, static_cast<int>(rows_.size()) - 1);
    const int baseFlatCol = std::clamp(selectedChannel_ * kEditItems + selectedItem_, 0, kChannels * kEditItems - 1);
    bool updated = false;
    std::vector<uint8_t> pastedRowEventFlags(rows_.size() * static_cast<size_t>(kChannels), 0);
    auto row_channel_index = [&](int row, int channel) -> size_t {
        return static_cast<size_t>(row) * static_cast<size_t>(kChannels) + static_cast<size_t>(channel);
    };
    auto ensure_row_anchor = [&](const nspc::NspcEditorLocation& location, int row, int channel) {
        if (row < 0 || row >= static_cast<int>(rows_.size())) {
            return false;
        }
        if (channel < 0 || channel >= kChannels) {
            return false;
        }

        const bool hasVisibleRowEvent = rows_[static_cast<size_t>(row)][static_cast<size_t>(channel)].note != "...";
        const bool rowEventAlreadyPasted = pastedRowEventFlags[row_channel_index(row, channel)] != 0;
        if (hasVisibleRowEvent || rowEventAlreadyPasted) {
            return true;
        }

        auto cmd = std::make_unique<nspc::SetRowEventCommand>(location, nspc::Tie{});
        const bool anchorInserted = appState_.commandHistory.execute(song, std::move(cmd));
        if (anchorInserted) {
            pastedRowEventFlags[row_channel_index(row, channel)] = 1;
        }
        return anchorInserted;
    };

    for (const auto& clip : clipboardCells_) {
        const int targetRow = baseRow + clip.rowOffset;
        const int targetFlatCol = baseFlatCol + clip.flatColumnOffset;
        if (targetRow < 0 || targetRow >= static_cast<int>(rows_.size())) {
            continue;
        }
        if (targetFlatCol < 0 || targetFlatCol >= kChannels * kEditItems) {
            continue;
        }

        const int targetChannel = targetFlatCol / kEditItems;
        const int targetItem = targetFlatCol % kEditItems;
        nspc::NspcEditorLocation location{
            .patternId = patternId,
            .channel = targetChannel,
            .row = static_cast<uint32_t>(targetRow),
        };

        switch (targetItem) {
        case 0:
            if (clip.rowEvent.has_value()) {
                auto cmd = std::make_unique<nspc::SetRowEventCommand>(location, *clip.rowEvent);
                updated = appState_.commandHistory.execute(song, std::move(cmd)) || updated;
                pastedRowEventFlags[row_channel_index(targetRow, targetChannel)] = 1;
            }
            break;
        case 1:
            if (clip.byteValue.has_value()) {
                (void)ensure_row_anchor(location, targetRow, targetChannel);
            }
            {
                auto cmd = std::make_unique<nspc::SetInstrumentCommand>(location, clip.byteValue);
                updated = appState_.commandHistory.execute(song, std::move(cmd)) || updated;
            }
            break;
        case 2:
            if (clip.byteValue.has_value()) {
                (void)ensure_row_anchor(location, targetRow, targetChannel);
            }
            {
                auto cmd = std::make_unique<nspc::SetVolumeCommand>(location, clip.byteValue);
                updated = appState_.commandHistory.execute(song, std::move(cmd)) || updated;
            }
            break;
        case 3:
            if (clip.byteValue.has_value()) {
                (void)ensure_row_anchor(location, targetRow, targetChannel);
            }
            {
                auto cmd = std::make_unique<nspc::SetQvCommand>(location, clip.byteValue);
                updated = appState_.commandHistory.execute(song, std::move(cmd)) || updated;
            }
            break;
        case 4: {
            if (!clip.effects.empty()) {
                (void)ensure_row_anchor(location, targetRow, targetChannel);
            }
            // Build effects list from clipboard
            std::vector<nspc::Vcmd> newEffects;
            for (const auto& fx : clip.effects) {
                const auto vcmd = reconstructVcmdFromEffectChip(fx);
                if (vcmd.has_value()) {
                    newEffects.push_back(*vcmd);
                }
            }
            auto cmd = std::make_unique<nspc::SetEffectsCommand>(location, std::move(newEffects));
            bool fieldUpdated = appState_.commandHistory.execute(song, std::move(cmd));
            updated = fieldUpdated || updated;
            break;
        }
        default:
            break;
        }
    }

    txn.commit();
    return updated;
}

bool PatternEditorPanel::clearSelectedCells(nspc::NspcSong& song, int patternId) {
    if (rows_.empty()) {
        return false;
    }

    ensureSelectionStorage();
    if (!hasCellSelection() && selectedRow_ >= 0 && selectedChannel_ >= 0) {
        selectSingleCell(selectedRow_, selectedChannel_, selectedItem_, true);
    }
    if (!hasCellSelection()) {
        return false;
    }

    // Wrap entire clear operation in a transaction
    nspc::NspcCommandTransaction txn(appState_.commandHistory, "Delete Selection");

    bool updated = false;
    for (int row = 0; row < static_cast<int>(rows_.size()); ++row) {
        for (int channel = 0; channel < kChannels; ++channel) {
            for (int item = 0; item < kEditItems; ++item) {
                if (!isCellSelected(row, channel, item)) {
                    continue;
                }
                nspc::NspcEditorLocation location{
                    .patternId = patternId,
                    .channel = channel,
                    .row = static_cast<uint32_t>(row),
                };
                switch (item) {
                case 0: {
                    auto cmd = std::make_unique<nspc::DeleteRowEventCommand>(location);
                    updated = appState_.commandHistory.execute(song, std::move(cmd)) || updated;
                    break;
                }
                case 1: {
                    auto cmd = std::make_unique<nspc::SetInstrumentCommand>(location, std::nullopt);
                    updated = appState_.commandHistory.execute(song, std::move(cmd)) || updated;
                    break;
                }
                case 2: {
                    auto cmd = std::make_unique<nspc::SetVolumeCommand>(location, std::nullopt);
                    updated = appState_.commandHistory.execute(song, std::move(cmd)) || updated;
                    break;
                }
                case 3: {
                    auto cmd = std::make_unique<nspc::SetQvCommand>(location, std::nullopt);
                    updated = appState_.commandHistory.execute(song, std::move(cmd)) || updated;
                    break;
                }
                case 4: {
                    auto cmd = std::make_unique<nspc::SetEffectsCommand>(location, std::vector<nspc::Vcmd>{});
                    updated = appState_.commandHistory.execute(song, std::move(cmd)) || updated;
                    break;
                }
                default:
                    break;
                }
            }
        }
    }
    txn.commit();
    return updated;
}

void PatternEditorPanel::clampSelectionToRows() {
    if (rows_.empty()) {
        selectedRow_ = -1;
        selectedChannel_ = -1;
        selectedItem_ = 0;
        hexInput_.clear();
        selectedCells_.clear();
        selectionAnchorValid_ = false;
        mouseSelecting_ = false;
        return;
    }

    const int maxRow = static_cast<int>(rows_.size()) - 1;
    selectedRow_ = std::clamp(selectedRow_, 0, maxRow);
    selectedChannel_ = std::clamp(selectedChannel_, 0, kChannels - 1);
    selectedItem_ = std::clamp(selectedItem_, 0, kEditItems - 1);
    appState_.trackerInputOctave = std::clamp(appState_.trackerInputOctave, 0, 7);

    const int step = std::max(ticksPerRow_, kMinTicksPerRow);
    selectedRow_ = (selectedRow_ / step) * step;

    ensureSelectionStorage();
    if (!hasCellSelection()) {
        selectSingleCell(selectedRow_, selectedChannel_, selectedItem_, !selectionAnchorValid_);
    }

    if (selectionAnchorValid_) {
        selectionAnchor_.row = std::clamp(selectionAnchor_.row, 0, maxRow);
        selectionAnchor_.channel = std::clamp(selectionAnchor_.channel, 0, kChannels - 1);
        selectionAnchor_.item = std::clamp(selectionAnchor_.item, 0, kEditItems - 1);
    }
}

bool PatternEditorPanel::appendTypedHexNibble() {
    struct KeyNibble {
        ImGuiKey key;
        char hex;
    };

    static constexpr std::array<KeyNibble, 26> kMap = {{
        {ImGuiKey_0, '0'},       {ImGuiKey_1, '1'},       {ImGuiKey_2, '2'},       {ImGuiKey_3, '3'},
        {ImGuiKey_4, '4'},       {ImGuiKey_5, '5'},       {ImGuiKey_6, '6'},       {ImGuiKey_7, '7'},
        {ImGuiKey_8, '8'},       {ImGuiKey_9, '9'},       {ImGuiKey_A, 'A'},       {ImGuiKey_B, 'B'},
        {ImGuiKey_C, 'C'},       {ImGuiKey_D, 'D'},       {ImGuiKey_E, 'E'},       {ImGuiKey_F, 'F'},
        {ImGuiKey_Keypad0, '0'}, {ImGuiKey_Keypad1, '1'}, {ImGuiKey_Keypad2, '2'}, {ImGuiKey_Keypad3, '3'},
        {ImGuiKey_Keypad4, '4'}, {ImGuiKey_Keypad5, '5'}, {ImGuiKey_Keypad6, '6'}, {ImGuiKey_Keypad7, '7'},
        {ImGuiKey_Keypad8, '8'}, {ImGuiKey_Keypad9, '9'},
    }};

    for (const auto& keyNibble : kMap) {
        if (ImGui::IsKeyPressed(keyNibble.key)) {
            hexInput_.push_back(keyNibble.hex);
            return true;
        }
    }
    return false;
}

std::optional<PatternEditorPanel::TrackerPitchInput> PatternEditorPanel::consumeTrackerPitchInput() const {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl || io.KeyAlt || io.KeySuper) {
        return std::nullopt;
    }

    for (const auto& key : kTrackerNoteKeys) {
        if (ImGui::IsKeyPressed(key.key, false)) {
            const int pitch = std::clamp(appState_.trackerInputOctave * 12 + key.semitoneOffset, 0, 0x47);
            return TrackerPitchInput{.pitch = pitch, .key = static_cast<int>(key.key)};
        }
    }

    return std::nullopt;
}

std::optional<uint8_t> PatternEditorPanel::selectedInstrumentForEntry() {
    if (!appState_.project.has_value()) {
        return std::nullopt;
    }

    const auto& instruments = appState_.project->instruments();
    if (instruments.empty()) {
        return std::nullopt;
    }

    const auto selectedIt = std::find_if(instruments.begin(), instruments.end(), [&](const nspc::NspcInstrument& instrument) {
        return instrument.id == appState_.selectedInstrumentId;
    });
    if (selectedIt != instruments.end()) {
        return static_cast<uint8_t>(selectedIt->id & 0xFF);
    }

    appState_.selectedInstrumentId = instruments.front().id;
    return static_cast<uint8_t>(instruments.front().id & 0xFF);
}

std::optional<uint8_t> PatternEditorPanel::effectiveInstrumentAtRow(int channel, uint32_t row) const {
    if (channel < 0 || channel >= kChannels) {
        return std::nullopt;
    }
    if (!flatPattern_.has_value()) {
        return std::nullopt;
    }

    std::optional<uint8_t> currentInstrument = std::nullopt;
    const auto& events = flatPattern_->channels[static_cast<size_t>(channel)].events;
    for (const auto& flatEvent : events) {
        if (flatEvent.tick > row) {
            break;
        }

        const auto* vcmd = std::get_if<nspc::Vcmd>(&flatEvent.event);
        if (!vcmd) {
            continue;
        }

        if (const auto* inst = std::get_if<nspc::VcmdInst>(&vcmd->vcmd)) {
            currentInstrument = inst->instrumentIndex;
        }
    }

    return currentInstrument;
}

bool PatternEditorPanel::cycleSelectedInstrument(int direction) {
    if (!appState_.project.has_value()) {
        return false;
    }

    const auto& instruments = appState_.project->instruments();
    if (instruments.empty()) {
        appState_.selectedInstrumentId = -1;
        return false;
    }

    std::vector<int> ids;
    ids.reserve(instruments.size());
    for (const auto& instrument : instruments) {
        ids.push_back(instrument.id);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    if (ids.empty()) {
        appState_.selectedInstrumentId = -1;
        return false;
    }

    auto current = std::find(ids.begin(), ids.end(), appState_.selectedInstrumentId);
    if (current == ids.end()) {
        appState_.selectedInstrumentId = direction >= 0 ? ids.front() : ids.back();
        return true;
    }

    const size_t index = static_cast<size_t>(std::distance(ids.begin(), current));
    const size_t count = ids.size();
    const size_t nextIndex = direction >= 0 ? (index + 1) % count : (index + count - 1) % count;
    appState_.selectedInstrumentId = ids[nextIndex];
    return true;
}

void PatternEditorPanel::syncProjectAramToPreviewPlayer() {
    if (!appState_.project.has_value() || !appState_.spcPlayer) {
        return;
    }

    const auto srcAram = appState_.project->aram();
    auto dstAram = appState_.spcPlayer->spcDsp().aram();
    const auto srcAll = srcAram.all();
    auto dstAll = dstAram.all();
    std::copy(srcAll.begin(), srcAll.end(), dstAll.begin());
}

void PatternEditorPanel::startTrackerPreview(int midiPitch, int key) {
    if (!appState_.project.has_value() || !appState_.spcPlayer) {
        return;
    }
    if (appState_.isPlaying && appState_.isPlaying()) {
        return;
    }

    const auto& instruments = appState_.project->instruments();
    const auto it = std::find_if(instruments.begin(), instruments.end(), [&](const nspc::NspcInstrument& instrument) {
        return instrument.id == appState_.selectedInstrumentId;
    });
    if (it == instruments.end()) {
        return;
    }

    const auto& engine = appState_.project->engineConfig();
    if (engine.sampleHeaders == 0) {
        return;
    }

    syncProjectAramToPreviewPlayer();
    appState_.spcPlayer->spcDsp().writeDspRegister(kDspDirReg, static_cast<uint8_t>(engine.sampleHeaders >> 8));

    constexpr uint8_t kPreviewVoice = 1;
    appState_.spcPlayer->noteOff(kPreviewVoice);

    audio::NotePreviewParams params{};
    params.sampleIndex = static_cast<uint8_t>(it->sampleIndex & 0x7F);
    params.pitch = audio::NotePreviewParams::pitchFromNspcNote(midiPitch, pitchMultiplierFromInstrument(*it));
    params.volumeL = 127;
    params.volumeR = 127;
    params.adsr1 = it->adsr1;
    params.adsr2 = it->adsr2;
    params.gain = it->gain;
    params.voice = kPreviewVoice;
    appState_.spcPlayer->noteOn(params);

    activeTrackerPreviewKey_ = key;
    trackerPreviewActive_ = true;
}

void PatternEditorPanel::stopTrackerPreview() {
    if (trackerPreviewActive_ && appState_.spcPlayer) {
        constexpr uint8_t kPreviewVoice = 1;
        appState_.spcPlayer->noteOff(kPreviewVoice);
    }

    trackerPreviewActive_ = false;
    activeTrackerPreviewKey_.reset();
}

void PatternEditorPanel::advanceEditingCursor(int step, int maxRow) {
    const int advanceStep = editStep_ * step;
    selectedRow_ = std::clamp(selectedRow_ + advanceStep, 0, maxRow);
    selectSingleCell(selectedRow_, selectedChannel_, selectedItem_, true);
}

bool PatternEditorPanel::handleNoteColumnEditing(nspc::NspcSong& song, const nspc::NspcEditorLocation& location,
                                                 int step, int maxRow) {
    if (const auto pitchInput = consumeTrackerPitchInput(); pitchInput.has_value()) {
        const bool shouldBootstrapPatternLength = !flatPatternHasAnyTimedEvents(flatPattern_);
        constexpr uint32_t kBootstrapPatternEndTick = static_cast<uint32_t>(kDefaultVisibleRows - 1);

        bool hasPreviewInstrument = false;
        bool updated = false;
        nspc::NspcCommandTransaction txn(appState_.commandHistory, "Set Note");

        if (const auto instrument = selectedInstrumentForEntry(); instrument.has_value()) {
            appState_.selectedInstrumentId = *instrument;
            const auto effectiveInstrument = effectiveInstrumentAtRow(selectedChannel_, location.row);
            if (!effectiveInstrument.has_value() || *effectiveInstrument != *instrument) {
                auto noteCmd = std::make_unique<nspc::SetRowEventCommand>(
                    location, nspc::Note{static_cast<uint8_t>(pitchInput->pitch)});
                updated = appState_.commandHistory.execute(song, std::move(noteCmd));
                auto instCmd = std::make_unique<nspc::SetInstrumentCommand>(location, *instrument);
                updated = appState_.commandHistory.execute(song, std::move(instCmd)) || updated;
                hasPreviewInstrument = true;
            } else {
                auto cmd = std::make_unique<nspc::SetRowEventCommand>(
                    location, nspc::Note{static_cast<uint8_t>(pitchInput->pitch)});
                updated = appState_.commandHistory.execute(song, std::move(cmd));
                hasPreviewInstrument = true;
            }
        } else {
            auto cmd = std::make_unique<nspc::SetRowEventCommand>(location, nspc::Note{static_cast<uint8_t>(pitchInput->pitch)});
            updated = appState_.commandHistory.execute(song, std::move(cmd));
        }

        if (updated && shouldBootstrapPatternLength) {
            auto lengthCmd = std::make_unique<SetPatternLengthCommand>(location.patternId, kBootstrapPatternEndTick);
            updated = appState_.commandHistory.execute(song, std::move(lengthCmd)) || updated;
        }
        txn.commit();

        if (hasPreviewInstrument) {
            startTrackerPreview(pitchInput->pitch, pitchInput->key);
        }
        if (updated) {
            advanceEditingCursor(step, maxRow);
        }
        return updated;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Period)) {
        auto cmd = std::make_unique<nspc::SetRowEventCommand>(location, nspc::Rest{});
        const bool updated = appState_.commandHistory.execute(song, std::move(cmd));
        if (updated) {
            advanceEditingCursor(step, maxRow);
        }
        return updated;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Backslash)) {
        auto cmd = std::make_unique<nspc::SetRowEventCommand>(location, nspc::Tie{});
        const bool updated = appState_.commandHistory.execute(song, std::move(cmd));
        if (updated) {
            advanceEditingCursor(step, maxRow);
        }
        return updated;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Insert)) {
        auto cmd = std::make_unique<nspc::InsertTickCommand>(location);
        const bool updated = appState_.commandHistory.execute(song, std::move(cmd));
        if (updated) {
            selectedRow_ = std::clamp(selectedRow_ + step, 0, maxRow);
            selectSingleCell(selectedRow_, selectedChannel_, selectedItem_, true);
        }
        return updated;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        auto cmd = std::make_unique<nspc::RemoveTickCommand>(location);
        const bool updated = appState_.commandHistory.execute(song, std::move(cmd));
        if (updated) {
            selectedRow_ = std::clamp(selectedRow_ - step, 0, maxRow);
            selectSingleCell(selectedRow_, selectedChannel_, selectedItem_, true);
        }
        return updated;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        auto cmd = std::make_unique<nspc::DeleteRowEventCommand>(location);
        return appState_.commandHistory.execute(song, std::move(cmd));
    }

    return false;
}

bool PatternEditorPanel::clearCurrentValueColumn(nspc::NspcSong& song, const nspc::NspcEditorLocation& location) {
    if (selectedItem_ == 1) {
        auto cmd = std::make_unique<nspc::SetInstrumentCommand>(location, std::nullopt);
        return appState_.commandHistory.execute(song, std::move(cmd));
    }
    if (selectedItem_ == 2) {
        auto cmd = std::make_unique<nspc::SetVolumeCommand>(location, std::nullopt);
        return appState_.commandHistory.execute(song, std::move(cmd));
    }
    if (selectedItem_ == 3) {
        auto cmd = std::make_unique<nspc::SetQvCommand>(location, std::nullopt);
        return appState_.commandHistory.execute(song, std::move(cmd));
    }
    if (selectedItem_ == 4) {
        auto cmd = std::make_unique<nspc::SetEffectsCommand>(location, std::vector<nspc::Vcmd>{});
        return appState_.commandHistory.execute(song, std::move(cmd));
    }
    return false;
}

bool PatternEditorPanel::handleValueColumnHexEditing(nspc::NspcSong& song,
                                                      const nspc::NspcEditorLocation& location, int step,
                                                      int maxRow) {
    if (selectedItem_ < 1 || selectedItem_ > 3) {
        return false;
    }

    if (hexInput_.size() < 2) {
        return false;
    }

    const int value = std::clamp(parseHexValue(hexInput_), 0, 0xFF);
    bool updated = false;
    if (selectedItem_ == 1) {
        auto cmd = std::make_unique<nspc::SetInstrumentCommand>(location, static_cast<uint8_t>(value));
        updated = appState_.commandHistory.execute(song, std::move(cmd));
    } else if (selectedItem_ == 2) {
        auto cmd = std::make_unique<nspc::SetVolumeCommand>(location, static_cast<uint8_t>(value));
        updated = appState_.commandHistory.execute(song, std::move(cmd));
    } else {
        auto cmd = std::make_unique<nspc::SetQvCommand>(location, static_cast<uint8_t>(value));
        updated = appState_.commandHistory.execute(song, std::move(cmd));
    }

    hexInput_.clear();
    if (updated) {
        advanceEditingCursor(step, maxRow);
    }
    return updated;
}

bool PatternEditorPanel::handleFxHexEditing(nspc::NspcSong& song, const nspc::NspcEditorLocation& location) {
    const auto selectedRow = static_cast<size_t>(std::max(selectedRow_, 0));
    const auto selectedChannel = static_cast<size_t>(std::clamp(selectedChannel_, 0, kChannels - 1));
    const PatternCell* selectedCell = (selectedRow < rows_.size()) ? &rows_[selectedRow][selectedChannel] : nullptr;

    const EffectChip* singleFx = nullptr;
    if (selectedCell && selectedCell->effects.size() == 1) {
        singleFx = &selectedCell->effects.front();
    }
    const bool rowHasExistingEffects = selectedCell && !selectedCell->effects.empty();

    const auto hexView = std::string_view(hexInput_);
    const auto startsWithVirtualPrefix = [&]() {
        return hexInput_.size() >= 2 && parseHexValue(hexView.substr(0, 2)) == 0xFF;
    };

    if (singleFx && fxParamCountForCurrentEngine(singleFx->id).has_value() && singleFx->paramCount > 0) {
        bool overwriteMode = false;
        uint8_t overwriteId = 0;
        size_t overwriteLeadChars = 2;
        std::optional<uint8_t> overwriteParamCount = std::nullopt;
        if (const auto decodedLead = decodeTypedFxLeadForCurrentEngine(hexView); decodedLead.has_value()) {
            overwriteId = decodedLead->first;
            overwriteLeadChars = decodedLead->second;
            overwriteParamCount = fxParamCountForCurrentEngine(overwriteId);
            overwriteMode = overwriteParamCount.has_value() && (overwriteId != singleFx->id || overwriteLeadChars > 2);
        } else if (startsWithVirtualPrefix()) {
            if (hexInput_.size() >= 4) {
                hexInput_.clear();
            }
            return false;
        }

        if (!overwriteMode) {
            const size_t totalChars = static_cast<size_t>(singleFx->paramCount) * 2;
            if (hexInput_.size() < totalChars) {
                return false;
            }

            std::array<uint8_t, 4> params = singleFx->params;
            for (size_t i = 0; i < static_cast<size_t>(singleFx->paramCount); ++i) {
                params[i] = static_cast<uint8_t>(parseHexValue(hexView.substr(i * 2, 2)));
            }

            const auto vcmd = buildVcmdFromRawForCurrentEngine(singleFx->id, params, singleFx->paramCount);
            hexInput_.clear();
            if (!vcmd.has_value()) {
                return false;
            }
            std::vector<nspc::Vcmd> newEffects = {*vcmd};
            auto cmd = std::make_unique<nspc::SetEffectsCommand>(location, std::move(newEffects));
            return appState_.commandHistory.execute(song, std::move(cmd));
        }

        const size_t totalChars = overwriteLeadChars + static_cast<size_t>(*overwriteParamCount) * 2;
        if (hexInput_.size() < totalChars) {
            return false;
        }

        std::array<uint8_t, 4> params{};
        for (uint8_t i = 0; i < *overwriteParamCount; ++i) {
            params[static_cast<size_t>(i)] =
                static_cast<uint8_t>(
                    parseHexValue(hexView.substr(overwriteLeadChars + static_cast<size_t>(i) * 2, 2)));
        }

        const auto vcmd = buildVcmdFromRawForCurrentEngine(overwriteId, params, *overwriteParamCount);
        hexInput_.clear();
        if (!vcmd.has_value()) {
            return false;
        }
        std::vector<nspc::Vcmd> newEffects;
        if (selectedCell && !selectedCell->effects.empty()) {
            for (const auto& effect : selectedCell->effects) {
                if (effect.id == nspc::VcmdSubroutineCall::id) {
                    continue;
                }
                if (auto existingVcmd = reconstructVcmdFromEffectChip(effect)) {
                    newEffects.push_back(*existingVcmd);
                }
            }
        }
        newEffects.push_back(*vcmd);
        auto cmd = std::make_unique<nspc::SetEffectsCommand>(location, std::move(newEffects));
        return appState_.commandHistory.execute(song, std::move(cmd));
    }

    if (hexInput_.size() < 2) {
        return false;
    }

    const auto decodedLead = decodeTypedFxLeadForCurrentEngine(hexView);
    if (!decodedLead.has_value()) {
        if (startsWithVirtualPrefix() && hexInput_.size() >= 4) {
            hexInput_.clear();
        }
        return false;
    }

    const uint8_t vcmdId = decodedLead->first;
    const size_t leadChars = decodedLead->second;
    const auto paramCount = fxParamCountForCurrentEngine(vcmdId);
    if (!paramCount.has_value()) {
        hexInput_.clear();
        return false;
    }

    const size_t totalChars = leadChars + static_cast<size_t>(*paramCount) * 2;
    if (hexInput_.size() < totalChars) {
        return false;
    }

    std::array<uint8_t, 4> params{};
    for (uint8_t i = 0; i < *paramCount; ++i) {
        params[static_cast<size_t>(i)] =
            static_cast<uint8_t>(parseHexValue(hexView.substr(leadChars + static_cast<size_t>(i) * 2, 2)));
    }

    const auto vcmd = buildVcmdFromRawForCurrentEngine(vcmdId, params, *paramCount);
    hexInput_.clear();
    if (!vcmd.has_value()) {
        return false;
    }

    std::vector<nspc::Vcmd> newEffects;
    if (rowHasExistingEffects && selectedCell) {
        for (const auto& effect : selectedCell->effects) {
            if (effect.id == nspc::VcmdSubroutineCall::id) {
                continue;
            }
            if (auto existingVcmd = reconstructVcmdFromEffectChip(effect)) {
                newEffects.push_back(*existingVcmd);
            }
        }
    }
    newEffects.push_back(*vcmd);
    auto cmd = std::make_unique<nspc::SetEffectsCommand>(location, std::move(newEffects));
    return appState_.commandHistory.execute(song, std::move(cmd));
}

std::optional<bool> PatternEditorPanel::handlePreNavigationShortcuts(nspc::NspcSong& song, int patternId,
                                                                      bool commandModifier) {
    if (commandModifier && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        const int amount = ImGui::GetIO().KeyShift ? 12 : 1;
        return transposeSelectedCells(song, patternId, amount);
    }
    if (commandModifier && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        const int amount = ImGui::GetIO().KeyShift ? -12 : -1;
        return transposeSelectedCells(song, patternId, amount);
    }
    if (commandModifier && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Comma, false)) {
        (void)cycleSelectedInstrument(-1);
        return false;
    }
    if (commandModifier && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Period, false)) {
        (void)cycleSelectedInstrument(1);
        return false;
    }
    return std::nullopt;
}

void PatternEditorPanel::handleNavigationKeys(bool commandModifier, int step, int maxRow,
                                              const SelectionCell& cursorBeforeMove) {
    auto clampRow = [&](int row) { return std::clamp(row, 0, maxRow); };
    bool movedByTab = false;
    bool movedSelection = false;

    if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        const int direction = ImGui::GetIO().KeyShift ? -1 : 1;
        selectedChannel_ = (selectedChannel_ + direction + kChannels) % kChannels;
        selectedItem_ = 0;
        movedSelection = true;
        movedByTab = true;
    }
    if (!commandModifier && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        selectedRow_ = clampRow(selectedRow_ - step);
        movedSelection = true;
    }
    if (!commandModifier && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        selectedRow_ = clampRow(selectedRow_ + step);
        movedSelection = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
        selectedRow_ = clampRow(selectedRow_ - step * 16);
        movedSelection = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
        selectedRow_ = clampRow(selectedRow_ + step * 16);
        movedSelection = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
        selectedRow_ = 0;
        movedSelection = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End)) {
        selectedRow_ = (maxRow / step) * step;
        movedSelection = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        if (selectedItem_ > 0) {
            --selectedItem_;
        } else {
            selectedItem_ = kEditItems - 1;
            selectedChannel_ = (selectedChannel_ + kChannels - 1) % kChannels;
        }
        movedSelection = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        if (selectedItem_ + 1 < kEditItems) {
            ++selectedItem_;
        } else {
            selectedItem_ = 0;
            selectedChannel_ = (selectedChannel_ + 1) % kChannels;
        }
        movedSelection = true;
    }

    if (!movedSelection) {
        return;
    }

    hexInput_.clear();
    const bool extending = ImGui::GetIO().KeyShift && !movedByTab;
    if (extending && !selectionAnchorValid_) {
        selectionAnchorValid_ = true;
        selectionAnchor_ = cursorBeforeMove;
    }
    updateSelectionFromCursor(extending);
}

std::optional<bool> PatternEditorPanel::handlePostNavigationShortcuts(nspc::NspcSong& song, int patternId,
                                                                       bool commandModifier, int step) {
    if (commandModifier && ImGui::IsKeyPressed(ImGuiKey_C)) {
        (void)copyCellSelectionToClipboard();
        return false;
    }
    if (commandModifier && ImGui::IsKeyPressed(ImGuiKey_X)) {
        if (!copyCellSelectionToClipboard()) {
            return false;
        }
        return clearSelectedCells(song, patternId);
    }
    if (commandModifier && ImGui::IsKeyPressed(ImGuiKey_V)) {
        return pasteClipboardAtCursor(song, patternId);
    }

    if (commandModifier && ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
        editStep_ = std::max(editStep_ - 1, 0);
    } else if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
        appState_.trackerInputOctave = std::max(appState_.trackerInputOctave - 1, 0);
    }
    if (commandModifier && ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
        editStep_ = std::min(editStep_ + 1, 16);
    } else if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
        appState_.trackerInputOctave = std::min(appState_.trackerInputOctave + 1, 7);
    }
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E)) {
        requestFxEditorOpen(std::max(selectedRow_, 0), std::max(selectedChannel_, 0));
        return false;
    }
    if (commandModifier && ImGui::IsKeyPressed(ImGuiKey_I)) {
        return interpolateSelectedCells(song, patternId);
    }
    if (commandModifier && ImGui::IsKeyPressed(ImGuiKey_A)) {
        ensureSelectionStorage();
        if (ImGui::GetIO().KeyShift) {
            clearCellSelection();
            for (int row = 0; row < static_cast<int>(rows_.size()); row += step) {
                for (int item = 0; item < kEditItems; ++item) {
                    setCellSelected(row, selectedChannel_, item, true);
                }
            }
        } else {
            std::fill(selectedCells_.begin(), selectedCells_.end(), 1);
        }
        return false;
    }
    if (ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_I)) {
        setInstrumentPopupOpen_ = true;
        return false;
    }
    if (ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_V)) {
        setVolumePopupOpen_ = true;
        return false;
    }
    if (ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_R)) {
        songInstrumentRemapPopupOpen_ = true;
        return false;
    }
    return std::nullopt;
}

bool PatternEditorPanel::handleDeleteSelectionShortcut(nspc::NspcSong& song, int patternId) {
    if (!ImGui::IsKeyPressed(ImGuiKey_Delete) || !hexInput_.empty()) {
        return false;
    }

    int selectionCount = 0;
    for (const uint8_t cell : selectedCells_) {
        selectionCount += (cell != 0U) ? 1 : 0;
    }
    if (selectionCount <= 1) {
        return false;
    }

    return clearSelectedCells(song, patternId);
}

bool PatternEditorPanel::handleKeyboardEditing(nspc::NspcSong& song, int patternId) {
    if (rows_.empty()) {
        return false;
    }
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        stopTrackerPreview();
        return false;
    }
    if (ImGui::IsAnyItemActive()) {
        return false;
    }

    if (trackerPreviewActive_ && activeTrackerPreviewKey_.has_value() &&
        ImGui::IsKeyReleased(static_cast<ImGuiKey>(*activeTrackerPreviewKey_))) {
        stopTrackerPreview();
    }

    // Playback shortcuts (F5/F6/F8/Space) are handled globally in UiManager

    clampSelectionToRows();
    const int step = std::max(ticksPerRow_, kMinTicksPerRow);
    const int maxRow = static_cast<int>(rows_.size()) - 1;
    const SelectionCell cursorBeforeMove{
        .row = selectedRow_,
        .channel = selectedChannel_,
        .item = selectedItem_,
    };
    const bool commandModifier = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;

    if (const auto handled = handlePreNavigationShortcuts(song, patternId, commandModifier); handled.has_value()) {
        return *handled;
    }
    handleNavigationKeys(commandModifier, step, maxRow, cursorBeforeMove);
    if (const auto handled = handlePostNavigationShortcuts(song, patternId, commandModifier, step);
        handled.has_value()) {
        return *handled;
    }

    nspc::NspcEditorLocation location{
        .patternId = patternId,
        .channel = selectedChannel_,
        .row = static_cast<uint32_t>(std::max(selectedRow_, 0)),
    };

    if (handleDeleteSelectionShortcut(song, patternId)) {
        return true;
    }

    if (selectedItem_ == 0) {
        return handleNoteColumnEditing(song, location, step, maxRow);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        hexInput_.clear();
        return false;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !hexInput_.empty()) {
        hexInput_.pop_back();
        return false;
    }

    if ((ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) && hexInput_.empty()) {
        return clearCurrentValueColumn(song, location);
    }

    if (!appendTypedHexNibble()) {
        return false;
    }

    if (selectedItem_ == 1 || selectedItem_ == 2 || selectedItem_ == 3) {
        return handleValueColumnHexEditing(song, location, step, maxRow);
    }

    if (selectedItem_ == 4) {
        return handleFxHexEditing(song, location);
    }

    return false;
}

bool PatternEditorPanel::transposeSelectedCells(nspc::NspcSong& song, int patternId, int semitones) {
    if (rows_.empty()) {
        return false;
    }

    ensureSelectionStorage();
    if (!hasCellSelection() && selectedRow_ >= 0 && selectedChannel_ >= 0) {
        selectSingleCell(selectedRow_, selectedChannel_, selectedItem_, true);
    }
    if (!hasCellSelection()) {
        return false;
    }

    // Wrap entire transpose operation in a transaction
    nspc::NspcCommandTransaction txn(appState_.commandHistory,
        std::format("Transpose {}{} semitones", semitones > 0 ? "+" : "", semitones));

    bool updated = false;
    const int step = std::max(ticksPerRow_, kMinTicksPerRow);
    for (int row = 0; row < static_cast<int>(rows_.size()); row += step) {
        for (int channel = 0; channel < kChannels; ++channel) {
            if (!isCellSelected(row, channel, 0)) {
                continue;
            }

            const auto& cell = rows_[static_cast<size_t>(row)][static_cast<size_t>(channel)];
            auto rowEvent = parseRowEventFromCell(cell);
            if (!rowEvent.has_value()) {
                continue;
            }

            if (const auto* note = std::get_if<nspc::Note>(&*rowEvent)) {
                const int newPitch = std::clamp(static_cast<int>(note->pitch) + semitones, 0, 0x47);
                nspc::NspcEditorLocation location{
                    .patternId = patternId,
                    .channel = channel,
                    .row = static_cast<uint32_t>(row),
                };
                auto cmd = std::make_unique<nspc::SetRowEventCommand>(location, nspc::Note{static_cast<uint8_t>(newPitch)});
                updated = appState_.commandHistory.execute(song, std::move(cmd)) || updated;
            }
        }
    }
    txn.commit();
    return updated;
}

bool PatternEditorPanel::setInstrumentOnSelection(nspc::NspcSong& song, int patternId, uint8_t instrument) {
    if (rows_.empty()) {
        return false;
    }

    ensureSelectionStorage();
    if (!hasCellSelection() && selectedRow_ >= 0 && selectedChannel_ >= 0) {
        selectSingleCell(selectedRow_, selectedChannel_, selectedItem_, true);
    }
    if (!hasCellSelection()) {
        return false;
    }

    // Wrap entire operation in a transaction
    nspc::NspcCommandTransaction txn(appState_.commandHistory, std::format("Set Instrument {:02X}", instrument));

    bool updated = false;
    const int step = std::max(ticksPerRow_, kMinTicksPerRow);
    for (int row = 0; row < static_cast<int>(rows_.size()); row += step) {
        for (int channel = 0; channel < kChannels; ++channel) {
            // Apply to cells where either the note column or instrument column is selected
            if (!isCellSelected(row, channel, 0) && !isCellSelected(row, channel, 1)) {
                continue;
            }

            const auto& cell = rows_[static_cast<size_t>(row)][static_cast<size_t>(channel)];
            if (!canShowInstVol(cell.note)) {
                continue;
            }

            nspc::NspcEditorLocation location{
                .patternId = patternId,
                .channel = channel,
                .row = static_cast<uint32_t>(row),
            };
            auto cmd = std::make_unique<nspc::SetInstrumentCommand>(location, instrument);
            updated = appState_.commandHistory.execute(song, std::move(cmd)) || updated;
        }
    }
    txn.commit();
    return updated;
}

bool PatternEditorPanel::setVolumeOnSelection(nspc::NspcSong& song, int patternId, uint8_t volume) {
    if (rows_.empty()) {
        return false;
    }

    ensureSelectionStorage();
    if (!hasCellSelection() && selectedRow_ >= 0 && selectedChannel_ >= 0) {
        selectSingleCell(selectedRow_, selectedChannel_, selectedItem_, true);
    }
    if (!hasCellSelection()) {
        return false;
    }

    // Wrap entire operation in a transaction
    nspc::NspcCommandTransaction txn(appState_.commandHistory, std::format("Set Volume {:02X}", volume));

    bool updated = false;
    const int step = std::max(ticksPerRow_, kMinTicksPerRow);
    for (int row = 0; row < static_cast<int>(rows_.size()); row += step) {
        for (int channel = 0; channel < kChannels; ++channel) {
            if (!isCellSelected(row, channel, 0) && !isCellSelected(row, channel, 2)) {
                continue;
            }

            const auto& cell = rows_[static_cast<size_t>(row)][static_cast<size_t>(channel)];
            if (!canShowInstVol(cell.note)) {
                continue;
            }

            nspc::NspcEditorLocation location{
                .patternId = patternId,
                .channel = channel,
                .row = static_cast<uint32_t>(row),
            };
            auto cmd = std::make_unique<nspc::SetVolumeCommand>(location, volume);
            updated = appState_.commandHistory.execute(song, std::move(cmd)) || updated;
        }
    }
    txn.commit();
    return updated;
}

bool PatternEditorPanel::interpolateSelectedCells(nspc::NspcSong& song, int patternId) {
    if (rows_.empty()) {
        return false;
    }

    ensureSelectionStorage();
    if (!hasCellSelection()) {
        return false;
    }

    // Wrap entire interpolate operation in a transaction
    nspc::NspcCommandTransaction txn(appState_.commandHistory, "Interpolate");

    bool updated = false;
    const int step = std::max(ticksPerRow_, kMinTicksPerRow);

    // Interpolate each (channel, item) column independently
    for (int channel = 0; channel < kChannels; ++channel) {
        for (int item = 1; item <= 3; ++item) {  // instrument, volume, qv
            // Find first and last selected rows with concrete values
            int firstRow = -1;
            int lastRow = -1;
            int firstValue = -1;
            int lastValue = -1;

            for (int row = 0; row < static_cast<int>(rows_.size()); row += step) {
                if (!isCellSelected(row, channel, item)) {
                    continue;
                }

                const auto& cell = rows_[static_cast<size_t>(row)][static_cast<size_t>(channel)];
                if (!canShowInstVol(cell.note)) {
                    continue;
                }

                std::optional<uint8_t> value;
                if (item == 1) {
                    value = parseHexByte(cell.instrument);
                } else if (item == 2) {
                    value = parseHexByte(cell.volume);
                } else if (item == 3) {
                    value = parseHexByte(cell.qv);
                }

                if (!value.has_value()) {
                    continue;
                }

                if (firstRow < 0) {
                    firstRow = row;
                    firstValue = static_cast<int>(*value);
                }
                lastRow = row;
                lastValue = static_cast<int>(*value);
            }

            if (firstRow < 0 || lastRow <= firstRow) {
                continue;
            }

            // Linearly interpolate for all selected rows between first and last
            const int rowSpan = lastRow - firstRow;
            for (int row = firstRow; row <= lastRow; row += step) {
                if (!isCellSelected(row, channel, item)) {
                    continue;
                }

                const double t = static_cast<double>(row - firstRow) / static_cast<double>(rowSpan);
                const int interpolated = static_cast<int>(std::lround(
                    static_cast<double>(firstValue) + t * static_cast<double>(lastValue - firstValue)));
                const auto byteValue = static_cast<uint8_t>(std::clamp(interpolated, 0, 0xFF));

                nspc::NspcEditorLocation location{
                    .patternId = patternId,
                    .channel = channel,
                    .row = static_cast<uint32_t>(row),
                };

                if (item == 1) {
                    auto cmd = std::make_unique<nspc::SetInstrumentCommand>(location, byteValue);
                    updated = appState_.commandHistory.execute(song, std::move(cmd)) || updated;
                } else if (item == 2) {
                    auto cmd = std::make_unique<nspc::SetVolumeCommand>(location, byteValue);
                    updated = appState_.commandHistory.execute(song, std::move(cmd)) || updated;
                } else if (item == 3) {
                    auto cmd = std::make_unique<nspc::SetQvCommand>(location, byteValue);
                    updated = appState_.commandHistory.execute(song, std::move(cmd)) || updated;
                }
            }
        }
    }
    txn.commit();
    return updated;
}

void PatternEditorPanel::drawSetInstrumentPopup(nspc::NspcSong& song, int patternId) {
    drawBulkValuePopup(setInstrumentPopupOpen_, "Set Instrument##bulk", "##bulk_inst",
                       "Set instrument on selection:", true, song, patternId);
}

void PatternEditorPanel::drawSetVolumePopup(nspc::NspcSong& song, int patternId) {
    drawBulkValuePopup(setVolumePopupOpen_, "Set Volume##bulk", "##bulk_vol", "Set volume on selection:", false,
                       song, patternId);
}

void PatternEditorPanel::drawPatternLengthPopup(nspc::NspcSong& song, int patternId) {
    if (patternLengthPopupOpen_) {
        patternLengthInputTicks_ = flatPattern_.has_value() ? static_cast<int>(flatPattern_->totalTicks) : 0;
        patternLengthInputTicks_ = std::clamp(patternLengthInputTicks_, 0, kMaxVisibleRows - 1);
        patternLengthStatus_.clear();
        ImGui::OpenPopup("Set Pattern Length");
        patternLengthPopupOpen_ = false;
    }

    if (!ImGui::BeginPopupModal("Set Pattern Length", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const int maxTicks = kMaxVisibleRows - 1;
    const int currentTicks = flatPattern_.has_value() ? static_cast<int>(flatPattern_->totalTicks) : 0;
    patternLengthInputTicks_ = std::clamp(patternLengthInputTicks_, 0, maxTicks);

    ImGui::Text("Current: %d ticks", currentTicks);
    ImGui::TextDisabled("Set explicit pattern end tick (%d-%d).", 0, maxTicks);
    ImGui::SetNextItemWidth(96.0f);
    if (ImGui::InputInt("Target", &patternLengthInputTicks_, 1, 16)) {
        patternLengthInputTicks_ = std::clamp(patternLengthInputTicks_, 0, maxTicks);
    }
    if (selectedRow_ >= 0) {
        ImGui::SameLine();
        if (ImGui::Button("Use Cursor")) {
            patternLengthInputTicks_ = std::clamp(selectedRow_, 0, maxTicks);
        }
    }

    const bool canApply = patternLengthInputTicks_ != currentTicks;
    ImGui::BeginDisabled(!canApply);
    if (ImGui::Button("Apply")) {
        auto cmd =
            std::make_unique<SetPatternLengthCommand>(patternId, static_cast<uint32_t>(std::max(patternLengthInputTicks_, 0)));
        if (appState_.commandHistory.execute(song, std::move(cmd))) {
            rebuildPatternRows(song, patternId);
            clampSelectionToRows();
            patternLengthStatus_.clear();
            ImGui::CloseCurrentPopup();
        } else {
            patternLengthStatus_ = "No changes applied";
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        patternLengthStatus_.clear();
        ImGui::CloseCurrentPopup();
    }
    if (!patternLengthStatus_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", patternLengthStatus_.c_str());
    }

    ImGui::EndPopup();
}

void PatternEditorPanel::drawBulkValuePopup(bool& openFlag, const char* popupId, const char* inputId,
                                            const char* prompt, bool instrumentMode, nspc::NspcSong& song,
                                            int patternId) {
    if (openFlag) {
        ImGui::OpenPopup(popupId);
        std::fill(bulkValueInput_.begin(), bulkValueInput_.end(), '\0');
        openFlag = false;
    }

    if (!ImGui::BeginPopup(popupId)) {
        return;
    }

    ImGui::TextUnformatted(prompt);
    ImGui::SetNextItemWidth(60.0f);
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::InputText(inputId, bulkValueInput_.data(), bulkValueInput_.size(),
                     ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();

    const std::string_view inputView(bulkValueInput_.data());
    const bool validInput = inputView.size() == 2;
    ImGui::BeginDisabled(!validInput);
    if (ImGui::Button("Apply") || (validInput && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
        const auto value = parseHexByte(inputView);
        if (value.has_value()) {
            const bool changed =
                instrumentMode ? setInstrumentOnSelection(song, patternId, *value) : setVolumeOnSelection(song, patternId, *value);
            if (changed) {
                rebuildPatternRows(song, patternId);
            }
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void PatternEditorPanel::rebuildSongInstrumentRemapEntries(const nspc::NspcSong& song) {
    std::array<int, 256> previousTargets{};
    previousTargets.fill(-1);
    for (const auto& entry : songInstrumentRemapEntries_) {
        previousTargets[entry.source] = entry.target;
    }

    const std::optional<int> channelScope = (songInstrumentRemapScope_ == InstrumentRemapScope::Channel)
                                                ? std::optional<int>(std::clamp(songInstrumentRemapChannel_, 0, 7))
                                                : std::nullopt;
    const std::array<int, 256> usedCounts = countUsedInstruments(song, channelScope);

    songInstrumentRemapEntries_.clear();
    for (int inst = 0; inst <= 0xFF; ++inst) {
        const int uses = usedCounts[static_cast<size_t>(inst)];
        if (uses <= 0) {
            continue;
        }
        const int previousTarget = previousTargets[static_cast<size_t>(inst)];
        songInstrumentRemapEntries_.push_back(SongInstrumentRemapEntry{
            .source = static_cast<uint8_t>(inst),
            .target = static_cast<uint8_t>((previousTarget >= 0) ? previousTarget : inst),
            .uses = uses,
        });
    }
}

bool PatternEditorPanel::applySongInstrumentRemap(nspc::NspcSong& song, int patternId) {
    std::vector<std::pair<uint8_t, uint8_t>> mappings;
    mappings.reserve(songInstrumentRemapEntries_.size());
    for (const auto& entry : songInstrumentRemapEntries_) {
        if (entry.target != entry.source) {
            mappings.emplace_back(entry.source, entry.target);
        }
    }

    const std::optional<int> channelScope = (songInstrumentRemapScope_ == InstrumentRemapScope::Channel)
                                                ? std::optional<int>(std::clamp(songInstrumentRemapChannel_, 0, 7))
                                                : std::nullopt;
    auto cmd = std::make_unique<SongInstrumentRemapCommand>(std::move(mappings), channelScope);
    if (!appState_.commandHistory.execute(song, std::move(cmd))) {
        songInstrumentRemapStatus_ = "No changes applied";
        return false;
    }

    rebuildPatternRows(song, patternId);
    rebuildSongInstrumentRemapEntries(song);
    songInstrumentRemapStatus_ = "Applied";
    return true;
}

void PatternEditorPanel::prepareSongInstrumentRemapPopup(const nspc::NspcSong& song) {
    if (songInstrumentRemapPopupOpen_) {
        if (selectedChannel_ >= 0 && selectedChannel_ < kChannels) {
            songInstrumentRemapChannel_ = selectedChannel_;
        } else {
            songInstrumentRemapChannel_ = std::clamp(songInstrumentRemapChannel_, 0, kChannels - 1);
        }
        rebuildSongInstrumentRemapEntries(song);
        songInstrumentRemapStatus_.clear();
        ImGui::OpenPopup("Remap Song Instruments");
        songInstrumentRemapPopupOpen_ = false;
    }
}

bool PatternEditorPanel::beginSongInstrumentRemapPopup() {
    bool keepOpen = true;
    if (!ImGui::BeginPopupModal("Remap Song Instruments", &keepOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        return false;
    }
    if (!keepOpen) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return false;
    }
    return true;
}

void PatternEditorPanel::drawSongInstrumentRemapScopeControls(const nspc::NspcSong& song) {
    bool scopeChanged = false;
    int scopeMode = (songInstrumentRemapScope_ == InstrumentRemapScope::Global) ? 0 : 1;
    if (ImGui::RadioButton("Global", &scopeMode, 0)) {
        scopeChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Per Channel", &scopeMode, 1)) {
        scopeChanged = true;
    }
    songInstrumentRemapScope_ = (scopeMode == 0) ? InstrumentRemapScope::Global : InstrumentRemapScope::Channel;

    if (songInstrumentRemapScope_ == InstrumentRemapScope::Channel) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        const std::string preview = std::format("Channel {}", std::clamp(songInstrumentRemapChannel_, 0, 7) + 1);
        if (ImGui::BeginCombo("##remap_channel", preview.c_str())) {
            for (int channel = 0; channel < kChannels; ++channel) {
                const bool selected = (channel == songInstrumentRemapChannel_);
                if (ImGui::Selectable(std::format("Channel {}", channel + 1).c_str(), selected)) {
                    songInstrumentRemapChannel_ = channel;
                    scopeChanged = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    if (scopeChanged) {
        rebuildSongInstrumentRemapEntries(song);
    }
}

void PatternEditorPanel::drawSongInstrumentRemapEntriesTable() {
    if (!appState_.project.has_value()) {
        return;
    }

    const auto& instruments = appState_.project->instruments();
    if (songInstrumentRemapEntries_.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No instrument commands found for the current scope.");
        return;
    }

    if (!ImGui::BeginTable("##song_inst_remap", 3,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        return;
    }

    ImGui::TableSetupColumn("Source");
    ImGui::TableSetupColumn("Target");
    ImGui::TableSetupColumn("Uses", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < songInstrumentRemapEntries_.size(); ++i) {
        auto& entry = songInstrumentRemapEntries_[i];
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(instrumentDisplayLabel(instruments, entry.source).c_str());

        ImGui::TableSetColumnIndex(1);
        const std::string comboId = std::format("##song_inst_target_{}", i);
        std::string preview = instrumentDisplayLabel(instruments, entry.target);
        if (entry.target == entry.source) {
            preview += " (no change)";
        }
        if (ImGui::BeginCombo(comboId.c_str(), preview.c_str())) {
            const bool keepSelected = (entry.target == entry.source);
            if (ImGui::Selectable("Keep source instrument", keepSelected)) {
                entry.target = entry.source;
            }
            if (keepSelected) {
                ImGui::SetItemDefaultFocus();
            }

            for (const auto& instrument : instruments) {
                const uint8_t targetId = static_cast<uint8_t>(instrument.id & 0xFF);
                const bool selected = (entry.target == targetId);
                if (ImGui::Selectable(instrumentDisplayLabel(instruments, targetId).c_str(), selected)) {
                    entry.target = targetId;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%d", entry.uses);
    }

    ImGui::EndTable();
}

int PatternEditorPanel::countPendingSongInstrumentRemaps() const {
    int changedMappings = 0;
    for (const auto& entry : songInstrumentRemapEntries_) {
        if (entry.target != entry.source) {
            ++changedMappings;
        }
    }
    return changedMappings;
}

void PatternEditorPanel::drawSongInstrumentRemapFooter(nspc::NspcSong& song, int patternId) {
    const int changedMappings = countPendingSongInstrumentRemaps();
    ImGui::Spacing();
    ImGui::TextDisabled("%d remap(s) pending", changedMappings);
    ImGui::BeginDisabled(changedMappings == 0);
    if (ImGui::Button("Apply")) {
        (void)applySongInstrumentRemap(song, patternId);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
    }
    if (!songInstrumentRemapStatus_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", songInstrumentRemapStatus_.c_str());
    }
}

void PatternEditorPanel::drawSongInstrumentRemapPopup(nspc::NspcSong& song, int patternId) {
    prepareSongInstrumentRemapPopup(song);

    if (!beginSongInstrumentRemapPopup()) {
        return;
    }

    ImGui::TextUnformatted("Remap instrument references for this song.");
    ImGui::TextDisabled("Affects Ins (E0) and percussion base instrument (FA) commands.");
    drawSongInstrumentRemapScopeControls(song);
    drawSongInstrumentRemapEntriesTable();
    drawSongInstrumentRemapFooter(song, patternId);
    ImGui::EndPopup();
}

std::optional<int> PatternEditorPanel::selectedSubroutineIdForActions(const nspc::NspcSong& song) const {
    if (selectedRow_ < 0 || selectedRow_ >= static_cast<int>(rows_.size()) || selectedChannel_ < 0 ||
        selectedChannel_ >= kChannels) {
        return std::nullopt;
    }

    const auto hasSubroutineId = [&](int subroutineId) -> bool {
        return std::any_of(song.subroutines().begin(), song.subroutines().end(),
                           [subroutineId](const nspc::NspcSubroutine& subroutine) { return subroutine.id == subroutineId; });
    };

    const auto& cell = rows_[static_cast<size_t>(selectedRow_)][static_cast<size_t>(selectedChannel_)];
    if (cell.hasSubroutineData && cell.subroutineId >= 0 && hasSubroutineId(cell.subroutineId)) {
        return cell.subroutineId;
    }

    for (const auto& effect : cell.effects) {
        if (effect.id != nspc::VcmdSubroutineCall::id) {
            continue;
        }
        if (effect.subroutineId.has_value() && hasSubroutineId(*effect.subroutineId)) {
            return effect.subroutineId;
        }
        const uint16_t address = static_cast<uint16_t>(effect.params[0]) | (static_cast<uint16_t>(effect.params[1]) << 8u);
        const auto resolvedId = resolveSubroutineIdForAddress(address);
        if (resolvedId.has_value() && hasSubroutineId(*resolvedId)) {
            return resolvedId;
        }
    }

    return std::nullopt;
}

std::optional<std::pair<uint32_t, uint32_t>> PatternEditorPanel::selectedRowRangeForChannel(int channel) const {
    if (rows_.empty() || channel < 0 || channel >= kChannels) {
        return std::nullopt;
    }

    int minRow = std::numeric_limits<int>::max();
    int maxRow = -1;

    if (hasCellSelection()) {
        for (int row = 0; row < static_cast<int>(rows_.size()); ++row) {
            for (int item = 0; item < kEditItems; ++item) {
                if (!isCellSelected(row, channel, item)) {
                    continue;
                }
                minRow = std::min(minRow, row);
                maxRow = std::max(maxRow, row);
            }
        }
    }

    if (maxRow < 0) {
        if (selectedRow_ < 0) {
            return std::nullopt;
        }
        const int clampedRow = std::clamp(selectedRow_, 0, static_cast<int>(rows_.size()) - 1);
        minRow = clampedRow;
        maxRow = clampedRow;
    }

    return std::pair<uint32_t, uint32_t>{static_cast<uint32_t>(minRow), static_cast<uint32_t>(maxRow)};
}

void PatternEditorPanel::drawContextMenu(nspc::NspcSong& song, int patternId) {
    if (!ImGui::BeginPopup("PatternContextMenu")) {
        return;
    }

    const auto runAndRefresh = [&](auto&& editFn) {
        if (editFn()) {
            rebuildPatternRows(song, patternId);
        }
    };

    const bool hasValidChannelSelection = selectedChannel_ >= 0 && selectedChannel_ < kChannels;
    const int actionChannel = hasValidChannelSelection ? selectedChannel_ : 0;
    const auto selectedRows = hasValidChannelSelection ? selectedRowRangeForChannel(actionChannel) : std::nullopt;
    const auto activeSubroutineId = selectedSubroutineIdForActions(song);

    if (ImGui::MenuItem("Cut", "Ctrl+X")) {
        if (copyCellSelectionToClipboard()) {
            runAndRefresh([&]() { return clearSelectedCells(song, patternId); });
        }
    }
    if (ImGui::MenuItem("Copy", "Ctrl+C")) {
        (void)copyCellSelectionToClipboard();
    }
    if (ImGui::MenuItem("Paste", "Ctrl+V", false, clipboardHasData_)) {
        runAndRefresh([&]() { return pasteClipboardAtCursor(song, patternId); });
    }
    if (ImGui::MenuItem("Delete", "Del")) {
        runAndRefresh([&]() { return clearSelectedCells(song, patternId); });
    }

    ImGui::Separator();

    if (ImGui::BeginMenu("Transpose")) {
        if (ImGui::MenuItem("+1 Semitone", "Ctrl+Up")) {
            runAndRefresh([&]() { return transposeSelectedCells(song, patternId, 1); });
        }
        if (ImGui::MenuItem("-1 Semitone", "Ctrl+Down")) {
            runAndRefresh([&]() { return transposeSelectedCells(song, patternId, -1); });
        }
        if (ImGui::MenuItem("+1 Octave", "Ctrl+Shift+Up")) {
            runAndRefresh([&]() { return transposeSelectedCells(song, patternId, 12); });
        }
        if (ImGui::MenuItem("-1 Octave", "Ctrl+Shift+Down")) {
            runAndRefresh([&]() { return transposeSelectedCells(song, patternId, -12); });
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Set Instrument...", "Alt+I")) {
        setInstrumentPopupOpen_ = true;
    }
    if (ImGui::MenuItem("Prev Selected Instrument", "Ctrl+Shift+,")) {
        (void)cycleSelectedInstrument(-1);
    }
    if (ImGui::MenuItem("Next Selected Instrument", "Ctrl+Shift+.")) {
        (void)cycleSelectedInstrument(1);
    }
    if (ImGui::MenuItem("Set Volume...", "Alt+V")) {
        setVolumePopupOpen_ = true;
    }
    if (ImGui::MenuItem("Set Pattern Length...")) {
        patternLengthPopupOpen_ = true;
    }
    if (ImGui::MenuItem("Remap Song Instruments...", "Alt+R")) {
        songInstrumentRemapPopupOpen_ = true;
    }
    if (ImGui::MenuItem("Interpolate", "Ctrl+I")) {
        runAndRefresh([&]() { return interpolateSelectedCells(song, patternId); });
    }
    if (ImGui::MenuItem("Create Subroutine From Selection", nullptr, false, selectedRows.has_value())) {
        const auto [startRow, endRow] = *selectedRows;
        runAndRefresh([&]() {
            auto cmd = std::make_unique<SongMutationCommand>(
                "Create Subroutine",
                [patternId, actionChannel, startRow, endRow](nspc::NspcSong& targetSong) {
                    nspc::NspcEditor editor;
                    nspc::NspcEditorLocation location{
                        .patternId = patternId,
                        .channel = actionChannel,
                        .row = startRow,
                    };
                    return editor.createSubroutineFromRowRange(targetSong, location, startRow, endRow);
                });
            return appState_.commandHistory.execute(song, std::move(cmd));
        });
    }
    if (ImGui::MenuItem("Flatten Subroutine On Channel", nullptr, false,
                        hasValidChannelSelection && activeSubroutineId.has_value())) {
        const int subroutineId = *activeSubroutineId;
        runAndRefresh([&]() {
            auto cmd = std::make_unique<SongMutationCommand>(
                std::format("Flatten Sub {} (Ch {})", subroutineId, actionChannel + 1),
                [patternId, actionChannel, selectedRow = std::max(selectedRow_, 0), subroutineId](nspc::NspcSong& targetSong) {
                    nspc::NspcEditor editor;
                    nspc::NspcEditorLocation location{
                        .patternId = patternId,
                        .channel = actionChannel,
                        .row = static_cast<uint32_t>(selectedRow),
                    };
                    return editor.flattenSubroutineOnChannel(targetSong, location, subroutineId);
                });
            return appState_.commandHistory.execute(song, std::move(cmd));
        });
    }
    if (ImGui::MenuItem("Delete Subroutine (Flatten Everywhere)", nullptr, false, activeSubroutineId.has_value())) {
        const int subroutineId = *activeSubroutineId;
        runAndRefresh([&]() {
            auto cmd = std::make_unique<SongMutationCommand>(
                std::format("Delete Sub {}", subroutineId),
                [subroutineId](nspc::NspcSong& targetSong) {
                    nspc::NspcEditor editor;
                    return editor.deleteSubroutine(targetSong, subroutineId);
                });
            return appState_.commandHistory.execute(song, std::move(cmd));
        });
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Select All", "Ctrl+A")) {
        ensureSelectionStorage();
        std::fill(selectedCells_.begin(), selectedCells_.end(), 1);
    }
    if (ImGui::MenuItem("Select Channel", "Ctrl+Shift+A")) {
        ensureSelectionStorage();
        clearCellSelection();
        const int step = std::max(ticksPerRow_, kMinTicksPerRow);
        for (int row = 0; row < static_cast<int>(rows_.size()); row += step) {
            for (int item = 0; item < kEditItems; ++item) {
                setCellSelected(row, selectedChannel_, item, true);
            }
        }
    }

    ImGui::Separator();

    if (ImGui::MenuItem("FX Editor...", "Ctrl+E")) {
        requestFxEditorOpen(std::max(selectedRow_, 0), std::max(selectedChannel_, 0));
    }

    ImGui::EndPopup();
}

}  // namespace ntrak::ui
