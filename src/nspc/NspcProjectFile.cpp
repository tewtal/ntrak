#include "ntrak/nspc/NspcProjectFile.hpp"
#include "ntrak/nspc/Base64.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

using json = nlohmann::json;

namespace ntrak::nspc {
namespace {

constexpr std::string_view kProjectFormatTag = "ntrak_project_ir";
constexpr int kProjectFormatVersion = 4;
constexpr std::string_view kPackedEventsEncoding = "eventpack_v1";
constexpr uint8_t kPackedEventsEncodingVersion = 1;
constexpr uint32_t kAramSize = NspcAramUsage::kTotalAramBytes;

bool isSmwV00Engine(const NspcEngineConfig& config) {
    return config.engineVersion == "0.0";
}

void writeOverlayInstrumentToAram(NspcProject& project, const NspcInstrument& instrument) {
    const auto& config = project.engineConfig();
    if (config.instrumentHeaders == 0 || instrument.id < 0) {
        return;
    }

    const uint8_t entrySize = std::clamp<uint8_t>(config.instrumentEntryBytes, 5, 6);
    const uint32_t address = static_cast<uint32_t>(config.instrumentHeaders) +
                             static_cast<uint32_t>(instrument.id) * static_cast<uint32_t>(entrySize);
    if (address + entrySize > kAramSize) {
        return;
    }

    auto aram = project.aram();
    const uint16_t base = static_cast<uint16_t>(address);
    aram.write(base + 0u, instrument.sampleIndex);
    aram.write(base + 1u, instrument.adsr1);
    aram.write(base + 2u, instrument.adsr2);
    aram.write(base + 3u, instrument.gain);
    aram.write(base + 4u, instrument.basePitchMult);
    if (entrySize >= 6) {
        aram.write(base + 5u, instrument.fracPitchMult);
    }

    if (isSmwV00Engine(config) && config.percussionHeaders != 0 && instrument.id >= 0) {
        const auto commandMap = config.commandMap.value_or(NspcCommandMap{});
        const int percussionCount =
            static_cast<int>(commandMap.percussionEnd) - static_cast<int>(commandMap.percussionStart) + 1;
        if (instrument.id < percussionCount) {
            const uint32_t percussionAddress =
                static_cast<uint32_t>(config.percussionHeaders) + static_cast<uint32_t>(instrument.id) * 6u;
            if (percussionAddress + 6u <= kAramSize) {
                const uint16_t percussionBase = static_cast<uint16_t>(percussionAddress);
                aram.write(percussionBase + 0u, instrument.sampleIndex);
                aram.write(percussionBase + 1u, instrument.adsr1);
                aram.write(percussionBase + 2u, instrument.adsr2);
                aram.write(percussionBase + 3u, instrument.gain);
                aram.write(percussionBase + 4u, instrument.basePitchMult);
                aram.write(percussionBase + 5u, instrument.percussionNote);
            }
        }
    }
}

void writeOverlaySampleToAram(NspcProject& project, const BrrSample& sample) {
    const auto& config = project.engineConfig();
    if (config.sampleHeaders == 0 || sample.id < 0) {
        return;
    }

    const uint32_t directoryAddress =
        static_cast<uint32_t>(config.sampleHeaders) + static_cast<uint32_t>(sample.id) * 4u;
    if (directoryAddress + 4u > kAramSize) {
        return;
    }

    auto aram = project.aram();
    const uint16_t directoryBase = static_cast<uint16_t>(directoryAddress);
    aram.write16(directoryBase, sample.originalAddr);
    aram.write16(static_cast<uint16_t>(directoryBase + 2u), sample.originalLoopAddr);

    if (sample.originalAddr == 0 || sample.data.empty()) {
        return;
    }

    const uint32_t sampleSize = static_cast<uint32_t>(sample.data.size());
    if (sampleSize != sample.data.size()) {
        return;
    }
    const uint32_t sampleEnd = static_cast<uint32_t>(sample.originalAddr) + sampleSize;
    if (sampleEnd > kAramSize) {
        return;
    }

    auto dst = aram.bytes(sample.originalAddr, sample.data.size());
    std::copy(sample.data.begin(), sample.data.end(), dst.begin());
}

std::string contentOriginToString(NspcContentOrigin origin) {
    return (origin == NspcContentOrigin::UserProvided) ? "user" : "engine";
}

NspcContentOrigin parseContentOrigin(const json& value, NspcContentOrigin fallback = NspcContentOrigin::UserProvided) {
    if (!value.is_string()) {
        return fallback;
    }
    const std::string text = value.get<std::string>();
    if (text == "engine") {
        return NspcContentOrigin::EngineProvided;
    }
    if (text == "user") {
        return NspcContentOrigin::UserProvided;
    }
    return fallback;
}

std::optional<uint16_t> parseU16(const json& value) {
    if (value.is_number_unsigned()) {
        return static_cast<uint16_t>(value.get<uint64_t>() & 0xFFFFu);
    }
    if (value.is_number_integer()) {
        const int64_t raw = value.get<int64_t>();
        if (raw < 0 || raw > std::numeric_limits<uint16_t>::max()) {
            return std::nullopt;
        }
        return static_cast<uint16_t>(raw);
    }
    if (value.is_string()) {
        std::string text = value.get<std::string>();
        if (text.empty()) {
            return std::nullopt;
        }
        if (text.starts_with("0x") || text.starts_with("0X")) {
            text = text.substr(2);
        } else if (text.starts_with("$")) {
            text = text.substr(1);
        }
        try {
            const auto raw = std::stoul(text, nullptr, 16);
            return static_cast<uint16_t>(raw & 0xFFFFu);
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<uint8_t> parseU8(const json& value) {
    if (const auto parsed = parseU16(value); parsed.has_value()) {
        return static_cast<uint8_t>(*parsed & 0xFFu);
    }
    return std::nullopt;
}

std::optional<int> parseInt(const json& value) {
    if (value.is_number_integer()) {
        return value.get<int>();
    }
    if (value.is_number_unsigned()) {
        return static_cast<int>(value.get<unsigned int>());
    }
    return std::nullopt;
}

std::vector<int> normalizeIdList(std::vector<int> ids) {
    ids.erase(std::remove_if(ids.begin(), ids.end(), [](int id) { return id < 0; }), ids.end());
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

std::expected<std::vector<int>, std::string> parseIdList(const json& value, std::string_view fieldLabel) {
    if (!value.is_array()) {
        return std::unexpected(std::format("Project {} payload must be an array", fieldLabel));
    }

    std::vector<int> ids;
    ids.reserve(value.size());
    for (const auto& idValue : value) {
        const auto parsed = parseInt(idValue);
        if (!parsed.has_value() || *parsed < 0) {
            return std::unexpected(std::format("Project {} contains an invalid id", fieldLabel));
        }
        ids.push_back(*parsed);
    }
    return normalizeIdList(std::move(ids));
}

void clearInstrumentEntryInAram(NspcProject& project, int instrumentId) {
    const auto& config = project.engineConfig();
    if (config.instrumentHeaders == 0 || instrumentId < 0) {
        return;
    }

    const uint8_t entrySize = std::clamp<uint8_t>(config.instrumentEntryBytes, 5, 6);
    const uint32_t address =
        static_cast<uint32_t>(config.instrumentHeaders) + static_cast<uint32_t>(instrumentId) * static_cast<uint32_t>(entrySize);
    if (address + entrySize > kAramSize) {
        return;
    }

    auto aram = project.aram();
    const uint16_t base = static_cast<uint16_t>(address);
    for (uint8_t i = 0; i < entrySize; ++i) {
        aram.write(static_cast<uint16_t>(base + i), 0);
    }
}

void clearSampleInAram(NspcProject& project, const BrrSample& sample) {
    const auto& config = project.engineConfig();
    if (config.sampleHeaders != 0 && sample.id >= 0) {
        const uint32_t directoryAddress =
            static_cast<uint32_t>(config.sampleHeaders) + static_cast<uint32_t>(sample.id) * 4u;
        if (directoryAddress + 4u <= kAramSize) {
            auto aram = project.aram();
            const uint16_t dirBase = static_cast<uint16_t>(directoryAddress);
            aram.write16(dirBase, 0);
            aram.write16(static_cast<uint16_t>(dirBase + 2u), 0);
        }
    }

    if (sample.originalAddr == 0 || sample.data.empty()) {
        return;
    }

    const uint32_t sampleSize = static_cast<uint32_t>(sample.data.size());
    if (sampleSize != sample.data.size()) {
        return;
    }
    const uint32_t sampleEnd = static_cast<uint32_t>(sample.originalAddr) + sampleSize;
    if (sampleEnd > kAramSize) {
        return;
    }

    auto aram = project.aram();
    auto bytes = aram.bytes(sample.originalAddr, sample.data.size());
    std::fill(bytes.begin(), bytes.end(), 0);
}

void pruneEngineSongs(NspcProject& project, const std::vector<int>& retainedIds) {
    const std::unordered_set<int> retained(retainedIds.begin(), retainedIds.end());
    for (size_t index = project.songs().size(); index-- > 0;) {
        const auto& song = project.songs()[index];
        if (!song.isEngineProvided()) {
            continue;
        }
        if (retained.contains(song.songId())) {
            continue;
        }
        (void)project.removeSong(index);
    }
}

void pruneEngineInstruments(NspcProject& project, const std::vector<int>& retainedIds) {
    const std::unordered_set<int> retained(retainedIds.begin(), retainedIds.end());
    auto& instruments = project.instruments();
    auto it = instruments.begin();
    while (it != instruments.end()) {
        if (it->contentOrigin != NspcContentOrigin::EngineProvided || retained.contains(it->id)) {
            ++it;
            continue;
        }
        clearInstrumentEntryInAram(project, it->id);
        it = instruments.erase(it);
    }
}

void pruneEngineSamples(NspcProject& project, const std::vector<int>& retainedIds) {
    const std::unordered_set<int> retained(retainedIds.begin(), retainedIds.end());
    auto& samples = project.samples();
    auto it = samples.begin();
    while (it != samples.end()) {
        if (it->contentOrigin != NspcContentOrigin::EngineProvided || retained.contains(it->id)) {
            ++it;
            continue;
        }
        clearSampleInAram(project, *it);
        it = samples.erase(it);
    }
}

json serializeSequenceOp(const NspcSequenceOp& op) {
    return std::visit(
        overloaded{
            [](const PlayPattern& value) {
                return json{
                    {"type", "playPattern"},
                    {"patternId", value.patternId},
                    {"trackTableAddr", value.trackTableAddr},
                };
            },
            [](const JumpTimes& value) {
                json out{
                    {"type", "jumpTimes"},
                    {"count", value.count},
                    {"targetAddr", value.target.addr},
                };
                if (value.target.index.has_value()) {
                    out["targetIndex"] = *value.target.index;
                }
                return out;
            },
            [](const AlwaysJump& value) {
                json out{
                    {"type", "alwaysJump"},
                    {"opcode", value.opcode},
                    {"targetAddr", value.target.addr},
                };
                if (value.target.index.has_value()) {
                    out["targetIndex"] = *value.target.index;
                }
                return out;
            },
            [](const FastForwardOn&) { return json{{"type", "fastForwardOn"}}; },
            [](const FastForwardOff&) { return json{{"type", "fastForwardOff"}}; },
            [](const EndSequence&) { return json{{"type", "endSequence"}}; },
        },
        op);
}

std::expected<NspcSequenceOp, std::string> parseSequenceOp(const json& value) {
    if (!value.is_object()) {
        return std::unexpected("Sequence op entry must be an object");
    }
    const std::string type = value.value("type", "");
    if (type == "playPattern") {
        const auto patternId = parseInt(value.value("patternId", -1));
        const auto trackTableAddr = parseU16(value.value("trackTableAddr", 0));
        if (!patternId.has_value() || !trackTableAddr.has_value()) {
            return std::unexpected("Invalid playPattern sequence op");
        }
        return PlayPattern{
            .patternId = *patternId,
            .trackTableAddr = *trackTableAddr,
        };
    }
    if (type == "jumpTimes") {
        const auto count = parseU8(value.value("count", 1));
        const auto targetAddr = parseU16(value.value("targetAddr", 0));
        if (!count.has_value() || !targetAddr.has_value()) {
            return std::unexpected("Invalid jumpTimes sequence op");
        }
        SequenceTarget target{
            .index = std::nullopt,
            .addr = *targetAddr,
        };
        if (value.contains("targetIndex")) {
            target.index = parseInt(value["targetIndex"]);
        }
        return JumpTimes{
            .count = *count,
            .target = target,
        };
    }
    if (type == "alwaysJump") {
        const auto opcode = parseU8(value.value("opcode", 0x82));
        const auto targetAddr = parseU16(value.value("targetAddr", 0));
        if (!opcode.has_value() || !targetAddr.has_value()) {
            return std::unexpected("Invalid alwaysJump sequence op");
        }
        SequenceTarget target{
            .index = std::nullopt,
            .addr = *targetAddr,
        };
        if (value.contains("targetIndex")) {
            target.index = parseInt(value["targetIndex"]);
        }
        return AlwaysJump{
            .opcode = *opcode,
            .target = target,
        };
    }
    if (type == "fastForwardOn") {
        return FastForwardOn{};
    }
    if (type == "fastForwardOff") {
        return FastForwardOff{};
    }
    if (type == "endSequence") {
        return EndSequence{};
    }
    return std::unexpected(std::format("Unknown sequence op type '{}'", type));
}

enum class PackedEventKind : uint8_t {
    Empty = 0,
    Duration = 1,
    Vcmd = 2,
    Note = 3,
    Tie = 4,
    Rest = 5,
    Percussion = 6,
    Subroutine = 7,
    End = 8,
};

void appendU16Le(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
}

void appendVarUint(std::vector<uint8_t>& out, uint64_t value) {
    do {
        uint8_t byte = static_cast<uint8_t>(value & 0x7Fu);
        value >>= 7u;
        if (value != 0) {
            byte |= 0x80u;
        }
        out.push_back(byte);
    } while (value != 0);
}

void appendVarInt(std::vector<uint8_t>& out, int64_t value) {
    const uint64_t zigZag = (static_cast<uint64_t>(value) << 1u) ^ static_cast<uint64_t>(value >> 63u);
    appendVarUint(out, zigZag);
}

bool readU16Le(const std::vector<uint8_t>& bytes, size_t& offset, uint16_t& value) {
    if (offset + 2u > bytes.size()) {
        return false;
    }
    value = static_cast<uint16_t>(bytes[offset] | (static_cast<uint16_t>(bytes[offset + 1u]) << 8u));
    offset += 2u;
    return true;
}

bool readVarUint(const std::vector<uint8_t>& bytes, size_t& offset, uint64_t& value) {
    value = 0;
    for (int i = 0; i < 10; ++i) {
        if (offset >= bytes.size()) {
            return false;
        }
        const uint8_t byte = bytes[offset++];
        if (i == 9 && (byte & 0xFEu) != 0) {
            return false;
        }
        value |= static_cast<uint64_t>(byte & 0x7Fu) << (i * 7);
        if ((byte & 0x80u) == 0) {
            return true;
        }
    }
    return false;
}

bool readVarInt(const std::vector<uint8_t>& bytes, size_t& offset, int64_t& value) {
    uint64_t raw = 0;
    if (!readVarUint(bytes, offset, raw)) {
        return false;
    }
    const uint64_t sign = 0u - (raw & 1u);
    value = static_cast<int64_t>((raw >> 1u) ^ sign);
    return true;
}

struct RawVcmd {
    uint8_t id = 0;
    std::array<uint8_t, 4> params{};
    uint8_t paramCount = 0;
    std::optional<int> subroutineId;
    std::optional<uint16_t> originalAddr;
};

std::optional<RawVcmd> toRawVcmd(const Vcmd& cmd) {
    return std::visit(
        overloaded{
            [](const std::monostate&) -> std::optional<RawVcmd> { return std::nullopt; },
            [](const VcmdInst& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdInst::id, .paramCount = 1};
                raw.params[0] = value.instrumentIndex;
                return raw;
            },
            [](const VcmdPanning& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdPanning::id, .paramCount = 1};
                raw.params[0] = value.panning;
                return raw;
            },
            [](const VcmdPanFade& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdPanFade::id, .paramCount = 2};
                raw.params[0] = value.time;
                raw.params[1] = value.target;
                return raw;
            },
            [](const VcmdVibratoOn& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdVibratoOn::id, .paramCount = 3};
                raw.params[0] = value.delay;
                raw.params[1] = value.rate;
                raw.params[2] = value.depth;
                return raw;
            },
            [](const VcmdVibratoOff&) -> std::optional<RawVcmd> { return RawVcmd{.id = VcmdVibratoOff::id, .paramCount = 0}; },
            [](const VcmdGlobalVolume& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdGlobalVolume::id, .paramCount = 1};
                raw.params[0] = value.volume;
                return raw;
            },
            [](const VcmdGlobalVolumeFade& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdGlobalVolumeFade::id, .paramCount = 2};
                raw.params[0] = value.time;
                raw.params[1] = value.target;
                return raw;
            },
            [](const VcmdTempo& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdTempo::id, .paramCount = 1};
                raw.params[0] = value.tempo;
                return raw;
            },
            [](const VcmdTempoFade& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdTempoFade::id, .paramCount = 2};
                raw.params[0] = value.time;
                raw.params[1] = value.target;
                return raw;
            },
            [](const VcmdGlobalTranspose& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdGlobalTranspose::id, .paramCount = 1};
                raw.params[0] = static_cast<uint8_t>(value.semitones);
                return raw;
            },
            [](const VcmdPerVoiceTranspose& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdPerVoiceTranspose::id, .paramCount = 1};
                raw.params[0] = static_cast<uint8_t>(value.semitones);
                return raw;
            },
            [](const VcmdTremoloOn& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdTremoloOn::id, .paramCount = 3};
                raw.params[0] = value.delay;
                raw.params[1] = value.rate;
                raw.params[2] = value.depth;
                return raw;
            },
            [](const VcmdTremoloOff&) -> std::optional<RawVcmd> { return RawVcmd{.id = VcmdTremoloOff::id, .paramCount = 0}; },
            [](const VcmdVolume& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdVolume::id, .paramCount = 1};
                raw.params[0] = value.volume;
                return raw;
            },
            [](const VcmdVolumeFade& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdVolumeFade::id, .paramCount = 2};
                raw.params[0] = value.time;
                raw.params[1] = value.target;
                return raw;
            },
            [](const VcmdSubroutineCall& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdSubroutineCall::id, .paramCount = 3};
                raw.params[0] = static_cast<uint8_t>(value.originalAddr & 0xFFu);
                raw.params[1] = static_cast<uint8_t>((value.originalAddr >> 8) & 0xFFu);
                raw.params[2] = value.count;
                raw.subroutineId = value.subroutineId;
                raw.originalAddr = value.originalAddr;
                return raw;
            },
            [](const VcmdVibratoFadeIn& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdVibratoFadeIn::id, .paramCount = 1};
                raw.params[0] = value.time;
                return raw;
            },
            [](const VcmdPitchEnvelopeTo& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdPitchEnvelopeTo::id, .paramCount = 3};
                raw.params[0] = value.delay;
                raw.params[1] = value.length;
                raw.params[2] = value.semitone;
                return raw;
            },
            [](const VcmdPitchEnvelopeFrom& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdPitchEnvelopeFrom::id, .paramCount = 3};
                raw.params[0] = value.delay;
                raw.params[1] = value.length;
                raw.params[2] = value.semitone;
                return raw;
            },
            [](const VcmdPitchEnvelopeOff&) -> std::optional<RawVcmd> { return RawVcmd{.id = VcmdPitchEnvelopeOff::id, .paramCount = 0}; },
            [](const VcmdFineTune& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdFineTune::id, .paramCount = 1};
                raw.params[0] = static_cast<uint8_t>(value.semitones);
                return raw;
            },
            [](const VcmdEchoOn& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdEchoOn::id, .paramCount = 3};
                raw.params[0] = value.channels;
                raw.params[1] = value.left;
                raw.params[2] = value.right;
                return raw;
            },
            [](const VcmdEchoOff&) -> std::optional<RawVcmd> { return RawVcmd{.id = VcmdEchoOff::id, .paramCount = 0}; },
            [](const VcmdEchoParams& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdEchoParams::id, .paramCount = 3};
                raw.params[0] = value.delay;
                raw.params[1] = value.feedback;
                raw.params[2] = value.firIndex;
                return raw;
            },
            [](const VcmdEchoVolumeFade& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdEchoVolumeFade::id, .paramCount = 3};
                raw.params[0] = value.time;
                raw.params[1] = value.leftTarget;
                raw.params[2] = value.rightTarget;
                return raw;
            },
            [](const VcmdPitchSlideToNote& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdPitchSlideToNote::id, .paramCount = 3};
                raw.params[0] = value.delay;
                raw.params[1] = value.length;
                raw.params[2] = value.note;
                return raw;
            },
            [](const VcmdPercussionBaseInstrument& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdPercussionBaseInstrument::id, .paramCount = 1};
                raw.params[0] = value.index;
                return raw;
            },
            [](const VcmdNOP& value) -> std::optional<RawVcmd> {
                RawVcmd raw{.id = VcmdNOP::id, .paramCount = 2};
                raw.params[0] = static_cast<uint8_t>(value.nopBytes & 0xFFu);
                raw.params[1] = static_cast<uint8_t>((value.nopBytes >> 8) & 0xFFu);
                return raw;
            },
            [](const VcmdMuteChannel&) -> std::optional<RawVcmd> { return RawVcmd{.id = VcmdMuteChannel::id, .paramCount = 0}; },
            [](const VcmdFastForwardOn&) -> std::optional<RawVcmd> {
                return RawVcmd{.id = VcmdFastForwardOn::id, .paramCount = 0};
            },
            [](const VcmdFastForwardOff&) -> std::optional<RawVcmd> {
                return RawVcmd{.id = VcmdFastForwardOff::id, .paramCount = 0};
            },
            [](const VcmdUnused&) -> std::optional<RawVcmd> { return RawVcmd{.id = VcmdUnused::id, .paramCount = 0}; },
            [](const VcmdExtension& value) -> std::optional<RawVcmd> {
                RawVcmd raw{
                    .id = value.id,
                    .params = value.params,
                    .paramCount = value.paramCount,
                };
                return raw;
            },
        },
        cmd.vcmd);
}

