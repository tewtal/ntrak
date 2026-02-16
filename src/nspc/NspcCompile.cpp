#include "ntrak/nspc/NspcCompileShared.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace ntrak::nspc {
namespace compile_detail {

void appendU8(std::vector<uint8_t>& out, uint8_t value) {
    out.push_back(value);
}

void appendU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

uint32_t sequenceOpSize(const NspcSequenceOp& op) {
    return std::visit(nspc::overloaded{
                          [](const PlayPattern&) { return 2u; },
                          [](const JumpTimes&) { return 4u; },
                          [](const AlwaysJump&) { return 4u; },
                          [](const FastForwardOn&) { return 2u; },
                          [](const FastForwardOff&) { return 2u; },
                          [](const EndSequence&) { return 2u; },
                      },
                      op);
}

bool isRelocatableSongRegion(const NspcAramRegion& region, int songId) {
    if (region.songId != songId) {
        return false;
    }

    switch (region.kind) {
    case NspcAramRegionKind::SequenceData:
    case NspcAramRegionKind::PatternTable:
    case NspcAramRegionKind::TrackData:
    case NspcAramRegionKind::SubroutineData:
        return true;
    default:
        return false;
    }
}

void addClampedRange(std::vector<AddressRange>& ranges, uint32_t from, uint32_t to) {
    from = std::min<uint32_t>(from, static_cast<uint32_t>(kAramSize));
    to = std::min<uint32_t>(to, static_cast<uint32_t>(kAramSize));
    if (to <= from) {
        return;
    }
    ranges.push_back(AddressRange{from, to});
}

void normalizeRanges(std::vector<AddressRange>& ranges) {
    if (ranges.empty()) {
        return;
    }

    std::sort(ranges.begin(), ranges.end(),
              [](const AddressRange& lhs, const AddressRange& rhs) { return lhs.from < rhs.from; });

    std::vector<AddressRange> merged;
    merged.reserve(ranges.size());
    merged.push_back(ranges.front());

    for (size_t i = 1; i < ranges.size(); ++i) {
        AddressRange& current = merged.back();
        const AddressRange& next = ranges[i];
        if (next.from <= current.to) {
            current.to = std::max(current.to, next.to);
            continue;
        }
        merged.push_back(next);
    }

    ranges = std::move(merged);
}

std::vector<AddressRange> invertRanges(const std::vector<AddressRange>& blockedRanges) {
    std::vector<AddressRange> freeRanges;
    uint32_t cursor = 0;
    for (const auto& blocked : blockedRanges) {
        if (blocked.from > cursor) {
            freeRanges.push_back(AddressRange{cursor, blocked.from});
        }
        cursor = std::max(cursor, blocked.to);
    }
    if (cursor < kAramSize) {
        freeRanges.push_back(AddressRange{cursor, static_cast<uint32_t>(kAramSize)});
    }
    return freeRanges;
}

uint32_t totalRangeBytes(const std::vector<AddressRange>& ranges) {
    uint32_t total = 0;
    for (const auto& range : ranges) {
        total += range.to - range.from;
    }
    return total;
}

void consumeAllocatedRange(std::vector<AddressRange>& freeRanges, uint32_t start, uint32_t size) {
    const uint32_t end = start + size;
    for (size_t i = 0; i < freeRanges.size(); ++i) {
        auto& range = freeRanges[i];
        if (start < range.from || end > range.to) {
            continue;
        }

        if (start == range.from && end == range.to) {
            freeRanges.erase(freeRanges.begin() + static_cast<ptrdiff_t>(i));
            return;
        }
        if (start == range.from) {
            range.from = end;
            return;
        }
        if (end == range.to) {
            range.to = start;
            return;
        }

        const AddressRange tail{end, range.to};
        range.to = start;
        freeRanges.insert(freeRanges.begin() + static_cast<ptrdiff_t>(i + 1), tail);
        return;
    }
}

std::optional<uint16_t> allocateFromFreeRanges(std::vector<AddressRange>& freeRanges, uint32_t size,
                                               std::optional<uint16_t> preferredAddr) {
    if (size == 0 || size > kAramSize) {
        return std::nullopt;
    }

    auto try_allocate_at = [&](uint32_t start) -> std::optional<uint16_t> {
        const uint32_t end = start + size;
        if (end > kAramSize) {
            return std::nullopt;
        }

        for (const auto& range : freeRanges) {
            if (start >= range.from && end <= range.to) {
                consumeAllocatedRange(freeRanges, start, size);
                return static_cast<uint16_t>(start);
            }
        }
        return std::nullopt;
    };

    if (preferredAddr.has_value()) {
        if (const auto preferred = try_allocate_at(*preferredAddr); preferred.has_value()) {
            return preferred;
        }
    }

    for (const auto& range : freeRanges) {
        if (range.to - range.from >= size) {
            if (const auto allocated = try_allocate_at(range.from); allocated.has_value()) {
                return allocated;
            }
        }
    }

    return std::nullopt;
}

std::optional<uint16_t> readSongSequencePointer(emulation::AramView aram, const NspcEngineConfig& engine,
                                                size_t songIndex) {
    if (engine.songIndexPointers == 0) {
        return std::nullopt;
    }

    const uint32_t entryAddr = static_cast<uint32_t>(engine.songIndexPointers) + static_cast<uint32_t>(songIndex) * 2u;
    if (entryAddr + 1u >= kAramSize) {
        return std::nullopt;
    }
    return aram.read16(static_cast<uint16_t>(entryAddr));
}

const NspcCommandMap& commandMapForEngine(const NspcEngineConfig& engine) {
    static const NspcCommandMap kDefaultMap{};
    if (!engine.commandMap.has_value()) {
        return kDefaultMap;
    }
    return *engine.commandMap;
}

std::expected<uint8_t, std::string> mapCommonVcmdIdToEngine(uint8_t commonId, const NspcEngineConfig& engine) {
    const auto& map = commandMapForEngine(engine);
    if (const auto it = map.writeVcmdMap.find(commonId); it != map.writeVcmdMap.end()) {
        return it->second;
    }
    if (engine.commandMap.has_value() && map.strictWriteVcmdMap) {
        return std::unexpected(std::format("VCMD ${:02X} is not mapped for engine '{}'", commonId,
                                           engine.name.empty() ? "unknown" : engine.name));
    }
    return commonId;
}

std::expected<void, std::string> encodeVcmd(const Vcmd& cmd, std::vector<uint8_t>& out,
                                            const std::unordered_map<int, uint16_t>& subroutineAddrById,
                                            std::vector<std::string>& warnings, const NspcEngineConfig& engine) {
    std::optional<std::string> encodeError;
    auto encode = [&](uint8_t id) {
        if (encodeError.has_value()) {
            return;
        }
        auto mapped = mapCommonVcmdIdToEngine(id, engine);
        if (!mapped.has_value()) {
            encodeError = mapped.error();
            return;
        }
        appendU8(out, *mapped);
    };
    auto encodeValueId = [&](const auto& value) {
        encode(value.id);
    };

    std::visit(nspc::overloaded{
                   [&](const std::monostate&) { warnings.push_back("Encountered empty VCMD; skipped"); },
                   [&](const VcmdInst& value) {
                       encodeValueId(value);
                       appendU8(out, value.instrumentIndex);
                   },
                   [&](const VcmdPanning& value) {
                       encodeValueId(value);
                       appendU8(out, value.panning);
                   },
                   [&](const VcmdPanFade& value) {
                       encodeValueId(value);
                       appendU8(out, value.time);
                       appendU8(out, value.target);
                   },
                   [&](const VcmdVibratoOn& value) {
                       encodeValueId(value);
                       appendU8(out, value.delay);
                       appendU8(out, value.rate);
                       appendU8(out, value.depth);
                   },
                   [&](const VcmdVibratoOff& value) { encodeValueId(value); },
                   [&](const VcmdGlobalVolume& value) {
                       encodeValueId(value);
                       appendU8(out, value.volume);
                   },
                   [&](const VcmdGlobalVolumeFade& value) {
                       encodeValueId(value);
                       appendU8(out, value.time);
                       appendU8(out, value.target);
                   },
                   [&](const VcmdTempo& value) {
                       encodeValueId(value);
                       appendU8(out, static_cast<uint8_t>(value.tempo & 0xFF));
                   },
                   [&](const VcmdTempoFade& value) {
                       encodeValueId(value);
                       appendU8(out, value.time);
                       appendU8(out, static_cast<uint8_t>(value.target & 0xFF));
                   },
                   [&](const VcmdGlobalTranspose& value) {
                       encodeValueId(value);
                       appendU8(out, static_cast<uint8_t>(value.semitones));
                   },
                   [&](const VcmdPerVoiceTranspose& value) {
                       encodeValueId(value);
                       appendU8(out, static_cast<uint8_t>(value.semitones));
                   },
                   [&](const VcmdTremoloOn& value) {
                       encodeValueId(value);
                       appendU8(out, value.delay);
                       appendU8(out, value.rate);
                       appendU8(out, value.depth);
                   },
                   [&](const VcmdTremoloOff& value) { encodeValueId(value); },
                   [&](const VcmdVolume& value) {
                       encodeValueId(value);
                       appendU8(out, value.volume);
                   },
                   [&](const VcmdVolumeFade& value) {
                       encodeValueId(value);
                       appendU8(out, value.time);
                       appendU8(out, value.target);
                   },
                   [&](const VcmdSubroutineCall& value) {
                       encodeValueId(value);
                       uint16_t subroutineAddr = value.originalAddr;
                       const auto it = subroutineAddrById.find(value.subroutineId);
                       if (it != subroutineAddrById.end()) {
                           subroutineAddr = it->second;
                       } else {
                           warnings.push_back(std::format("Subroutine id {} not found; using original address ${:04X}",
                                                          value.subroutineId, value.originalAddr));
                       }
                       appendU16(out, subroutineAddr);
                       appendU8(out, value.count);
                   },
                   [&](const VcmdVibratoFadeIn& value) {
                       encodeValueId(value);
                       appendU8(out, value.time);
                   },
                   [&](const VcmdPitchEnvelopeTo& value) {
                       encodeValueId(value);
                       appendU8(out, value.delay);
                       appendU8(out, value.length);
                       appendU8(out, value.semitone);
                   },
                   [&](const VcmdPitchEnvelopeFrom& value) {
                       encodeValueId(value);
                       appendU8(out, value.delay);
                       appendU8(out, value.length);
                       appendU8(out, value.semitone);
                   },
                   [&](const VcmdPitchEnvelopeOff& value) { encodeValueId(value); },
                   [&](const VcmdFineTune& value) {
                       encodeValueId(value);
                       appendU8(out, static_cast<uint8_t>(value.semitones));
                   },
                   [&](const VcmdEchoOn& value) {
                       encodeValueId(value);
                       appendU8(out, value.channels);
                       appendU8(out, value.left);
                       appendU8(out, value.right);
                   },
                   [&](const VcmdEchoOff& value) { encodeValueId(value); },
                   [&](const VcmdEchoParams& value) {
                       encodeValueId(value);
                       appendU8(out, value.delay);
                       appendU8(out, value.feedback);
                       appendU8(out, value.firIndex);
                   },
                   [&](const VcmdEchoVolumeFade& value) {
                       encodeValueId(value);
                       appendU8(out, value.time);
                       appendU8(out, value.leftTarget);
                       appendU8(out, value.rightTarget);
                   },
                   [&](const VcmdPitchSlideToNote& value) {
                       encodeValueId(value);
                       appendU8(out, value.delay);
                       appendU8(out, value.length);
                       appendU8(out, value.note);
                   },
                   [&](const VcmdPercussionBaseInstrument& value) {
                       encodeValueId(value);
                       appendU8(out, value.index);
                   },
                   [&](const VcmdNOP& value) {
                       encodeValueId(value);
                       appendU16(out, value.nopBytes);
                   },
                   [&](const VcmdMuteChannel& value) { encodeValueId(value); },
                   [&](const VcmdFastForwardOn& value) { encodeValueId(value); },
                   [&](const VcmdFastForwardOff& value) { encodeValueId(value); },
                   [&](const VcmdUnused& value) { encodeValueId(value); },
                   [&](const VcmdExtension& value) {
                       const auto extensionParamCount = extensionVcmdParamByteCount(engine, value.id, true);
                       if (!extensionParamCount.has_value()) {
                           encodeError = std::format("Extension VCMD ${:02X} is not enabled for engine '{}'", value.id,
                                                     engine.name.empty() ? "unknown" : engine.name);
                           return;
                       }
                       if (value.paramCount != *extensionParamCount) {
                           encodeError = std::format("Extension VCMD ${:02X} expected {} params, got {}", value.id,
                                                     *extensionParamCount, value.paramCount);
                           return;
                       }

                       encode(value.id);
                       for (uint8_t i = 0; i < value.paramCount; ++i) {
                           appendU8(out, value.params[i]);
                       }
                   },
               },
               cmd.vcmd);

    if (encodeError.has_value()) {
        return std::unexpected(*encodeError);
    }

    return {};
}

std::expected<std::vector<uint8_t>, std::string> encodeEventStream(
    const std::vector<NspcEventEntry>& events, const std::unordered_map<int, uint16_t>& subroutineAddrById,
    std::vector<std::string>& warnings, const NspcEngineConfig& engine) {
    std::vector<uint8_t> out;
    out.reserve(events.size() * 2);
    const auto& commandMap = commandMapForEngine(engine);
    const uint8_t noteMaxByRange = static_cast<uint8_t>(
        (commandMap.noteEnd >= commandMap.noteStart) ? (commandMap.noteEnd - commandMap.noteStart) : 0);
    const uint8_t percussionMaxByRange =
        static_cast<uint8_t>((commandMap.percussionEnd >= commandMap.percussionStart)
                                 ? (commandMap.percussionEnd - commandMap.percussionStart)
                                 : 0);

    for (const auto& entry : events) {
        const auto result = std::visit(
            nspc::overloaded{
                [&](const std::monostate&) -> std::expected<void, std::string> { return {}; },
                [&](const Duration& value) -> std::expected<void, std::string> {
                    uint8_t ticks = value.ticks;
                    if (ticks == 0) {
                        ticks = 1;
                        warnings.push_back("Duration tick of 0 encountered; clamped to 1");
                    }
                    appendU8(out, ticks);
                    if (value.quantization.has_value() || value.velocity.has_value()) {
                        const uint8_t quant = static_cast<uint8_t>(value.quantization.value_or(0) & 0x07);
                        const uint8_t vel = static_cast<uint8_t>(value.velocity.value_or(0) & 0x0F);
                        appendU8(out, static_cast<uint8_t>((quant << 4) | vel));
                    }
                    return {};
                },
                [&](const Vcmd& value) -> std::expected<void, std::string> {
                    return encodeVcmd(value, out, subroutineAddrById, warnings, engine);
                },
                [&](const Note& value) -> std::expected<void, std::string> {
                    uint8_t pitch = value.pitch;
                    if (pitch > 0x47) {
                        warnings.push_back(std::format("Note pitch {:02X} out of range; clamped to 47", pitch));
                        pitch = 0x47;
                    }
                    if (pitch > noteMaxByRange) {
                        warnings.push_back(std::format("Note pitch {:02X} exceeds engine note range; clamped to {:02X}",
                                                       pitch, noteMaxByRange));
                        pitch = noteMaxByRange;
                    }
                    appendU8(out, static_cast<uint8_t>(commandMap.noteStart + pitch));
                    return {};
                },
                [&](const Tie&) -> std::expected<void, std::string> {
                    appendU8(out, commandMap.tie);
                    return {};
                },
                [&](const Rest&) -> std::expected<void, std::string> {
                    appendU8(out, commandMap.restWrite);
                    return {};
                },
                [&](const Percussion& value) -> std::expected<void, std::string> {
                    uint8_t index = value.index;
                    if (index > 0x15) {
                        warnings.push_back(std::format("Percussion index {:02X} out of range; clamped to 15", index));
                        index = 0x15;
                    }
                    if (index > percussionMaxByRange) {
                        warnings.push_back(
                            std::format("Percussion index {:02X} exceeds engine range; clamped to {:02X}", index,
                                        percussionMaxByRange));
                        index = percussionMaxByRange;
                    }
                    appendU8(out, static_cast<uint8_t>(commandMap.percussionStart + index));
                    return {};
                },
                [&](const Subroutine& value) -> std::expected<void, std::string> {
                    warnings.push_back(
                        std::format("Standalone Subroutine event id {} at ${:04X} ignored during compile", value.id,
                                    value.originalAddr));
                    return {};
                },
                [&](const End&) -> std::expected<void, std::string> {
                    appendU8(out, 0x00);
                    return {};
                },
            },
            entry.event);

        if (!result.has_value()) {
            return std::unexpected(result.error());
        }
    }

    return out;
}

uint32_t vcmdEncodedSize(const Vcmd& value) {
    return std::visit(nspc::overloaded{
                          [](const std::monostate&) { return 0u; },
                          [](const VcmdInst&) { return 2u; },
                          [](const VcmdPanning&) { return 2u; },
                          [](const VcmdPanFade&) { return 3u; },
                          [](const VcmdVibratoOn&) { return 4u; },
                          [](const VcmdVibratoOff&) { return 1u; },
                          [](const VcmdGlobalVolume&) { return 2u; },
                          [](const VcmdGlobalVolumeFade&) { return 3u; },
                          [](const VcmdTempo&) { return 2u; },
                          [](const VcmdTempoFade&) { return 3u; },
                          [](const VcmdGlobalTranspose&) { return 2u; },
                          [](const VcmdPerVoiceTranspose&) { return 2u; },
                          [](const VcmdTremoloOn&) { return 4u; },
                          [](const VcmdTremoloOff&) { return 1u; },
                          [](const VcmdVolume&) { return 2u; },
                          [](const VcmdVolumeFade&) { return 3u; },
                          [](const VcmdSubroutineCall&) { return 4u; },
                          [](const VcmdVibratoFadeIn&) { return 2u; },
                          [](const VcmdPitchEnvelopeTo&) { return 4u; },
                          [](const VcmdPitchEnvelopeFrom&) { return 4u; },
                          [](const VcmdPitchEnvelopeOff&) { return 1u; },
                          [](const VcmdFineTune&) { return 2u; },
                          [](const VcmdEchoOn&) { return 4u; },
                          [](const VcmdEchoOff&) { return 1u; },
                          [](const VcmdEchoParams&) { return 4u; },
                          [](const VcmdEchoVolumeFade&) { return 4u; },
                          [](const VcmdPitchSlideToNote&) { return 4u; },
                          [](const VcmdPercussionBaseInstrument&) { return 2u; },
                          [](const VcmdNOP&) { return 3u; },
                          [](const VcmdMuteChannel&) { return 1u; },
                          [](const VcmdFastForwardOn&) { return 1u; },
                          [](const VcmdFastForwardOff&) { return 1u; },
                          [](const VcmdUnused&) { return 1u; },
                          [](const VcmdExtension& value) { return static_cast<uint32_t>(1u + value.paramCount); },
                      },
                      value.vcmd);
}

uint32_t eventEncodedSize(const NspcEventEntry& entry) {
    return std::visit(nspc::overloaded{
                          [](const std::monostate&) { return 0u; },
                          [](const Duration& value) {
                              return (value.quantization.has_value() || value.velocity.has_value()) ? 2u : 1u;
                          },
                          [](const Vcmd& value) { return vcmdEncodedSize(value); },
                          [](const Note&) { return 1u; },
                          [](const Tie&) { return 1u; },
                          [](const Rest&) { return 1u; },
                          [](const Percussion&) { return 1u; },
                          [](const Subroutine&) { return 0u; },
                          [](const End&) { return 1u; },
                      },
                      entry.event);
}

std::vector<uint8_t> buildSequencePointerMask(const std::vector<NspcSequenceOp>& sequence, size_t encodedSize) {
    std::vector<uint8_t> mask(encodedSize, 0);
    size_t offset = 0;

    for (const auto& op : sequence) {
        std::visit(nspc::overloaded{
                       [&](const PlayPattern&) {
                           if (offset + 1 < mask.size()) {
                               mask[offset] = 1;
                               mask[offset + 1] = 1;
                           }
                           offset += 2;
                       },
                       [&](const JumpTimes&) {
                           if (offset + 3 < mask.size()) {
                               mask[offset + 2] = 1;
                               mask[offset + 3] = 1;
                           }
                           offset += 4;
                       },
                       [&](const AlwaysJump&) {
                           if (offset + 3 < mask.size()) {
                               mask[offset + 2] = 1;
                               mask[offset + 3] = 1;
                           }
                           offset += 4;
                       },
                       [&](const FastForwardOn&) { offset += 2; },
                       [&](const FastForwardOff&) { offset += 2; },
                       [&](const EndSequence&) { offset += 2; },
                   },
                   op);
    }

    return mask;
}

std::vector<uint8_t> buildPatternPointerMask(size_t size) {
    return std::vector<uint8_t>(size, 1);
}

std::vector<uint8_t> buildStreamPointerMask(const std::vector<NspcEventEntry>& events, size_t encodedSize) {
    std::vector<uint8_t> mask(encodedSize, 0);
    size_t offset = 0;

    for (const auto& entry : events) {
        if (const auto* vcmd = std::get_if<Vcmd>(&entry.event)) {
            if (std::holds_alternative<VcmdSubroutineCall>(vcmd->vcmd)) {
                if (offset + 2 < mask.size()) {
                    mask[offset + 1] = 1;
                    mask[offset + 2] = 1;
                }
            }
        }
        offset += eventEncodedSize(entry);
    }

    return mask;
}

std::expected<std::vector<uint8_t>, std::string> readAramBytes(emulation::AramView aram, uint16_t address, size_t size,
                                                               std::string_view label) {
    if (size == 0) {
        return std::vector<uint8_t>{};
    }
    const uint32_t end = static_cast<uint32_t>(address) + static_cast<uint32_t>(size);
    if (end > kAramSize) {
        return std::unexpected(std::format("{} at ${:04X} with size {} exceeds ARAM bounds", label, address, size));
    }
    const auto span = aram.bytes(address, size);
    return std::vector<uint8_t>(span.begin(), span.end());
}

void compareBinaryObject(std::string_view label, std::span<const uint8_t> original, std::span<const uint8_t> rebuilt,
                         std::span<const uint8_t> pointerMask, NspcRoundTripReport& report) {
    report.objectsCompared++;

    if (original.size() != rebuilt.size()) {
        report.messages.push_back(
            std::format("{} size mismatch: original={} rebuilt={}", label, original.size(), rebuilt.size()));
    }

    const size_t commonSize = std::min(original.size(), rebuilt.size());
    report.bytesCompared += commonSize;

    constexpr size_t kMaxMessages = 64;
    for (size_t i = 0; i < commonSize; ++i) {
        if (original[i] == rebuilt[i]) {
            continue;
        }

        const bool pointerByte = i < pointerMask.size() && pointerMask[i] != 0;
        if (pointerByte) {
            report.pointerDifferencesIgnored++;
            continue;
        }

        report.differingBytes++;
        if (report.messages.size() < kMaxMessages) {
            report.messages.push_back(
                std::format("{} +{:04X}: {:02X} != {:02X}", label, static_cast<uint32_t>(i), original[i], rebuilt[i]));
        }
    }

    if (original.size() != rebuilt.size()) {
        report.differingBytes += (original.size() > rebuilt.size()) ? (original.size() - rebuilt.size())
                                                                    : (rebuilt.size() - original.size());
    }
}

std::vector<NspcUploadChunk> buildEnabledEngineExtensionPatchChunks(const NspcEngineConfig& engine) {
    std::vector<NspcUploadChunk> chunks;
    for (const auto& extension : engine.extensions) {
        if (!extension.enabled) {
            continue;
        }
        for (const auto& patch : extension.patches) {
            if (patch.bytes.empty()) {
                continue;
            }
            chunks.push_back(NspcUploadChunk{
                .address = patch.address,
                .bytes = patch.bytes,
                .label = std::format("Ext {} {}", extension.name, patch.name),
            });
        }
    }
    return chunks;
}

void sortUploadChunksByAddress(std::vector<NspcUploadChunk>& chunks, bool stableSort) {
    if (stableSort) {
        std::stable_sort(chunks.begin(), chunks.end(), [](const NspcUploadChunk& lhs, const NspcUploadChunk& rhs) {
            return lhs.address < rhs.address;
        });
    } else {
        std::sort(chunks.begin(), chunks.end(),
                  [](const NspcUploadChunk& lhs, const NspcUploadChunk& rhs) { return lhs.address < rhs.address; });
    }
}

std::expected<void, std::string> validateUploadChunkBoundsAndOverlap(const std::vector<NspcUploadChunk>& chunks,
                                                                     bool detailedOverlapMessage) {
    for (size_t i = 0; i < chunks.size(); ++i) {
        const auto& chunk = chunks[i];
        const uint32_t chunkEnd = static_cast<uint32_t>(chunk.address) + static_cast<uint32_t>(chunk.bytes.size());
        if (chunkEnd > kAramSize) {
            return std::unexpected(
                std::format("Upload chunk {} at ${:04X} exceeds ARAM bounds", chunk.label, chunk.address));
        }
        if (i == 0) {
            continue;
        }

        const auto& prev = chunks[i - 1];
        const uint32_t prevEnd = static_cast<uint32_t>(prev.address) + static_cast<uint32_t>(prev.bytes.size());
        if (chunk.address < prevEnd) {
            if (detailedOverlapMessage) {
                const uint16_t prevEndDisplay = prev.bytes.empty()
                                                    ? prev.address
                                                    : static_cast<uint16_t>(std::min<uint32_t>(prevEnd - 1u, 0xFFFFu));
                return std::unexpected(std::format("Upload chunks overlap: {} ends at ${:04X}, {} starts at ${:04X}",
                                                   prev.label, prevEndDisplay, chunk.label, chunk.address));
            }
            return std::unexpected(std::format("Upload chunks overlap: {} at ${:04X} and {} at ${:04X}", prev.label,
                                               prev.address, chunk.label, chunk.address));
        }
    }
    return {};
}

}  // namespace compile_detail

