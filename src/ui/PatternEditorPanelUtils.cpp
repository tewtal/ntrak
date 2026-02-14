#include "ntrak/ui/PatternEditorPanelUtils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace ntrak::ui::detail {

std::string noteToString(int note) {
    static constexpr std::array<const char*, 12> kNames = {"C-", "C#", "D-", "D#", "E-", "F-",
                                                           "F#", "G-", "G#", "A-", "A#", "B-"};

    if (note < 0) {
        return "---";
    }

    const int octave = note / 12;
    const int pitch = note % 12;
    return std::string(kNames[static_cast<size_t>(pitch)]) + std::to_string(octave);
}

std::string hex2(int value) {
    return std::format("{:02X}", value & 0xFF);
}

std::string vcmdChipText(const nspc::Vcmd& cmd) {
    return std::visit(
        nspc::overloaded{
            [](const std::monostate&) { return std::string{"---"}; },
            [](const nspc::VcmdInst& v) { return std::format("Ins {}", hex2(v.instrumentIndex)); },
            [](const nspc::VcmdPanning& v) { return std::format("Pan {}", hex2(v.panning)); },
            [](const nspc::VcmdPanFade& v) { return std::format("PFa {} {}", hex2(v.time), hex2(v.target)); },
            [](const nspc::VcmdVibratoOn& v) {
                return std::format("VOn {} {} {}", hex2(v.delay), hex2(v.rate), hex2(v.depth));
            },
            [](const nspc::VcmdVibratoOff&) { return std::string{"VOf"}; },
            [](const nspc::VcmdGlobalVolume& v) { return std::format("GVl {}", hex2(v.volume)); },
            [](const nspc::VcmdGlobalVolumeFade& v) {
                return std::format("GVF {} {}", hex2(v.time), hex2(v.target));
            },
            [](const nspc::VcmdTempo& v) { return std::format("Tmp {}", hex2(v.tempo)); },
            [](const nspc::VcmdTempoFade& v) {
                return std::format("TmF {} {}", hex2(v.time), hex2(v.target));
            },
            [](const nspc::VcmdGlobalTranspose& v) { return std::format("GTr {:+d}", v.semitones); },
            [](const nspc::VcmdPerVoiceTranspose& v) { return std::format("PTr {:+d}", v.semitones); },
            [](const nspc::VcmdTremoloOn& v) {
                return std::format("TOn {} {} {}", hex2(v.delay), hex2(v.rate), hex2(v.depth));
            },
            [](const nspc::VcmdTremoloOff&) { return std::string{"TOf"}; },
            [](const nspc::VcmdVolume& v) { return std::format("Vol {}", hex2(v.volume)); },
            [](const nspc::VcmdVolumeFade& v) { return std::format("VFd {} {}", hex2(v.time), hex2(v.target)); },
            [](const nspc::VcmdSubroutineCall& v) {
                return std::format("Sub {} x{}", v.subroutineId, hex2(v.count));
            },
            [](const nspc::VcmdVibratoFadeIn& v) { return std::format("Vfi {}", hex2(v.time)); },
            [](const nspc::VcmdPitchEnvelopeTo& v) {
                return std::format("PEt {} {} {}", hex2(v.delay), hex2(v.length), hex2(v.semitone));
            },
            [](const nspc::VcmdPitchEnvelopeFrom& v) {
                return std::format("PEf {} {} {}", hex2(v.delay), hex2(v.length), hex2(v.semitone));
            },
            [](const nspc::VcmdPitchEnvelopeOff&) { return std::string{"PEo"}; },
            [](const nspc::VcmdFineTune& v) { return std::format("FTn {:+d}", v.semitones); },
            [](const nspc::VcmdEchoOn& v) {
                return std::format("EOn {} {} {}", hex2(v.channels), hex2(v.left), hex2(v.right));
            },
            [](const nspc::VcmdEchoOff&) { return std::string{"EOf"}; },
            [](const nspc::VcmdEchoParams& v) {
                return std::format("EPr {} {} {}", hex2(v.delay), hex2(v.feedback), hex2(v.firIndex));
            },
            [](const nspc::VcmdEchoVolumeFade& v) {
                return std::format("EVF {} {} {}", hex2(v.time), hex2(v.leftTarget), hex2(v.rightTarget));
            },
            [](const nspc::VcmdPitchSlideToNote& v) {
                return std::format("PSt {} {} {}", hex2(v.delay), hex2(v.length), hex2(v.note));
            },
            [](const nspc::VcmdPercussionBaseInstrument& v) { return std::format("PIn {}", hex2(v.index)); },
            [](const nspc::VcmdExtension& v) {
                std::string text = "FF ";
                text += hex2(v.id);
                const size_t count = std::min<size_t>(v.paramCount, v.params.size());
                for (size_t i = 0; i < count; ++i) {
                    text += std::format(" {}", hex2(v.params[i]));
                }
                return text;
            },
            [](const auto& value) { return std::string{value.name}; },
        },
        cmd.vcmd);
}