std::expected<Vcmd, std::string> parseVcmd(const json& value) {
    if (!value.is_object()) {
        return std::unexpected("VCMD payload must be an object");
    }

    const auto idOpt = parseU8(value.value("id", 0));
    if (!idOpt.has_value()) {
        return std::unexpected("VCMD payload is missing a valid id");
    }
    const uint8_t id = *idOpt;
    const bool isExtension = value.contains("extension") && value["extension"].is_boolean() &&
                             value["extension"].get<bool>();

    std::array<uint8_t, 4> params{};
    uint8_t parsedParamCount = 0;
    if (value.contains("params")) {
        if (!value["params"].is_array()) {
            return std::unexpected("VCMD params must be an array");
        }
        if (value["params"].size() > params.size()) {
            return std::unexpected(std::format("VCMD ${:02X} has too many params ({})", id, value["params"].size()));
        }
        parsedParamCount = static_cast<uint8_t>(value["params"].size());
        for (size_t i = 0; i < value["params"].size(); ++i) {
            const auto param = parseU8(value["params"][i]);
            if (!param.has_value()) {
                return std::unexpected(std::format("VCMD ${:02X} has invalid param at index {}", id, i));
            }
            params[i] = *param;
        }
    }

    if (isExtension) {
        return Vcmd{VcmdExtension{
            .id = id,
            .params = params,
            .paramCount = parsedParamCount,
        }};
    }

    const uint8_t expectedParamCount = vcmdParamByteCount(id);
    if (parsedParamCount != expectedParamCount) {
        if (expectedParamCount == 0 && !value.contains("params")) {
            // Exact match for no-param VCMD without explicit payload.
        } else if (!value.contains("params")) {
            return std::unexpected(std::format("VCMD ${:02X} requires params", id));
        } else {
            return std::unexpected(std::format("VCMD ${:02X} expects {} params, got {}", id, expectedParamCount,
                                               parsedParamCount));
        }
    } else if (!value.contains("params") && expectedParamCount != 0) {
        return std::unexpected(std::format("VCMD ${:02X} requires params", id));
    }

    switch (id) {
    case VcmdInst::id:
        return Vcmd{VcmdInst{.instrumentIndex = params[0]}};
    case VcmdVolume::id:
        return Vcmd{VcmdVolume{.volume = params[0]}};
    case VcmdSubroutineCall::id: {
        const int subroutineId = value.value("subroutineId", -1);
        uint16_t originalAddr = static_cast<uint16_t>(params[0] | (static_cast<uint16_t>(params[1]) << 8u));
        if (value.contains("originalAddr")) {
            if (const auto parsed = parseU16(value["originalAddr"]); parsed.has_value()) {
                originalAddr = *parsed;
            }
        }
        return Vcmd{VcmdSubroutineCall{
            .subroutineId = subroutineId,
            .originalAddr = originalAddr,
            .count = params[2],
        }};
    }
    case VcmdNOP::id: {
        const uint16_t nopBytes = static_cast<uint16_t>(params[0] | (static_cast<uint16_t>(params[1]) << 8u));
        return Vcmd{VcmdNOP{.nopBytes = nopBytes}};
    }
    case VcmdUnused::id:
        return Vcmd{VcmdUnused{}};
    default:
        break;
    }

    if (const auto constructed = constructVcmd(id, params.data()); constructed.has_value()) {
        return *constructed;
    }
    return std::unexpected(std::format("Unsupported VCMD ${:02X} in project file", id));
}