std::expected<std::vector<uint8_t>, std::string> encodeEventStreamForEngine(
    const std::vector<NspcEventEntry>& events, const std::unordered_map<int, uint16_t>& subroutineAddrById,
    std::vector<std::string>& warnings, const NspcEngineConfig& engine) {
    return compile_detail::encodeEventStream(events, subroutineAddrById, warnings, engine);
}

std::expected<std::vector<uint8_t>, std::string> buildUserContentNspcExport(NspcProject& project,
                                                                            NspcBuildOptions options) {
    auto upload = buildUserContentUpload(project, options);
    if (!upload.has_value()) {
        return std::unexpected(upload.error());
    }

    std::map<uint32_t, uint8_t> byteWrites;
    for (const auto& chunk : upload->chunks) {
        for (size_t i = 0; i < chunk.bytes.size(); ++i) {
            const uint32_t address = static_cast<uint32_t>(chunk.address) + static_cast<uint32_t>(i);
            byteWrites[address] = chunk.bytes[i];
        }
    }

    if (byteWrites.empty()) {
        return std::unexpected("No bytes generated for user-content export");
    }

    const size_t maxHeaderBytes = (byteWrites.size() * 4u) + 4u;
    std::vector<uint8_t> output;
    output.reserve(byteWrites.size() + maxHeaderBytes);

    auto it = byteWrites.begin();
    while (it != byteWrites.end()) {
        const uint32_t segmentStart = it->first;
        std::vector<uint8_t> segmentBytes;
        segmentBytes.reserve(64);
        segmentBytes.push_back(it->second);
        uint32_t expectedNextAddress = segmentStart + 1u;
        ++it;

        while (it != byteWrites.end() && it->first == expectedNextAddress && segmentBytes.size() < 0xFFFFu) {
            segmentBytes.push_back(it->second);
            ++expectedNextAddress;
            ++it;
        }

        compile_detail::appendU16(output, static_cast<uint16_t>(segmentBytes.size()));
        compile_detail::appendU16(output, static_cast<uint16_t>(segmentStart & 0xFFFFu));
        output.insert(output.end(), segmentBytes.begin(), segmentBytes.end());
    }

    compile_detail::appendU16(output, 0x0000);
    compile_detail::appendU16(output, project.engineConfig().entryPoint);
    return output;
}

std::expected<std::vector<uint8_t>, std::string> applyUploadToSpcImage(const NspcUploadList& upload,
                                                                       std::span<const uint8_t> baseSpcFile) {
    if (baseSpcFile.size() < compile_detail::kSpcHeaderSize + compile_detail::kAramSize) {
        return std::unexpected("Base SPC image is too small");
    }

    std::vector<uint8_t> output(baseSpcFile.begin(), baseSpcFile.end());
    for (const auto& chunk : upload.chunks) {
        const size_t offset = compile_detail::kSpcHeaderSize + chunk.address;
        if (offset + chunk.bytes.size() > compile_detail::kSpcHeaderSize + compile_detail::kAramSize ||
            offset + chunk.bytes.size() > output.size()) {
            return std::unexpected(
                std::format("Upload chunk {} at ${:04X} exceeds SPC image bounds", chunk.label, chunk.address));
        }

        std::copy(chunk.bytes.begin(), chunk.bytes.end(), output.begin() + static_cast<ptrdiff_t>(offset));
    }

    return output;
}

}  // namespace ntrak::nspc