bool isTieMarker(std::string_view noteText) {
    return noteText == "~~~" || noteText == "^^^";
}

bool isRestMarker(std::string_view noteText) {
    return noteText == "===" || noteText == "---";
}

bool canShowInstVol(std::string_view noteText) {
    if (noteText.empty()) {
        return false;
    }
    if (noteText == "...") {
        return false;
    }
    if (isTieMarker(noteText) || isRestMarker(noteText)) {
        return false;
    }
    return true;
}

ImU32 subroutineColorU32(int subroutineId) {
    const float hue = std::fmod(static_cast<float>(subroutineId) * 0.61803398875f, 1.0f);
    const ImVec4 color = ImColor::HSV(hue, 0.55f, 0.95f);
    return ImGui::ColorConvertFloat4ToU32(color);
}

uint8_t vcmdCategory(const nspc::Vcmd& cmd) {
    return std::visit(
        nspc::overloaded{
            [](const std::monostate&) -> uint8_t { return 0; },
            [](const nspc::VcmdPanning&) -> uint8_t { return 1; },
            [](const nspc::VcmdPanFade&) -> uint8_t { return 1; },
            [](const nspc::VcmdGlobalVolume&) -> uint8_t { return 1; },
            [](const nspc::VcmdGlobalVolumeFade&) -> uint8_t { return 1; },
            [](const nspc::VcmdVolumeFade&) -> uint8_t { return 1; },
            [](const nspc::VcmdSubroutineCall&) -> uint8_t { return 6; },
            [](const nspc::VcmdGlobalTranspose&) -> uint8_t { return 2; },
            [](const nspc::VcmdPerVoiceTranspose&) -> uint8_t { return 2; },
            [](const nspc::VcmdFineTune&) -> uint8_t { return 2; },
            [](const nspc::VcmdPitchEnvelopeTo&) -> uint8_t { return 2; },
            [](const nspc::VcmdPitchEnvelopeFrom&) -> uint8_t { return 2; },
            [](const nspc::VcmdPitchEnvelopeOff&) -> uint8_t { return 2; },
            [](const nspc::VcmdPitchSlideToNote&) -> uint8_t { return 2; },
            [](const nspc::VcmdVibratoOn&) -> uint8_t { return 3; },
            [](const nspc::VcmdVibratoOff&) -> uint8_t { return 3; },
            [](const nspc::VcmdVibratoFadeIn&) -> uint8_t { return 3; },
            [](const nspc::VcmdTremoloOn&) -> uint8_t { return 3; },
            [](const nspc::VcmdTremoloOff&) -> uint8_t { return 3; },
            [](const nspc::VcmdEchoOn&) -> uint8_t { return 4; },
            [](const nspc::VcmdEchoOff&) -> uint8_t { return 4; },
            [](const nspc::VcmdEchoParams&) -> uint8_t { return 4; },
            [](const nspc::VcmdEchoVolumeFade&) -> uint8_t { return 4; },
            [](const nspc::VcmdTempo&) -> uint8_t { return 5; },
            [](const nspc::VcmdTempoFade&) -> uint8_t { return 5; },
            [](const auto&) -> uint8_t { return 0; },
        },
        cmd.vcmd);
}