PackedEventKind packedEventKindFor(const NspcEvent& event) {
    return std::visit(
        overloaded{
            [](const std::monostate&) { return PackedEventKind::Empty; },
            [](const Duration&) { return PackedEventKind::Duration; },
            [](const Vcmd&) { return PackedEventKind::Vcmd; },
            [](const Note&) { return PackedEventKind::Note; },
            [](const Tie&) { return PackedEventKind::Tie; },
            [](const Rest&) { return PackedEventKind::Rest; },
            [](const Percussion&) { return PackedEventKind::Percussion; },
            [](const Subroutine&) { return PackedEventKind::Subroutine; },
            [](const End&) { return PackedEventKind::End; },
        },
        event);
}

std::expected<std::vector<uint8_t>, std::string> packEventEntries(const std::vector<NspcEventEntry>& entries) {
    std::vector<uint8_t> out;
    out.reserve(1u + entries.size() * 8u);
    out.push_back(kPackedEventsEncodingVersion);
    appendVarUint(out, static_cast<uint64_t>(entries.size()));

    for (const auto& entry : entries) {
        appendVarUint(out, entry.id);

        const PackedEventKind kind = packedEventKindFor(entry.event);
        uint8_t header = static_cast<uint8_t>(static_cast<uint8_t>(kind) << 1u);
        if (entry.originalAddr.has_value()) {
            header |= 0x01u;
        }
        out.push_back(header);

        if (entry.originalAddr.has_value()) {
            appendU16Le(out, *entry.originalAddr);
        }

        auto encoded = std::visit(
            overloaded{
                [&](const std::monostate&) -> std::expected<void, std::string> { return {}; },
                [&](const Duration& value) -> std::expected<void, std::string> {
                    out.push_back(value.ticks);
                    uint8_t flags = 0;
                    if (value.quantization.has_value()) {
                        flags |= 0x01u;
                    }
                    if (value.velocity.has_value()) {
                        flags |= 0x02u;
                    }
                    out.push_back(flags);
                    if (value.quantization.has_value()) {
                        out.push_back(*value.quantization);
                    }
                    if (value.velocity.has_value()) {
                        out.push_back(*value.velocity);
                    }
                    return {};
                },
                [&](const Vcmd& value) -> std::expected<void, std::string> {
                    const auto raw = toRawVcmd(value);
                    if (!raw.has_value()) {
                        return std::unexpected("Unsupported VCMD payload in project event");
                    }
                    out.push_back(raw->id);
                    out.push_back(raw->paramCount);
                    for (uint8_t i = 0; i < raw->paramCount; ++i) {
                        out.push_back(raw->params[i]);
                    }
                    uint8_t flags = 0;
                    if (raw->subroutineId.has_value()) {
                        flags |= 0x01u;
                    }
                    if (raw->originalAddr.has_value()) {
                        flags |= 0x02u;
                    }
                    if (std::holds_alternative<VcmdExtension>(value.vcmd)) {
                        flags |= 0x04u;
                    }
                    out.push_back(flags);
                    if (raw->subroutineId.has_value()) {
                        appendVarInt(out, static_cast<int64_t>(*raw->subroutineId));
                    }
                    if (raw->originalAddr.has_value()) {
                        appendU16Le(out, *raw->originalAddr);
                    }
                    return {};
                },
                [&](const Note& value) -> std::expected<void, std::string> {
                    out.push_back(value.pitch);
                    return {};
                },
                [&](const Tie&) -> std::expected<void, std::string> { return {}; },
                [&](const Rest&) -> std::expected<void, std::string> { return {}; },
                [&](const Percussion& value) -> std::expected<void, std::string> {
                    out.push_back(value.index);
                    return {};
                },
                [&](const Subroutine& value) -> std::expected<void, std::string> {
                    appendVarInt(out, static_cast<int64_t>(value.id));
                    appendU16Le(out, value.originalAddr);
                    return {};
                },
                [&](const End&) -> std::expected<void, std::string> { return {}; },
            },
            entry.event);

        if (!encoded.has_value()) {
            return std::unexpected(encoded.error());
        }
    }

    return out;
}