std::string vcmdTooltipText(const nspc::Vcmd& cmd) {
    return std::visit(
        nspc::overloaded{
            [](const std::monostate&) { return std::string{}; },
            [](const nspc::VcmdInst& v) { return std::format("Instrument ${:02X}", v.instrumentIndex); },
            [](const nspc::VcmdPanning& v) { return std::format("Panning ${:02X}", v.panning); },
            [](const nspc::VcmdPanFade& v) {
                return std::format("Pan Fade: time={} target=${:02X}", v.time, v.target);
            },
            [](const nspc::VcmdVibratoOn& v) {
                return std::format("Vibrato On: delay={} rate={} depth={}", v.delay, v.rate, v.depth);
            },
            [](const nspc::VcmdVibratoOff&) { return std::string{"Vibrato Off"}; },
            [](const nspc::VcmdGlobalVolume& v) { return std::format("Global Volume ${:02X}", v.volume); },
            [](const nspc::VcmdGlobalVolumeFade& v) {
                return std::format("Global Vol Fade: time={} target=${:02X}", v.time, v.target);
            },
            [](const nspc::VcmdTempo& v) { return std::format("Tempo ${:04X}", v.tempo); },
            [](const nspc::VcmdTempoFade& v) {
                return std::format("Tempo Fade: time={} target=${:04X}", v.time, v.target);
            },
            [](const nspc::VcmdGlobalTranspose& v) {
                return std::format("Global Transpose: {} semitones", v.semitones);
            },
            [](const nspc::VcmdPerVoiceTranspose& v) {
                return std::format("Voice Transpose: {} semitones", v.semitones);
            },
            [](const nspc::VcmdTremoloOn& v) {
                return std::format("Tremolo On: delay={} rate={} depth={}", v.delay, v.rate, v.depth);
            },
            [](const nspc::VcmdTremoloOff&) { return std::string{"Tremolo Off"}; },
            [](const nspc::VcmdVolume& v) { return std::format("Volume ${:02X}", v.volume); },
            [](const nspc::VcmdVolumeFade& v) {
                return std::format("Volume Fade: time={} target=${:02X}", v.time, v.target);
            },
            [](const nspc::VcmdSubroutineCall& v) {
                return std::format("Subroutine {}: {} iterations (loop count ${:02X})", v.subroutineId,
                                   static_cast<uint16_t>(v.count), v.count);
            },
            [](const nspc::VcmdVibratoFadeIn& v) { return std::format("Vibrato Fade In: time={}", v.time); },
            [](const nspc::VcmdPitchEnvelopeTo& v) {
                return std::format("Pitch Env To: delay={} len={} note={}", v.delay, v.length, v.semitone);
            },
            [](const nspc::VcmdPitchEnvelopeFrom& v) {
                return std::format("Pitch Env From: delay={} len={} note={}", v.delay, v.length, v.semitone);
            },
            [](const nspc::VcmdPitchEnvelopeOff&) { return std::string{"Pitch Envelope Off"}; },
            [](const nspc::VcmdFineTune& v) { return std::format("Fine Tune: {} semitones", v.semitones); },
            [](const nspc::VcmdEchoOn& v) {
                return std::format("Echo On: ch=${:02X} L=${:02X} R=${:02X}", v.channels, v.left, v.right);
            },
            [](const nspc::VcmdEchoOff&) { return std::string{"Echo Off"}; },
            [](const nspc::VcmdEchoParams& v) {
                return std::format("Echo: delay={} feedback=${:02X} FIR={}", v.delay, v.feedback, v.firIndex);
            },
            [](const nspc::VcmdEchoVolumeFade& v) {
                return std::format("Echo Vol Fade: time={} L=${:02X} R=${:02X}", v.time, v.leftTarget,
                                   v.rightTarget);
            },
            [](const nspc::VcmdPitchSlideToNote& v) {
                return std::format("Pitch Slide: delay={} len={} note={}", v.delay, v.length, v.note);
            },
            [](const nspc::VcmdPercussionBaseInstrument& v) {
                return std::format("Percussion Base ${:02X}", v.index);
            },
            [](const nspc::VcmdNOP& v) { return std::format("NOP ({} bytes)", v.nopBytes); },
            [](const nspc::VcmdMuteChannel&) { return std::string{"Mute Channel"}; },
            [](const nspc::VcmdFastForwardOn&) { return std::string{"Fast Forward On"}; },
            [](const nspc::VcmdFastForwardOff&) { return std::string{"Fast Forward Off"}; },
            [](const nspc::VcmdUnused&) { return std::string{"Unused"}; },
            [](const nspc::VcmdExtension& v) {
                std::string text = std::format("Extension VCMD: FF {:02X}", v.id);
                const size_t count = std::min<size_t>(v.paramCount, v.params.size());
                for (size_t i = 0; i < count; ++i) {
                    text += std::format(" {:02X}", v.params[i]);
                }
                return text;
            },
        },
        cmd.vcmd);
}


FxCategoryStyle fxCategoryStyle(uint8_t category) {
    switch (category) {
    case 1:  // vol/pan
        return {IM_COL32(100, 65, 20, 220), IM_COL32(200, 140, 50, 220), IM_COL32(255, 220, 160, 255)};
    case 2:  // pitch
        return {IM_COL32(20, 70, 100, 220), IM_COL32(50, 150, 200, 220), IM_COL32(170, 225, 255, 255)};
    case 3:  // modulation
        return {IM_COL32(65, 25, 85, 220), IM_COL32(140, 70, 180, 220), IM_COL32(220, 180, 255, 255)};
    case 4:  // echo
        return {IM_COL32(20, 75, 35, 220), IM_COL32(50, 170, 80, 220), IM_COL32(170, 255, 190, 255)};
    case 5:  // tempo
        return {IM_COL32(60, 60, 70, 220), IM_COL32(150, 150, 170, 220), IM_COL32(240, 240, 255, 255)};
    case 6:  // subroutine call
        return {IM_COL32(65, 55, 20, 220), IM_COL32(215, 180, 70, 220), IM_COL32(255, 240, 170, 255)};
    default:  // other
        return {IM_COL32(35, 65, 110, 220), IM_COL32(80, 120, 180, 220), IM_COL32(210, 230, 255, 255)};
    }
}

int parseHexValue(std::string_view text) {
    int value = 0;
    for (const char c : text) {
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= (c - '0');
        } else if (c >= 'A' && c <= 'F') {
            value |= (c - 'A' + 10);
        }
    }
    return value;
}

const char* itemLabel(int item) {
    switch (item) {
    case 0:
        return "Note";
    case 1:
        return "Inst";
    case 2:
        return "Vol";
    case 3:
        return "Qv";
    case 4:
        return "Fx";
    default:
        return "Note";
    }
}

// Returns true if vcmd ID is editable as an FX-like row command (excludes Inst, Vol, NOP, Unused).
bool isEditableFxId(uint8_t id) {
    return id >= 0xE0 && id != 0xE0 && id != 0xED && id != 0xFB && id != 0xFF;
}


RawVcmdBytes makeRawVcmd(uint8_t id) {
    return RawVcmdBytes{
        .id = id,
        .params = {},
        .paramCount = 0,
    };
}

RawVcmdBytes makeRawVcmd(uint8_t id, uint8_t p0) {
    return RawVcmdBytes{
        .id = id,
        .params = {p0, 0, 0, 0},
        .paramCount = 1,
    };
}

RawVcmdBytes makeRawVcmd(uint8_t id, uint8_t p0, uint8_t p1) {
    return RawVcmdBytes{
        .id = id,
        .params = {p0, p1, 0, 0},
        .paramCount = 2,
    };
}

RawVcmdBytes makeRawVcmd(uint8_t id, uint8_t p0, uint8_t p1, uint8_t p2) {
    return RawVcmdBytes{
        .id = id,
        .params = {p0, p1, p2, 0},
        .paramCount = 3,
    };
}

RawVcmdBytes makeRawVcmd(uint8_t id, const std::array<uint8_t, 4>& params, uint8_t paramCount) {
    return RawVcmdBytes{
        .id = id,
        .params = params,
        .paramCount = static_cast<uint8_t>(std::min<size_t>(paramCount, params.size())),
    };
}