std::expected<std::vector<NspcEventEntry>, std::string> unpackEventEntries(const std::vector<uint8_t>& bytes,
                                                                            std::string_view label) {
    auto fail = [&](std::string_view detail) -> std::unexpected<std::string> {
        return std::unexpected(std::format("{} packed events decode error: {}", label, detail));
    };

    size_t offset = 0;
    if (bytes.empty()) {
        return fail("payload is empty");
    }

    const uint8_t encodingVersion = bytes[offset++];
    if (encodingVersion != kPackedEventsEncodingVersion) {
        return fail(std::format("unsupported encoding version {}", encodingVersion));
    }

    uint64_t eventCountRaw = 0;
    if (!readVarUint(bytes, offset, eventCountRaw)) {
        return fail("missing event count");
    }
    if (eventCountRaw > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return fail("event count is too large");
    }

    std::vector<NspcEventEntry> out;
    out.reserve(static_cast<size_t>(eventCountRaw));

    for (size_t eventIndex = 0; eventIndex < static_cast<size_t>(eventCountRaw); ++eventIndex) {
        NspcEventEntry entry{};

        uint64_t entryIdRaw = 0;
        if (!readVarUint(bytes, offset, entryIdRaw)) {
            return fail(std::format("event {} has invalid id encoding", eventIndex));
        }
        entry.id = static_cast<NspcEventId>(entryIdRaw);
        if (static_cast<uint64_t>(entry.id) != entryIdRaw) {
            return fail(std::format("event {} id is out of range", eventIndex));
        }

        if (offset >= bytes.size()) {
            return fail(std::format("event {} is missing header", eventIndex));
        }
        const uint8_t header = bytes[offset++];
        const bool hasOriginalAddr = (header & 0x01u) != 0;
        const PackedEventKind kind = static_cast<PackedEventKind>((header >> 1u) & 0x0Fu);

        if (hasOriginalAddr) {
            uint16_t originalAddr = 0;
            if (!readU16Le(bytes, offset, originalAddr)) {
                return fail(std::format("event {} originalAddr is truncated", eventIndex));
            }
            entry.originalAddr = originalAddr;
        }

        switch (kind) {
        case PackedEventKind::Empty:
            entry.event = NspcEvent{std::monostate{}};
            break;
        case PackedEventKind::Duration: {
            if (offset + 2u > bytes.size()) {
                return fail(std::format("event {} duration payload is truncated", eventIndex));
            }
            const uint8_t ticks = bytes[offset++];
            if (ticks == 0) {
                return fail(std::format("event {} duration has invalid ticks", eventIndex));
            }
            const uint8_t flags = bytes[offset++];
            Duration duration{
                .ticks = ticks,
                .quantization = std::nullopt,
                .velocity = std::nullopt,
            };
            if ((flags & 0x01u) != 0) {
                if (offset >= bytes.size()) {
                    return fail(std::format("event {} duration quantization is truncated", eventIndex));
                }
                duration.quantization = bytes[offset++];
            }
            if ((flags & 0x02u) != 0) {
                if (offset >= bytes.size()) {
                    return fail(std::format("event {} duration velocity is truncated", eventIndex));
                }
                duration.velocity = bytes[offset++];
            }
            entry.event = NspcEvent{duration};
            break;
        }
        case PackedEventKind::Vcmd: {
            if (offset + 2u > bytes.size()) {
                return fail(std::format("event {} VCMD payload is truncated", eventIndex));
            }
            const uint8_t vcmdId = bytes[offset++];
            const uint8_t paramCount = bytes[offset++];
            if (paramCount > 4) {
                return fail(std::format("event {} VCMD paramCount is out of range", eventIndex));
            }
            if (offset + static_cast<size_t>(paramCount) >= bytes.size()) {
                return fail(std::format("event {} VCMD params are truncated", eventIndex));
            }

            json payload{
                {"id", vcmdId},
            };
            json params = json::array();
            for (uint8_t i = 0; i < paramCount; ++i) {
                params.push_back(bytes[offset++]);
            }
            payload["params"] = std::move(params);

            const uint8_t flags = bytes[offset++];
            if ((flags & 0x01u) != 0) {
                int64_t subroutineIdRaw = 0;
                if (!readVarInt(bytes, offset, subroutineIdRaw)) {
                    return fail(std::format("event {} VCMD subroutineId is truncated", eventIndex));
                }
                if (subroutineIdRaw < std::numeric_limits<int>::min() || subroutineIdRaw > std::numeric_limits<int>::max()) {
                    return fail(std::format("event {} VCMD subroutineId is out of range", eventIndex));
                }
                payload["subroutineId"] = static_cast<int>(subroutineIdRaw);
            }
            if ((flags & 0x02u) != 0) {
                uint16_t originalAddr = 0;
                if (!readU16Le(bytes, offset, originalAddr)) {
                    return fail(std::format("event {} VCMD originalAddr is truncated", eventIndex));
                }
                payload["originalAddr"] = originalAddr;
            }
            if ((flags & 0x04u) != 0) {
                payload["extension"] = true;
            }

            auto parsedVcmd = parseVcmd(payload);
            if (!parsedVcmd.has_value()) {
                return fail(std::format("event {} VCMD decode failed: {}", eventIndex, parsedVcmd.error()));
            }
            entry.event = NspcEvent{*parsedVcmd};
            break;
        }
        case PackedEventKind::Note:
            if (offset >= bytes.size()) {
                return fail(std::format("event {} note payload is truncated", eventIndex));
            }
            entry.event = NspcEvent{Note{.pitch = bytes[offset++]}};
            break;
        case PackedEventKind::Tie:
            entry.event = NspcEvent{Tie{}};
            break;
        case PackedEventKind::Rest:
            entry.event = NspcEvent{Rest{}};
            break;
        case PackedEventKind::Percussion:
            if (offset >= bytes.size()) {
                return fail(std::format("event {} percussion payload is truncated", eventIndex));
            }
            entry.event = NspcEvent{Percussion{.index = bytes[offset++]}};
            break;
        case PackedEventKind::Subroutine: {
            int64_t subroutineIdRaw = 0;
            if (!readVarInt(bytes, offset, subroutineIdRaw)) {
                return fail(std::format("event {} subroutine id is truncated", eventIndex));
            }
            if (subroutineIdRaw < std::numeric_limits<int>::min() || subroutineIdRaw > std::numeric_limits<int>::max()) {
                return fail(std::format("event {} subroutine id is out of range", eventIndex));
            }
            uint16_t originalAddr = 0;
            if (!readU16Le(bytes, offset, originalAddr)) {
                return fail(std::format("event {} subroutine originalAddr is truncated", eventIndex));
            }
            entry.event = NspcEvent{Subroutine{
                .id = static_cast<int>(subroutineIdRaw),
                .originalAddr = originalAddr,
            }};
            break;
        }
        case PackedEventKind::End:
            entry.event = NspcEvent{End{}};
            break;
        default:
            return fail(std::format("event {} has unknown event kind {}", eventIndex, static_cast<int>(kind)));
        }

        out.push_back(std::move(entry));
    }

    if (offset != bytes.size()) {
        return fail("payload has trailing bytes");
    }

    return out;
}

void resolveLoadedEventId(NspcEventEntry& entry, NspcEventId& generatedId) {
    if (entry.id == 0) {
        entry.id = generatedId++;
        return;
    }
    generatedId = std::max(generatedId, entry.id + 1);
}

std::expected<json, std::string> serializeSong(const NspcSong& song) {
    json out{
        {"songId", song.songId()},
        {"contentOrigin", contentOriginToString(song.contentOrigin())},
    };
    if (!song.songName().empty()) {
        out["songName"] = song.songName();
    }
    if (!song.author().empty()) {
        out["author"] = song.author();
    }

    json sequence = json::array();
    for (const auto& op : song.sequence()) {
        sequence.push_back(serializeSequenceOp(op));
    }
    out["sequence"] = std::move(sequence);

    json patterns = json::array();
    for (const auto& pattern : song.patterns()) {
        json patternJson{
            {"id", pattern.id},
            {"trackTableAddr", pattern.trackTableAddr},
        };
        if (pattern.channelTrackIds.has_value()) {
            json ids = json::array();
            for (const int trackId : *pattern.channelTrackIds) {
                ids.push_back(trackId);
            }
            patternJson["channelTrackIds"] = std::move(ids);
        } else {
            patternJson["channelTrackIds"] = nullptr;
        }
        patterns.push_back(std::move(patternJson));
    }
    out["patterns"] = std::move(patterns);

    json tracks = json::array();
    for (const auto& track : song.tracks()) {
        json trackJson{
            {"id", track.id},
            {"originalAddr", track.originalAddr},
        };
        auto packedEvents = packEventEntries(track.events);
        if (!packedEvents.has_value()) {
            return std::unexpected(std::format("Failed to encode track {} events: {}", track.id, packedEvents.error()));
        }
        trackJson["eventsEncoding"] = kPackedEventsEncoding;
        trackJson["eventsData"] = encodeBase64(*packedEvents);
        tracks.push_back(std::move(trackJson));
    }
    out["tracks"] = std::move(tracks);

    json subroutines = json::array();
    for (const auto& subroutine : song.subroutines()) {
        json subJson{
            {"id", subroutine.id},
            {"originalAddr", subroutine.originalAddr},
        };
        auto packedEvents = packEventEntries(subroutine.events);
        if (!packedEvents.has_value()) {
            return std::unexpected(
                std::format("Failed to encode subroutine {} events: {}", subroutine.id, packedEvents.error()));
        }
        subJson["eventsEncoding"] = kPackedEventsEncoding;
        subJson["eventsData"] = encodeBase64(*packedEvents);
        subroutines.push_back(std::move(subJson));
    }
    out["subroutines"] = std::move(subroutines);

    return std::move(out);
}