std::optional<RawVcmdBytes> rawVcmdBytes(const nspc::Vcmd& cmd) {
    return std::visit(
        nspc::overloaded{
            [](const std::monostate&) -> std::optional<RawVcmdBytes> { return std::nullopt; },
            [](const nspc::VcmdInst& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdInst::id, v.instrumentIndex);
            },
            [](const nspc::VcmdPanning& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdPanning::id, v.panning);
            },
            [](const nspc::VcmdPanFade& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdPanFade::id, v.time, v.target);
            },
            [](const nspc::VcmdVibratoOn& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdVibratoOn::id, v.delay, v.rate, v.depth);
            },
            [](const nspc::VcmdVibratoOff&) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdVibratoOff::id);
            },
            [](const nspc::VcmdGlobalVolume& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdGlobalVolume::id, v.volume);
            },
            [](const nspc::VcmdGlobalVolumeFade& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdGlobalVolumeFade::id, v.time, v.target);
            },
            [](const nspc::VcmdTempo& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdTempo::id, v.tempo);
            },
            [](const nspc::VcmdTempoFade& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdTempoFade::id, v.time, v.target);
            },
            [](const nspc::VcmdGlobalTranspose& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdGlobalTranspose::id, static_cast<uint8_t>(v.semitones));
            },
            [](const nspc::VcmdPerVoiceTranspose& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdPerVoiceTranspose::id, static_cast<uint8_t>(v.semitones));
            },
            [](const nspc::VcmdTremoloOn& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdTremoloOn::id, v.delay, v.rate, v.depth);
            },
            [](const nspc::VcmdTremoloOff&) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdTremoloOff::id);
            },
            [](const nspc::VcmdVolume& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdVolume::id, v.volume);
            },
            [](const nspc::VcmdVolumeFade& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdVolumeFade::id, v.time, v.target);
            },
            [](const nspc::VcmdSubroutineCall& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdSubroutineCall::id, static_cast<uint8_t>(v.originalAddr & 0xFF),
                                   static_cast<uint8_t>((v.originalAddr >> 8) & 0xFF), v.count);
            },
            [](const nspc::VcmdVibratoFadeIn& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdVibratoFadeIn::id, v.time);
            },
            [](const nspc::VcmdPitchEnvelopeTo& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdPitchEnvelopeTo::id, v.delay, v.length, v.semitone);
            },
            [](const nspc::VcmdPitchEnvelopeFrom& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdPitchEnvelopeFrom::id, v.delay, v.length, v.semitone);
            },
            [](const nspc::VcmdPitchEnvelopeOff&) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdPitchEnvelopeOff::id);
            },
            [](const nspc::VcmdFineTune& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdFineTune::id, static_cast<uint8_t>(v.semitones));
            },
            [](const nspc::VcmdEchoOn& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdEchoOn::id, v.channels, v.left, v.right);
            },
            [](const nspc::VcmdEchoOff&) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdEchoOff::id);
            },
            [](const nspc::VcmdEchoParams& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdEchoParams::id, v.delay, v.feedback, v.firIndex);
            },
            [](const nspc::VcmdEchoVolumeFade& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdEchoVolumeFade::id, v.time, v.leftTarget, v.rightTarget);
            },
            [](const nspc::VcmdPitchSlideToNote& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdPitchSlideToNote::id, v.delay, v.length, v.note);
            },
            [](const nspc::VcmdPercussionBaseInstrument& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdPercussionBaseInstrument::id, v.index);
            },
            [](const nspc::VcmdNOP& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdNOP::id, static_cast<uint8_t>(v.nopBytes & 0xFF),
                                   static_cast<uint8_t>((v.nopBytes >> 8) & 0xFF));
            },
            [](const nspc::VcmdMuteChannel&) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdMuteChannel::id);
            },
            [](const nspc::VcmdFastForwardOn&) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdFastForwardOn::id);
            },
            [](const nspc::VcmdFastForwardOff&) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdFastForwardOff::id);
            },
            [](const nspc::VcmdUnused&) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(nspc::VcmdUnused::id);
            },
            [](const nspc::VcmdExtension& v) -> std::optional<RawVcmdBytes> {
                return makeRawVcmd(v.id, v.params, v.paramCount);
            },
        },
        cmd.vcmd);
}

std::string vcmdInlineText(uint8_t id, const std::array<uint8_t, 4>& params, uint8_t paramCount) {
    std::string text = std::format("{:02X}", id);
    const size_t count = std::min<size_t>(paramCount, params.size());
    for (size_t i = 0; i < count; ++i) {
        text += std::format(" {:02X}", params[i]);
    }
    return text;
}

std::string vcmdPackedHex(uint8_t id, const std::array<uint8_t, 4>& params, uint8_t paramCount) {
    std::string text = std::format("{:02X}", id);
    const size_t count = std::min<size_t>(paramCount, params.size());
    for (size_t i = 0; i < count; ++i) {
        text += std::format("{:02X}", params[i]);
    }
    return text;
}

std::string sanitizeHexInput(std::string_view text) {
    std::string hex;
    hex.reserve(text.size());
    for (const unsigned char c : text) {
        if (std::isxdigit(c) == 0) {
            continue;
        }
        hex.push_back(static_cast<char>(std::toupper(c)));
    }
    return hex;
}

std::optional<nspc::Vcmd> parseTypedFxCommand(std::string_view text) {
    const std::string hex = sanitizeHexInput(text);
    if (hex.size() < 2 || (hex.size() % 2) != 0) {
        return std::nullopt;
    }

    const auto hexView = std::string_view(hex);
    const uint8_t id = static_cast<uint8_t>(parseHexValue(hexView.substr(0, 2)));
    if (!isEditableFxId(id)) {
        return std::nullopt;
    }

    const int paramCount = nspc::vcmdParamByteCount(id);
    if (hex.size() != (2 + static_cast<size_t>(paramCount) * 2)) {
        return std::nullopt;
    }

    std::array<uint8_t, 4> params{};
    for (int i = 0; i < paramCount; ++i) {
        params[static_cast<size_t>(i)] =
            static_cast<uint8_t>(parseHexValue(hexView.substr(2 + static_cast<size_t>(i) * 2, 2)));
    }
    return nspc::constructVcmd(id, params.data());
}

}  // namespace ntrak::ui::detail