std::expected<NspcSong, std::string> parseSong(const json& value) {
    if (!value.is_object()) {
        return std::unexpected("Song entry must be an object");
    }
    const int songId = value.value("songId", -1);
    if (songId < 0) {
        return std::unexpected("Song entry is missing a valid songId");
    }

    NspcSong song = NspcSong::createEmpty(songId);
    song.setSongId(songId);
    song.setContentOrigin(parseContentOrigin(value.value("contentOrigin", "user")));
    song.setSongName(value.value("songName", ""));
    song.setAuthor(value.value("author", ""));

    auto parseSequenceSection = [&](std::vector<NspcSequenceOp>& out) -> std::expected<void, std::string> {
        out.clear();
        if (!value.contains("sequence")) {
            return {};
        }
        if (!value["sequence"].is_array()) {
            return std::unexpected("Song sequence must be an array");
        }
        for (const auto& opValue : value["sequence"]) {
            auto parsedOp = parseSequenceOp(opValue);
            if (!parsedOp.has_value()) {
                return std::unexpected(parsedOp.error());
            }
            out.push_back(*parsedOp);
        }
        return {};
    };

    auto parsePatternSection = [&](std::vector<NspcPattern>& out) -> std::expected<void, std::string> {
        out.clear();
        if (!value.contains("patterns")) {
            return {};
        }
        if (!value["patterns"].is_array()) {
            return std::unexpected("Song patterns must be an array");
        }
        for (const auto& patternValue : value["patterns"]) {
            if (!patternValue.is_object()) {
                return std::unexpected("Pattern entry must be an object");
            }

            NspcPattern pattern{};
            pattern.id = patternValue.value("id", -1);
            if (pattern.id < 0) {
                return std::unexpected("Pattern entry has invalid id");
            }
            if (const auto addr = parseU16(patternValue.value("trackTableAddr", 0)); addr.has_value()) {
                pattern.trackTableAddr = *addr;
            } else {
                return std::unexpected("Pattern entry has invalid trackTableAddr");
            }

            if (patternValue.contains("channelTrackIds") && !patternValue["channelTrackIds"].is_null()) {
                if (!patternValue["channelTrackIds"].is_array() || patternValue["channelTrackIds"].size() != 8) {
                    return std::unexpected("Pattern channelTrackIds must be an array of 8 values");
                }
                std::array<int, 8> channelIds{};
                for (size_t i = 0; i < 8; ++i) {
                    channelIds[i] = patternValue["channelTrackIds"][i].get<int>();
                }
                pattern.channelTrackIds = channelIds;
            } else {
                pattern.channelTrackIds.reset();
            }

            out.push_back(std::move(pattern));
        }
        return {};
    };

    auto parseEventList = [&](const json& owner, std::string_view label,
                              std::vector<NspcEventEntry>& out,
                              NspcEventId& generatedEventId) -> std::expected<void, std::string> {
        out.clear();
        if (!owner.contains("eventsData")) {
            return std::unexpected(std::format("{} is missing eventsData payload", label));
        }

        if (!owner["eventsData"].is_string()) {
            return std::unexpected(std::format("{} eventsData must be a base64 string", label));
        }

        const std::string encoding = owner.value("eventsEncoding", "");
        if (encoding != kPackedEventsEncoding) {
            return std::unexpected(std::format("{} eventsEncoding must be '{}'", label, kPackedEventsEncoding));
        }

        const std::string encoded = owner["eventsData"].get<std::string>();
        auto decoded = decodeBase64(encoded);
        if (!decoded.has_value()) {
            return std::unexpected(std::format("{} eventsData has invalid base64 payload: {}", label, decoded.error()));
        }
        auto unpacked = unpackEventEntries(*decoded, label);
        if (!unpacked.has_value()) {
            return std::unexpected(unpacked.error());
        }
        out = std::move(*unpacked);
        for (auto& entry : out) {
            resolveLoadedEventId(entry, generatedEventId);
        }
        return {};
    };

    auto parseTrackSection = [&](std::vector<NspcTrack>& out, NspcEventId& generatedEventId) -> std::expected<void, std::string> {
        out.clear();
        if (!value.contains("tracks")) {
            return {};
        }
        if (!value["tracks"].is_array()) {
            return std::unexpected("Song tracks must be an array");
        }
        for (const auto& trackValue : value["tracks"]) {
            if (!trackValue.is_object()) {
                return std::unexpected("Track entry must be an object");
            }

            NspcTrack track{};
            track.id = trackValue.value("id", -1);
            if (track.id < 0) {
                return std::unexpected("Track entry has invalid id");
            }
            if (const auto addr = parseU16(trackValue.value("originalAddr", 0)); addr.has_value()) {
                track.originalAddr = *addr;
            } else {
                return std::unexpected("Track entry has invalid originalAddr");
            }

            auto parsedEvents = parseEventList(trackValue, "Track", track.events, generatedEventId);
            if (!parsedEvents.has_value()) {
                return std::unexpected(parsedEvents.error());
            }
            out.push_back(std::move(track));
        }
        return {};
    };

    auto parseSubroutineSection = [&](std::vector<NspcSubroutine>& out,
                                      NspcEventId& generatedEventId) -> std::expected<void, std::string> {
        out.clear();
        if (!value.contains("subroutines")) {
            return {};
        }
        if (!value["subroutines"].is_array()) {
            return std::unexpected("Song subroutines must be an array");
        }
        for (const auto& subValue : value["subroutines"]) {
            if (!subValue.is_object()) {
                return std::unexpected("Subroutine entry must be an object");
            }

            NspcSubroutine subroutine{};
            subroutine.id = subValue.value("id", -1);
            if (subroutine.id < 0) {
                return std::unexpected("Subroutine entry has invalid id");
            }
            if (const auto addr = parseU16(subValue.value("originalAddr", 0)); addr.has_value()) {
                subroutine.originalAddr = *addr;
            } else {
                return std::unexpected("Subroutine entry has invalid originalAddr");
            }

            auto parsedEvents = parseEventList(subValue, "Subroutine", subroutine.events, generatedEventId);
            if (!parsedEvents.has_value()) {
                return std::unexpected(parsedEvents.error());
            }
            out.push_back(std::move(subroutine));
        }
        return {};
    };

    if (auto parsed = parseSequenceSection(song.sequence()); !parsed.has_value()) {
        return std::unexpected(parsed.error());
    }
    if (auto parsed = parsePatternSection(song.patterns()); !parsed.has_value()) {
        return std::unexpected(parsed.error());
    }

    NspcEventId generatedEventId = 1;
    if (auto parsed = parseTrackSection(song.tracks(), generatedEventId); !parsed.has_value()) {
        return std::unexpected(parsed.error());
    }
    if (auto parsed = parseSubroutineSection(song.subroutines(), generatedEventId); !parsed.has_value()) {
        return std::unexpected(parsed.error());
    }

    return song;
}

json serializeInstrument(const NspcInstrument& instrument) {
    return json{
        {"id", instrument.id},
        {"name", instrument.name},
        {"sampleIndex", instrument.sampleIndex},
        {"adsr1", instrument.adsr1},
        {"adsr2", instrument.adsr2},
        {"gain", instrument.gain},
        {"basePitchMult", instrument.basePitchMult},
        {"fracPitchMult", instrument.fracPitchMult},
        {"percussionNote", instrument.percussionNote},
        {"originalAddr", instrument.originalAddr},
        {"contentOrigin", contentOriginToString(instrument.contentOrigin)},
    };
}

std::expected<NspcInstrument, std::string> parseInstrument(const json& value) {
    if (!value.is_object()) {
        return std::unexpected("Instrument entry must be an object");
    }
    NspcInstrument instrument{};
    instrument.id = value.value("id", -1);
    if (instrument.id < 0) {
        return std::unexpected("Instrument entry has invalid id");
    }
    instrument.name = value.value("name", "");
    auto parseRequiredByte = [&](const char* fieldName, uint8_t& out) -> bool {
        const auto parsed = parseU8(value.value(fieldName, 0));
        if (!parsed.has_value()) {
            return false;
        }
        out = *parsed;
        return true;
    };
    if (!parseRequiredByte("sampleIndex", instrument.sampleIndex) || !parseRequiredByte("adsr1", instrument.adsr1) ||
        !parseRequiredByte("adsr2", instrument.adsr2) || !parseRequiredByte("gain", instrument.gain) ||
        !parseRequiredByte("basePitchMult", instrument.basePitchMult) ||
        !parseRequiredByte("fracPitchMult", instrument.fracPitchMult)) {
        return std::unexpected("Instrument entry has invalid byte fields");
    }
    if (const auto percussionNote = parseU8(value.value("percussionNote", 0)); percussionNote.has_value()) {
        instrument.percussionNote = *percussionNote;
    } else {
        return std::unexpected("Instrument entry has invalid percussionNote");
    }
    if (const auto addr = parseU16(value.value("originalAddr", 0)); addr.has_value()) {
        instrument.originalAddr = *addr;
    } else {
        return std::unexpected("Instrument entry has invalid originalAddr");
    }
    instrument.contentOrigin = parseContentOrigin(value.value("contentOrigin", "user"));
    return instrument;
}

json serializeSample(const BrrSample& sample) {
    return json{
        {"id", sample.id},
        {"name", sample.name},
        {"dataEncoding", "base64"},
        {"data", encodeBase64(sample.data)},
        {"dataSize", sample.data.size()},
        {"originalAddr", sample.originalAddr},
        {"originalLoopAddr", sample.originalLoopAddr},
        {"contentOrigin", contentOriginToString(sample.contentOrigin)},
    };
}

std::expected<BrrSample, std::string> parseSample(const json& value) {
    if (!value.is_object()) {
        return std::unexpected("Sample entry must be an object");
    }
    BrrSample sample{};
    sample.id = value.value("id", -1);
    if (sample.id < 0) {
        return std::unexpected("Sample entry has invalid id");
    }
    sample.name = value.value("name", "");
    if (const auto addr = parseU16(value.value("originalAddr", 0)); addr.has_value()) {
        sample.originalAddr = *addr;
    } else {
        return std::unexpected("Sample entry has invalid originalAddr");
    }
    if (const auto addr = parseU16(value.value("originalLoopAddr", 0)); addr.has_value()) {
        sample.originalLoopAddr = *addr;
    } else {
        return std::unexpected("Sample entry has invalid originalLoopAddr");
    }
    sample.contentOrigin = parseContentOrigin(value.value("contentOrigin", "user"));

    const std::string dataEncoding = value.value("dataEncoding", "");
    if (dataEncoding != "base64") {
        return std::unexpected(std::format("Sample entry has unsupported dataEncoding '{}'", dataEncoding));
    }
    if (!value.contains("data") || !value["data"].is_string()) {
        return std::unexpected("Sample entry has invalid data payload; expected base64 string");
    }
    const std::string encoded = value["data"].get<std::string>();
    auto decoded = decodeBase64(encoded);
    if (!decoded.has_value()) {
        return std::unexpected(std::format("Sample entry has invalid base64 data: {}", decoded.error()));
    }
    sample.data = std::move(*decoded);
    return sample;
}

}  // namespace

std::expected<void, std::string> saveProjectIrFile(const NspcProject& project, const std::filesystem::path& path,
                                                   std::optional<std::filesystem::path> baseSpcPath) {
    json root{
        {"format", kProjectFormatTag},
        {"version", kProjectFormatVersion},
        {"engine", project.engineConfig().name},
    };

    if (baseSpcPath.has_value() && !baseSpcPath->empty()) {
        std::filesystem::path storedPath = *baseSpcPath;
        if (storedPath.is_absolute()) {
            std::error_code relError;
            const auto relativePath = std::filesystem::relative(storedPath, path.parent_path(), relError);
            if (!relError && !relativePath.empty()) {
                storedPath = relativePath;
            }
        }
        root["baseSpcPath"] = storedPath.generic_string();
    }
    if (!project.engineConfig().extensions.empty()) {
        root["engineExtensions"] = project.enabledEngineExtensionNames();
    }

    json songs = json::array();
    std::vector<int> retainedEngineSongIds;
    retainedEngineSongIds.reserve(project.songs().size());
    for (const auto& song : project.songs()) {
        if (song.isEngineProvided()) {
            retainedEngineSongIds.push_back(song.songId());
        }
        const bool hasSongMetadata = !song.songName().empty() || !song.author().empty();
        if (!song.isUserProvided() && !hasSongMetadata) {
            continue;
        }
        auto serializedSong = serializeSong(song);
        if (!serializedSong.has_value()) {
            return std::unexpected(serializedSong.error());
        }
        songs.push_back(std::move(*serializedSong));
    }
    root["songs"] = std::move(songs);

    json instruments = json::array();
    std::vector<int> retainedEngineInstrumentIds;
    retainedEngineInstrumentIds.reserve(project.instruments().size());
    for (const auto& instrument : project.instruments()) {
        if (instrument.contentOrigin == NspcContentOrigin::EngineProvided) {
            retainedEngineInstrumentIds.push_back(instrument.id);
        }
        if (instrument.contentOrigin != NspcContentOrigin::UserProvided) {
            continue;
        }
        instruments.push_back(serializeInstrument(instrument));
    }
    root["instruments"] = std::move(instruments);

    json samples = json::array();
    std::vector<int> retainedEngineSampleIds;
    retainedEngineSampleIds.reserve(project.samples().size());
    for (const auto& sample : project.samples()) {
        if (sample.contentOrigin == NspcContentOrigin::EngineProvided) {
            retainedEngineSampleIds.push_back(sample.id);
        }
        if (sample.contentOrigin != NspcContentOrigin::UserProvided) {
            continue;
        }
        samples.push_back(serializeSample(sample));
    }
    root["samples"] = std::move(samples);
    root["engineRetained"] = json{
        {"songs", normalizeIdList(std::move(retainedEngineSongIds))},
        {"instruments", normalizeIdList(std::move(retainedEngineInstrumentIds))},
        {"samples", normalizeIdList(std::move(retainedEngineSampleIds))},
    };

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected(std::format("Failed to open '{}' for writing", path.string()));
    }
    out << root.dump();
    if (!out.good()) {
        return std::unexpected(std::format("Failed while writing '{}'", path.string()));
    }
    return {};
}

std::expected<NspcProjectIrData, std::string> loadProjectIrFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::unexpected(std::format("Failed to open '{}'", path.string()));
    }

    json root;
    try {
        in >> root;
    } catch (const std::exception& ex) {
        return std::unexpected(std::format("Failed to parse project file '{}': {}", path.string(), ex.what()));
    }

    if (!root.is_object()) {
        return std::unexpected("Project file root must be an object");
    }
    const std::string format = root.value("format", "");
    if (format != kProjectFormatTag) {
        return std::unexpected(std::format("Unsupported project format '{}'", format));
    }
    const int version = root.value("version", 0);
    if (version != kProjectFormatVersion) {
        return std::unexpected(std::format("Unsupported project format version {} (expected {})", version, kProjectFormatVersion));
    }

    NspcProjectIrData overlay{};
    overlay.engineName = root.value("engine", "");
    if (root.contains("baseSpcPath") && root["baseSpcPath"].is_string()) {
        const std::string basePath = root["baseSpcPath"].get<std::string>();
        if (!basePath.empty()) {
            overlay.baseSpcPath = std::filesystem::path(basePath);
        }
    }
    if (root.contains("engineExtensions")) {
        if (!root["engineExtensions"].is_array()) {
            return std::unexpected("Project engineExtensions payload must be an array");
        }
        std::vector<std::string> enabledExtensions;
        enabledExtensions.reserve(root["engineExtensions"].size());
        for (const auto& entry : root["engineExtensions"]) {
            if (!entry.is_string()) {
                return std::unexpected("Project engineExtensions entries must be strings");
            }
            enabledExtensions.push_back(entry.get<std::string>());
        }
        overlay.enabledEngineExtensions = std::move(enabledExtensions);
    }

    if (root.contains("songs")) {
        if (!root["songs"].is_array()) {
            return std::unexpected("Project songs payload must be an array");
        }
        for (const auto& songValue : root["songs"]) {
            auto song = parseSong(songValue);
            if (!song.has_value()) {
                return std::unexpected(song.error());
            }
            overlay.songs.push_back(std::move(*song));
        }
    }

    if (root.contains("instruments")) {
        if (!root["instruments"].is_array()) {
            return std::unexpected("Project instruments payload must be an array");
        }
        for (const auto& instrumentValue : root["instruments"]) {
            auto instrument = parseInstrument(instrumentValue);
            if (!instrument.has_value()) {
                return std::unexpected(instrument.error());
            }
            overlay.instruments.push_back(std::move(*instrument));
        }
    }

    if (root.contains("samples")) {
        if (!root["samples"].is_array()) {
            return std::unexpected("Project samples payload must be an array");
        }
        for (const auto& sampleValue : root["samples"]) {
            auto sample = parseSample(sampleValue);
            if (!sample.has_value()) {
                return std::unexpected(sample.error());
            }
            overlay.samples.push_back(std::move(*sample));
        }
    }

    if (!root.contains("engineRetained")) {
        return std::unexpected("Project file is missing required engineRetained payload");
    }
    if (!root["engineRetained"].is_object()) {
        return std::unexpected("Project engineRetained payload must be an object");
    }

    const auto& retained = root["engineRetained"];
    if (!retained.contains("songs") || !retained.contains("instruments") || !retained.contains("samples")) {
        return std::unexpected("Project engineRetained payload must include songs, instruments, and samples arrays");
    }

    auto retainedSongIds = parseIdList(retained["songs"], "engineRetained.songs");
    if (!retainedSongIds.has_value()) {
        return std::unexpected(retainedSongIds.error());
    }
    auto retainedInstrumentIds = parseIdList(retained["instruments"], "engineRetained.instruments");
    if (!retainedInstrumentIds.has_value()) {
        return std::unexpected(retainedInstrumentIds.error());
    }
    auto retainedSampleIds = parseIdList(retained["samples"], "engineRetained.samples");
    if (!retainedSampleIds.has_value()) {
        return std::unexpected(retainedSampleIds.error());
    }
    overlay.retainedEngineSongIds = std::move(*retainedSongIds);
    overlay.retainedEngineInstrumentIds = std::move(*retainedInstrumentIds);
    overlay.retainedEngineSampleIds = std::move(*retainedSampleIds);

    return overlay;
}

std::expected<void, std::string> applyProjectIrOverlay(NspcProject& project, const NspcProjectIrData& overlay) {
    if (!overlay.engineName.empty() && overlay.engineName != project.engineConfig().name) {
        return std::unexpected(std::format("Project overlay engine '{}' does not match loaded base engine '{}'",
                                           overlay.engineName, project.engineConfig().name));
    }

    auto& songs = project.songs();
    if (overlay.enabledEngineExtensions.has_value()) {
        std::unordered_set<std::string> enabledExtensions(overlay.enabledEngineExtensions->begin(),
                                                          overlay.enabledEngineExtensions->end());
        for (const auto& extensionName : *overlay.enabledEngineExtensions) {
            if (findEngineExtension(project.engineConfig(), extensionName) == nullptr) {
                return std::unexpected(
                    std::format("Project overlay references unknown engine extension '{}'", extensionName));
            }
        }
        for (const auto& extension : project.engineConfig().extensions) {
            (void)project.setEngineExtensionEnabled(extension.name, enabledExtensions.contains(extension.name));
        }
    }

    std::vector<NspcSong> sortedSongs = overlay.songs;
    std::sort(sortedSongs.begin(), sortedSongs.end(),
              [](const NspcSong& lhs, const NspcSong& rhs) { return lhs.songId() < rhs.songId(); });

    for (const auto& incomingSong : sortedSongs) {
        const int songId = incomingSong.songId();
        if (songId < 0) {
            return std::unexpected("Project overlay contains a song with negative songId");
        }

        while (songs.size() <= static_cast<size_t>(songId)) {
            const auto addedIndex = project.addEmptySong();
            if (!addedIndex.has_value()) {
                return std::unexpected("Unable to expand song table while applying project overlay");
            }
            if (*addedIndex != static_cast<size_t>(songId)) {
                (void)project.setSongContentOrigin(*addedIndex, NspcContentOrigin::EngineProvided);
            }
        }

        NspcSong mergedSong = incomingSong;
        mergedSong.setSongId(songId);
        songs[static_cast<size_t>(songId)] = std::move(mergedSong);
    }

    auto& instruments = project.instruments();
    for (const auto& incomingInstrument : overlay.instruments) {
        auto it = std::find_if(instruments.begin(), instruments.end(), [&](const NspcInstrument& instrument) {
            return instrument.id == incomingInstrument.id;
        });
        if (it == instruments.end()) {
            instruments.push_back(incomingInstrument);
        } else {
            *it = incomingInstrument;
        }
    }
    std::sort(instruments.begin(), instruments.end(),
              [](const NspcInstrument& lhs, const NspcInstrument& rhs) { return lhs.id < rhs.id; });

    auto& samples = project.samples();
    for (const auto& incomingSample : overlay.samples) {
        auto it = std::find_if(samples.begin(), samples.end(),
                               [&](const BrrSample& sample) { return sample.id == incomingSample.id; });
        if (it == samples.end()) {
            samples.push_back(incomingSample);
        } else {
            *it = incomingSample;
        }
    }
    std::sort(samples.begin(), samples.end(), [](const BrrSample& lhs, const BrrSample& rhs) { return lhs.id < rhs.id; });

    for (const auto& incomingInstrument : overlay.instruments) {
        writeOverlayInstrumentToAram(project, incomingInstrument);
    }
    for (const auto& incomingSample : overlay.samples) {
        writeOverlaySampleToAram(project, incomingSample);
    }

    pruneEngineSongs(project, overlay.retainedEngineSongIds);
    pruneEngineInstruments(project, overlay.retainedEngineInstrumentIds);
    pruneEngineSamples(project, overlay.retainedEngineSampleIds);

    project.refreshAramUsage();
    return {};
}

}  // namespace ntrak::nspc
