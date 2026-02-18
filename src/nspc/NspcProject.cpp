#include "ntrak/nspc/NspcProject.hpp"

#include "ntrak/common/Log.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <unordered_set>

namespace ntrak::nspc {
namespace {

constexpr uint32_t kAramSize = NspcAramUsage::kTotalAramBytes;
constexpr uint32_t kBrrBlockSize = 9;
constexpr uint32_t kMaxSampleDirectoryEntries = 64;
constexpr uint32_t kMaxInstruments = 64;
constexpr uint32_t kMaxBrrBlocksPerSample = 0x2000;
constexpr size_t kMaxSongEntries = 256;
constexpr uint32_t kSequenceProbeLimit = 128;
constexpr uint32_t kTrackProbeLimit = 16384;

void insertIfValid(std::unordered_set<uint16_t>& pointers, uint16_t pointer) {
    if (pointer != 0 && pointer != 0xFFFF) {
        pointers.insert(pointer);
    }
}

void collectSongPointers(const NspcSong& song, uint16_t sequencePointer, std::unordered_set<uint16_t>& pointers) {
    insertIfValid(pointers, sequencePointer);

    for (const auto& op : song.sequence()) {
        if (const auto* play = std::get_if<PlayPattern>(&op)) {
            insertIfValid(pointers, play->trackTableAddr);
            continue;
        }
        if (const auto* jump = std::get_if<JumpTimes>(&op)) {
            insertIfValid(pointers, jump->target.addr);
            continue;
        }
        if (const auto* jump = std::get_if<AlwaysJump>(&op)) {
            insertIfValid(pointers, jump->target.addr);
        }
    }

    for (const auto& pattern : song.patterns()) {
        insertIfValid(pointers, pattern.trackTableAddr);
    }
    for (const auto& track : song.tracks()) {
        insertIfValid(pointers, track.originalAddr);
    }
    for (const auto& subroutine : song.subroutines()) {
        insertIfValid(pointers, subroutine.originalAddr);
    }
}

uint32_t sequenceOpSize(const NspcSequenceOp& op) {
    return std::visit(overloaded{
                          [](const PlayPattern&) { return 2u; },
                          [](const JumpTimes&) { return 4u; },
                          [](const AlwaysJump&) { return 4u; },
                          [](const FastForwardOn&) { return 2u; },
                          [](const FastForwardOff&) { return 2u; },
                          [](const EndSequence&) { return 2u; },
                      },
                      op);
}

uint32_t vcmdSize(const Vcmd& cmd) {
    return std::visit(overloaded{
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
                      cmd.vcmd);
}

uint32_t eventSize(const NspcEventEntry& entry) {
    return std::visit(overloaded{
                          [](const std::monostate&) { return 0u; },
                          [](const Duration& value) {
                              return (value.quantization.has_value() || value.velocity.has_value()) ? 2u : 1u;
                          },
                          [](const Vcmd& value) { return vcmdSize(value); },
                          [](const Note&) { return 1u; },
                          [](const Tie&) { return 1u; },
                          [](const Rest&) { return 1u; },
                          [](const Percussion&) { return 1u; },
                          [](const Subroutine&) { return 0u; },  // Annotation-only entry in flattened view.
                          [](const End&) { return 1u; },
                      },
                      entry.event);
}

uint32_t streamSize(const std::vector<NspcEventEntry>& events) {
    uint32_t size = 0;
    for (const auto& entry : events) {
        size += eventSize(entry);
    }
    return std::max<uint32_t>(size, 1u);
}

void addUsageRegion(std::vector<NspcAramRegion>& regions, NspcAramRegionKind kind, uint32_t from, uint32_t to,
                    std::string label, int songId = -1, int objectId = -1) {
    from = std::min<uint32_t>(from, kAramSize);
    to = std::min<uint32_t>(to, kAramSize);
    if (to <= from) {
        return;
    }

    regions.push_back(NspcAramRegion{
        .kind = kind,
        .from = static_cast<uint16_t>(from),
        .to = static_cast<uint16_t>(to),
        .songId = songId,
        .objectId = objectId,
        .label = std::move(label),
    });
}

void paintRegion(std::array<NspcAramRegionKind, NspcAramUsage::kTotalAramBytes>& map, const NspcAramRegion& region) {
    const uint32_t from = region.from;
    const uint32_t to = region.to;
    for (uint32_t addr = from; addr < to; ++addr) {
        if (map[addr] == NspcAramRegionKind::Free) {
            map[addr] = region.kind;
        }
    }
}

uint8_t instrumentEntrySize(const NspcEngineConfig& engineConfig) {
    return std::clamp<uint8_t>(engineConfig.instrumentEntryBytes, 5, 6);
}

void collectStaticAramRegions(const NspcEngineConfig& engineConfig, std::span<const NspcSong> songs,
                              std::span<const NspcInstrument> instruments, std::span<const BrrSample> samples,
                              std::vector<NspcAramRegion>& regions) {
    for (const auto& region : engineConfig.reserved) {
        if (region.to <= region.from) {
            continue;
        }
        addUsageRegion(regions, NspcAramRegionKind::Reserved, region.from, region.to,
                       region.name.empty() ? "Reserved" : region.name);
    }

    if (regions.empty() && !engineConfig.engineBytes.empty()) {
        const uint32_t from = engineConfig.entryPoint;
        const uint32_t to = from + static_cast<uint32_t>(engineConfig.engineBytes.size());
        addUsageRegion(regions, NspcAramRegionKind::Reserved, from, to, "Engine");
    }

    if (engineConfig.echoBuffer != 0 && engineConfig.echoBufferLen > 0) {
        const uint32_t echoEnd = static_cast<uint32_t>(engineConfig.echoBuffer);
        const uint32_t echoSize = static_cast<uint32_t>(engineConfig.echoBufferLen);
        const uint32_t echoStart = (echoEnd > echoSize) ? (echoEnd - echoSize) : 0;
        addUsageRegion(regions, NspcAramRegionKind::Reserved, echoStart, echoEnd, "Echo buffer");
    }

    if (engineConfig.songIndexPointers != 0 && !songs.empty()) {
        const uint32_t from = engineConfig.songIndexPointers;
        const uint32_t to = from + static_cast<uint32_t>(songs.size()) * 2u;
        addUsageRegion(regions, NspcAramRegionKind::SongIndexTable, from, to, "Song index table");
    }

    const uint8_t entrySize = instrumentEntrySize(engineConfig);
    for (const auto& inst : instruments) {
        addUsageRegion(regions, NspcAramRegionKind::InstrumentTable, inst.originalAddr,
                       static_cast<uint32_t>(inst.originalAddr) + entrySize, std::format("Inst {:02X}", inst.id), -1,
                       inst.id);
    }

    if (engineConfig.sampleHeaders != 0) {
        for (const auto& sample : samples) {
            const uint32_t headerFrom = static_cast<uint32_t>(engineConfig.sampleHeaders) +
                                        static_cast<uint32_t>(sample.id) * 4u;
            addUsageRegion(regions, NspcAramRegionKind::SampleDirectory, headerFrom, headerFrom + 4u,
                           std::format("Sample {:02X} Header", sample.id), -1, sample.id);
        }
    }

    for (const auto& sample : samples) {
        if (sample.originalAddr == 0 || sample.data.empty()) {
            continue;
        }
        addUsageRegion(regions, NspcAramRegionKind::SampleData, sample.originalAddr,
                       static_cast<uint32_t>(sample.originalAddr) + static_cast<uint32_t>(sample.data.size()),
                       std::format("Sample {:02X} BRR", sample.id), -1, sample.id);
    }
}

uint16_t resolveLayoutAddress(const NspcSongAddressLayout* layout, int objectId, uint16_t fallback,
                              const std::unordered_map<int, uint16_t>& map) {
    if (layout == nullptr) {
        return fallback;
    }
    if (const auto it = map.find(objectId); it != map.end() && it->second != 0) {
        return it->second;
    }
    return fallback;
}

uint32_t resolveLayoutSize(const NspcSongAddressLayout* layout, int objectId, uint32_t fallback,
                           const std::unordered_map<int, uint32_t>& map) {
    if (layout == nullptr) {
        return fallback;
    }
    if (const auto it = map.find(objectId); it != map.end() && it->second > 0) {
        return it->second;
    }
    return fallback;
}

uint16_t resolveSequenceAddress(const NspcEngineConfig& engineConfig, emulation::AramView aramView, int songId,
                                const NspcSongAddressLayout* layout) {
    if (layout != nullptr && layout->sequenceAddr != 0) {
        return layout->sequenceAddr;
    }
    if (engineConfig.songIndexPointers == 0) {
        return 0;
    }
    const uint32_t pointerAddr = static_cast<uint32_t>(engineConfig.songIndexPointers) +
                                 static_cast<uint32_t>(songId) * 2u;
    if (pointerAddr + 1u >= kAramSize) {
        return 0;
    }
    return aramView.read16(static_cast<uint16_t>(pointerAddr));
}

void collectSongAramRegions(const NspcProject& project, emulation::AramView aramView,
                            std::vector<NspcAramRegion>& regions) {
    const auto& engineConfig = project.engineConfig();
    for (const auto& song : project.songs()) {
        const int songId = song.songId();
        const NspcSongAddressLayout* layout = project.songAddressLayout(songId);

        const uint16_t sequenceAddr = resolveSequenceAddress(engineConfig, aramView, songId, layout);
        if (sequenceAddr != 0 && sequenceAddr != 0xFFFF) {
            uint32_t seqSize = 0;
            for (const auto& op : song.sequence()) {
                seqSize += sequenceOpSize(op);
            }
            addUsageRegion(regions, NspcAramRegionKind::SequenceData, sequenceAddr,
                           static_cast<uint32_t>(sequenceAddr) + std::max<uint32_t>(seqSize, 1u),
                           std::format("Song {:02X} Sequence", songId), songId);
        }

        for (const auto& pattern : song.patterns()) {
            const uint16_t patternAddr = (layout != nullptr)
                                             ? resolveLayoutAddress(layout, pattern.id, pattern.trackTableAddr,
                                                                    layout->patternAddrById)
                                             : pattern.trackTableAddr;
            if (patternAddr == 0) {
                continue;
            }
            addUsageRegion(regions, NspcAramRegionKind::PatternTable, patternAddr,
                           static_cast<uint32_t>(patternAddr) + 16u,
                           std::format("Song {:02X} Pattern {:02X}", songId, pattern.id), songId, pattern.id);
        }

        for (const auto& track : song.tracks()) {
            const uint16_t trackAddr = (layout != nullptr) ? resolveLayoutAddress(layout, track.id, track.originalAddr,
                                                                                  layout->trackAddrById)
                                                           : track.originalAddr;
            if (trackAddr == 0) {
                continue;
            }
            const uint32_t size = (layout != nullptr) ? resolveLayoutSize(layout, track.id, streamSize(track.events),
                                                                          layout->trackSizeById)
                                                      : streamSize(track.events);
            addUsageRegion(regions, NspcAramRegionKind::TrackData, trackAddr, static_cast<uint32_t>(trackAddr) + size,
                           std::format("Song {:02X} Track {:02X}", songId, track.id), songId, track.id);
        }

        std::unordered_set<int> seenSubroutineIds;
        seenSubroutineIds.reserve(song.subroutines().size());
        for (const auto& subroutine : song.subroutines()) {
            const uint16_t subroutineAddr = (layout != nullptr)
                                                ? resolveLayoutAddress(layout, subroutine.id, subroutine.originalAddr,
                                                                       layout->subroutineAddrById)
                                                : subroutine.originalAddr;
            if (subroutineAddr == 0) {
                continue;
            }
            const uint32_t size = (layout != nullptr)
                                      ? resolveLayoutSize(layout, subroutine.id, streamSize(subroutine.events),
                                                          layout->subroutineSizeById)
                                      : streamSize(subroutine.events);
            seenSubroutineIds.insert(subroutine.id);
            addUsageRegion(regions, NspcAramRegionKind::SubroutineData, subroutineAddr,
                           static_cast<uint32_t>(subroutineAddr) + size,
                           std::format("Song {:02X} Sub {:02X}", songId, subroutine.id), songId, subroutine.id);
        }

        if (layout == nullptr) {
            continue;
        }
        for (const auto& [subroutineId, subroutineAddr] : layout->subroutineAddrById) {
            if (subroutineAddr == 0 || seenSubroutineIds.contains(subroutineId)) {
                continue;
            }
            const uint32_t size = resolveLayoutSize(layout, subroutineId, 1u, layout->subroutineSizeById);
            addUsageRegion(regions, NspcAramRegionKind::SubroutineData, subroutineAddr,
                           static_cast<uint32_t>(subroutineAddr) + size,
                           std::format("Song {:02X} Sub {:02X}", songId, subroutineId), songId, subroutineId);
        }
    }
}

void tallyAramOwnership(const std::array<NspcAramRegionKind, NspcAramUsage::kTotalAramBytes>& ownership,
                        NspcAramUsage& usage) {
    for (const auto kind : ownership) {
        switch (kind) {
        case NspcAramRegionKind::Free:
            usage.freeBytes++;
            break;
        case NspcAramRegionKind::Reserved:
            usage.reservedBytes++;
            break;
        case NspcAramRegionKind::SongIndexTable:
            usage.songIndexBytes++;
            break;
        case NspcAramRegionKind::InstrumentTable:
            usage.instrumentBytes++;
            break;
        case NspcAramRegionKind::SampleDirectory:
            usage.sampleDirectoryBytes++;
            break;
        case NspcAramRegionKind::SampleData:
            usage.sampleDataBytes++;
            break;
        case NspcAramRegionKind::SequenceData:
            usage.sequenceBytes++;
            break;
        case NspcAramRegionKind::PatternTable:
            usage.patternTableBytes++;
            break;
        case NspcAramRegionKind::TrackData:
            usage.trackBytes++;
            break;
        case NspcAramRegionKind::SubroutineData:
            usage.subroutineBytes++;
            break;
        }
    }
}

struct ParsedBrrData {
    std::vector<uint8_t> bytes;
    uint16_t endExclusive = 0;
};

std::optional<ParsedBrrData> parseBrrSample(emulation::AramView aram, uint16_t sampleStart, uint32_t maxEndExclusive,
                                            bool allowExtendedRange = false) {
    if (sampleStart == 0) {
        return std::nullopt;
    }
    maxEndExclusive = std::min<uint32_t>(maxEndExclusive, kAramSize);
    if (static_cast<uint32_t>(sampleStart) >= maxEndExclusive) {
        return std::nullopt;
    }

    ParsedBrrData parsed;
    parsed.bytes.reserve(9 * 8);

    uint32_t addr = sampleStart;
    for (uint32_t block = 0; block < kMaxBrrBlocksPerSample; ++block) {
        if (addr + kBrrBlockSize > maxEndExclusive) {
            return std::nullopt;
        }

        const uint8_t header = aram.read(static_cast<uint16_t>(addr));
        const uint8_t range = static_cast<uint8_t>(header >> 4);
        if (!allowExtendedRange && range > 0x0C) {
            // Invalid BRR range nibble; likely not actual BRR sample data.
            return std::nullopt;
        }

        for (uint32_t i = 0; i < kBrrBlockSize; ++i) {
            parsed.bytes.push_back(aram.read(static_cast<uint16_t>(addr + i)));
        }

        addr += kBrrBlockSize;
        if ((header & 0x01) != 0) {
            parsed.endExclusive = static_cast<uint16_t>(addr);
            return parsed;
        }
    }

    return std::nullopt;
}

void reindexSongsAndLayouts(std::vector<NspcSong>& songs,
                            std::unordered_map<int, NspcSongAddressLayout>& songAddressLayouts) {
    std::unordered_map<int, NspcSongAddressLayout> remappedLayouts;
    remappedLayouts.reserve(std::min(songAddressLayouts.size(), songs.size()));

    std::unordered_set<int> consumedLayoutIds;
    consumedLayoutIds.reserve(songAddressLayouts.size());

    for (size_t i = 0; i < songs.size(); ++i) {
        auto& song = songs[i];
        const int oldSongId = song.songId();
        const int newSongId = static_cast<int>(i);
        song.setSongId(newSongId);

        if (!consumedLayoutIds.insert(oldSongId).second) {
            continue;
        }

        const auto it = songAddressLayouts.find(oldSongId);
        if (it == songAddressLayouts.end()) {
            continue;
        }
        remappedLayouts.emplace(newSongId, std::move(it->second));
    }

    songAddressLayouts = std::move(remappedLayouts);
}

bool containsId(const std::vector<int>& ids, int id) {
    return std::binary_search(ids.begin(), ids.end(), id);
}

NspcContentOrigin defaultContentOrigin(int id, const std::vector<int>& defaultEngineProvidedIds,
                                       bool hasExplicitDefaults) {
    if (id < 0) {
        return NspcContentOrigin::UserProvided;
    }
    if (!hasExplicitDefaults) {
        return NspcContentOrigin::EngineProvided;
    }
    return containsId(defaultEngineProvidedIds, id) ? NspcContentOrigin::EngineProvided
                                                    : NspcContentOrigin::UserProvided;
}

bool readAramWordSafe(emulation::AramView aram, uint32_t address, uint16_t& outValue) {
    if (address + 1u >= kAramSize) {
        return false;
    }
    outValue = aram.read16(static_cast<uint16_t>(address));
    return true;
}

uint32_t computeInstrumentTableScanEnd(const NspcEngineConfig& config, uint8_t entrySize) {
    if (config.instrumentHeaders == 0) {
        return 0;
    }

    const uint32_t tableStart = config.instrumentHeaders;
    uint32_t scanEnd = std::min<uint32_t>(kAramSize, tableStart + static_cast<uint32_t>(kMaxInstruments) * entrySize);

    auto clampEnd = [&](uint16_t candidate) {
        const uint32_t c = candidate;
        if (c > tableStart && c < scanEnd) {
            scanEnd = c;
        }
    };

    clampEnd(config.songIndexPointers);
    clampEnd(config.sampleHeaders);
    clampEnd(config.percussionHeaders);
    for (const auto& region : config.reserved) {
        clampEnd(region.from);
    }

    return scanEnd;
}

bool isSmwV00Engine(const NspcEngineConfig& config) {
    return config.engineVersion == "0.0";
}

void applyPercussionTableNotes(std::vector<NspcInstrument>& instruments, emulation::AramView aramView,
                               const NspcEngineConfig& config) {
    if (!isSmwV00Engine(config) || config.percussionHeaders == 0) {
        return;
    }

    const auto percussionStartInstId = std::clamp((config.percussionHeaders - config.instrumentHeaders) / 5u, 0u,
                                                  kMaxInstruments);

    const auto commandMap = config.commandMap.value_or(NspcCommandMap{});
    const int percussionCount = static_cast<int>(commandMap.percussionEnd) -
                                static_cast<int>(commandMap.percussionStart) + 1;
    if (percussionCount <= 0) {
        return;
    }

    // Cap percussion entries so they don't overflow into custom instrument territory.
    const int maxPercEntries =
        config.customInstrumentStartIndex.has_value()
            ? std::min(percussionCount,
                       static_cast<int>(*config.customInstrumentStartIndex) - static_cast<int>(percussionStartInstId))
            : percussionCount;
    if (maxPercEntries <= 0) {
        return;
    }

    const uint8_t percEntrySize = std::clamp<uint8_t>(config.percussionEntryBytes, 6, 7);

    for (int i = 0; i < maxPercEntries; ++i) {
        const uint32_t entryAddr = static_cast<uint32_t>(config.percussionHeaders) +
                                   static_cast<uint32_t>(i) * percEntrySize;
        if (entryAddr + percEntrySize > kAramSize) {
            break;
        }

        const uint8_t sampleIndex = aramView.read(static_cast<uint16_t>(entryAddr + 0u));
        const uint8_t adsr1 = aramView.read(static_cast<uint16_t>(entryAddr + 1u));
        const uint8_t adsr2 = aramView.read(static_cast<uint16_t>(entryAddr + 2u));
        const uint8_t gain = aramView.read(static_cast<uint16_t>(entryAddr + 3u));
        const uint8_t basePitch = aramView.read(static_cast<uint16_t>(entryAddr + 4u));
        uint8_t fracPitch = 0;
        if (percEntrySize >= 7) {
            fracPitch = aramView.read(static_cast<uint16_t>(entryAddr + 5u));
        }
        const uint8_t note = aramView.read(static_cast<uint16_t>(entryAddr + percEntrySize - 1u));

        const bool allFF = (sampleIndex == 0xFF && adsr1 == 0xFF && adsr2 == 0xFF && gain == 0xFF &&
                            basePitch == 0xFF && note == 0xFF);
        const bool allZero = (sampleIndex == 0 && adsr1 == 0 && adsr2 == 0 && gain == 0 && basePitch == 0 && note == 0);
        if (allFF || allZero) {
            continue;
        }

        // Create a new instrument entry
        const int instId = percussionStartInstId + i;
        if (instId >= kMaxInstruments) {
            break;
        }

        NspcInstrument inst{};
        inst.id = instId;
        inst.sampleIndex = sampleIndex;
        inst.adsr1 = adsr1;
        inst.adsr2 = adsr2;
        inst.gain = gain;
        inst.basePitchMult = basePitch;
        inst.fracPitchMult = fracPitch;
        inst.percussionNote = note;
        inst.originalAddr = static_cast<uint16_t>(entryAddr);
        inst.contentOrigin = defaultContentOrigin(inst.id, config.defaultEngineProvidedInstrumentIds,
                                                  config.hasDefaultEngineProvidedInstruments);

        instruments.push_back(std::move(inst));
    }
}

std::unordered_set<int> collectReferencedSampleIdsFromInstrumentTable(emulation::AramView aram,
                                                                      const NspcEngineConfig& config) {
    std::unordered_set<int> referencedIds;
    if (config.instrumentHeaders == 0) {
        return referencedIds;
    }

    const uint8_t entrySize = std::clamp<uint8_t>(config.instrumentEntryBytes, 5, 6);
    const uint32_t scanEnd = computeInstrumentTableScanEnd(config, entrySize);
    uint32_t addr = config.instrumentHeaders;
    bool seenNonEmptyEntry = false;
    for (int instId = 0; instId < static_cast<int>(kMaxInstruments) && addr + entrySize <= scanEnd;
         ++instId, addr += entrySize) {
        const uint8_t sampleIndex = aram.read(static_cast<uint16_t>(addr));
        const uint8_t adsr1 = aram.read(static_cast<uint16_t>(addr + 1u));
        const uint8_t adsr2 = aram.read(static_cast<uint16_t>(addr + 2u));
        const uint8_t gain = aram.read(static_cast<uint16_t>(addr + 3u));
        const uint8_t basePitch = aram.read(static_cast<uint16_t>(addr + 4u));
        uint8_t fracPitch = 0;
        if (entrySize >= 6) {
            fracPitch = aram.read(static_cast<uint16_t>(addr + 5u));
        }

        // Treat 00/FF sample index as end-of-table once parsing has started.
        if (seenNonEmptyEntry && (sampleIndex == 0x00 || sampleIndex == 0xFF)) {
            break;
        }

        const bool allFF = (sampleIndex == 0xFF && adsr1 == 0xFF && adsr2 == 0xFF && gain == 0xFF &&
                            basePitch == 0xFF) &&
                           (entrySize < 6 || fracPitch == 0xFF);
        const bool allZero = (sampleIndex == 0 && adsr1 == 0 && adsr2 == 0 && gain == 0 && basePitch == 0) &&
                             (entrySize < 6 || fracPitch == 0);
        if (allFF || allZero) {
            continue;
        }

        seenNonEmptyEntry = true;
        referencedIds.insert(static_cast<int>(sampleIndex & 0x7F));
    }

    return referencedIds;
}

std::optional<uint8_t> mapRawVcmdId(const NspcCommandMap& map, uint8_t rawId) {
    if (rawId < map.vcmdStart) {
        return std::nullopt;
    }
    if (const auto it = map.readVcmdMap.find(rawId); it != map.readVcmdMap.end()) {
        return it->second;
    }
    if (map.strictReadVcmdMap && !map.readVcmdMap.empty()) {
        return std::nullopt;
    }
    return rawId;
}

bool isLikelyTrackLeadByte(uint8_t byte, const NspcCommandMap& commandMap, const NspcEngineConfig& engine) {
    if (byte == 0x00 || (byte >= 0x01 && byte <= 0x7F)) {
        return true;
    }
    if (byte >= commandMap.noteStart && byte <= commandMap.noteEnd) {
        return true;
    }
    if (byte == commandMap.tie) {
        return true;
    }
    if (byte >= commandMap.restStart && byte <= commandMap.restEnd) {
        return true;
    }
    if (byte >= commandMap.percussionStart && byte <= commandMap.percussionEnd) {
        return true;
    }
    if (byte < commandMap.vcmdStart) {
        return false;
    }
    const auto mappedVcmd = mapRawVcmdId(commandMap, byte);
    if (!mappedVcmd.has_value()) {
        return false;
    }
    if (extensionVcmdParamByteCount(engine, *mappedVcmd, true).has_value()) {
        return true;
    }
    return *mappedVcmd != VcmdUnused::id;
}

bool probeTrackStream(emulation::AramView aram, uint16_t trackAddr, const NspcCommandMap& commandMap,
                      const NspcEngineConfig& engine) {
    uint32_t addr = trackAddr;
    for (uint32_t steps = 0; steps < kTrackProbeLimit && addr < kAramSize; ++steps) {
        const uint8_t byte = aram.read(static_cast<uint16_t>(addr));

        std::printf("Probe step %u: Read byte %02X at address %04X\n", steps, byte, addr);

        if (byte == 0x00) {
            return true;
        }

        if (byte >= 0x01 && byte <= 0x7F) {
            ++addr;
            if (addr >= kAramSize) {
                return false;
            }
            const uint8_t maybeQv = aram.read(static_cast<uint16_t>(addr));
            if (maybeQv >= 0x01 && maybeQv <= 0x7F) {
                ++addr;
            }
            continue;
        }

        if ((byte >= commandMap.noteStart && byte <= commandMap.noteEnd) || byte == commandMap.tie ||
            (byte >= commandMap.restStart && byte <= commandMap.restEnd) ||
            (byte >= commandMap.percussionStart && byte <= commandMap.percussionEnd)) {
            ++addr;
            continue;
        }

        if (byte >= commandMap.vcmdStart) {
            const auto mappedVcmd = mapRawVcmdId(commandMap, byte);
            if (!mappedVcmd.has_value()) {
                return false;
            }

            uint32_t neededBytes = 0;
            if (const auto extensionParamCount = extensionVcmdParamByteCount(engine, *mappedVcmd, true);
                extensionParamCount.has_value()) {
                neededBytes = 1u + *extensionParamCount;
            } else {
                if (*mappedVcmd == VcmdUnused::id) {
                    return false;
                }
                neededBytes = 1u + vcmdParamByteCount(*mappedVcmd);
            }

            if (addr + neededBytes > kAramSize) {
                return false;
            }
            addr += neededBytes;
            continue;
        }

        return false;
    }

    return false;
}

bool findFirstTrackPointer(emulation::AramView aram, uint16_t sequencePtr, std::optional<uint16_t>& outTrackAddr) {
    uint32_t seqAddr = sequencePtr;
    for (uint32_t steps = 0; steps < kSequenceProbeLimit; ++steps) {
        uint16_t seqWord = 0;
        if (!readAramWordSafe(aram, seqAddr, seqWord)) {
            return false;
        }

        if (seqWord == 0x0000) {
            return false;
        }

        if ((seqWord & 0xFF00u) == 0x0000u) {
            const uint8_t lowByte = static_cast<uint8_t>(seqWord & 0x00FFu);
            if ((lowByte >= 0x01 && lowByte <= 0x7F) || lowByte >= 0x82) {
                seqAddr += 4u;
            } else {
                seqAddr += 2u;
            }
            continue;
        }

        const uint16_t patternAddr = seqWord;
        if (static_cast<uint32_t>(patternAddr) + 15u >= kAramSize) {
            return false;
        }
        for (uint32_t channel = 0; channel < 8; ++channel) {
            uint16_t trackAddr = 0;
            if (!readAramWordSafe(aram, static_cast<uint32_t>(patternAddr) + channel * 2u, trackAddr)) {
                return false;
            }
            if (trackAddr != 0) {
                outTrackAddr = trackAddr;
                return true;
            }
        }
        // Pattern exists but has no active tracks; this can still be a valid song.
        outTrackAddr.reset();
        return true;
    }

    return false;
}

bool isLikelySongPointer(emulation::AramView aram, uint16_t sequencePtr, const NspcCommandMap& commandMap,
                         const NspcEngineConfig& engine) {
    if (sequencePtr == 0 || sequencePtr == 0xFFFF) {
        return false;
    }
    if (static_cast<uint32_t>(sequencePtr) + 1u >= kAramSize) {
        return false;
    }

    std::optional<uint16_t> firstTrackAddr = std::nullopt;
    if (!findFirstTrackPointer(aram, sequencePtr, firstTrackAddr)) {
        return false;
    }
    if (!firstTrackAddr.has_value()) {
        return true;
    }
    if (static_cast<uint32_t>(*firstTrackAddr) >= kAramSize) {
        return false;
    }
    const uint8_t firstByte = aram.read(*firstTrackAddr);
    if (!isLikelyTrackLeadByte(firstByte, commandMap, engine)) {
        return false;
    }
    return probeTrackStream(aram, *firstTrackAddr, commandMap, engine);
}

}  // namespace

NspcProject::NspcProject(NspcEngineConfig config, std::array<std::uint8_t, 0x10000> aramData)
    : engineConfig_(std::move(config)), aram_(std::move(aramData)) {
    auto normalizeIds = [](std::vector<int>& ids) {
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    };
    normalizeIds(engineConfig_.defaultEngineProvidedSongIds);
    normalizeIds(engineConfig_.defaultEngineProvidedInstrumentIds);
    normalizeIds(engineConfig_.defaultEngineProvidedSampleIds);

    parseSamples();
    parseSongs();
    parseInstruments();
    rebuildAramUsage();
}

void NspcProject::parseInstruments() {
    if (engineConfig_.instrumentHeaders == 0) {
        return;
    }

    const uint8_t entrySize = std::clamp<uint8_t>(engineConfig_.instrumentEntryBytes, 5, 6);
    emulation::AramView aramView(aram_.data(), aram_.size());

    // Instrument table entries are engine-dependent (5 or 6 bytes):
    // Byte 0: Sample index (bit 7 = noise flag)
    // Byte 1: ADSR1
    // Byte 2: ADSR2
    // Byte 3: GAIN
    // Byte 4: Pitch multiplier (base)
    // Byte 5: Pitch multiplier (fractional, optional in 5-byte formats)

    const uint32_t scanEnd = computeInstrumentTableScanEnd(engineConfig_, entrySize);
    uint32_t addr = engineConfig_.instrumentHeaders;
    bool seenNonEmptyEntry = false;
    // If customInstrumentStartIndex is set, the global table only covers indices below that split
    // point — entries at and beyond it live in the per-song custom instrument header instead.
    const int globalTableLimit = engineConfig_.customInstrumentStartIndex.has_value()
                                     ? static_cast<int>(*engineConfig_.customInstrumentStartIndex)
                                     : static_cast<int>(kMaxInstruments);
    // Parse full table (up to engine max). Some SPCs contain sparse entries with zero-filled holes.
    for (int instId = 0; instId < globalTableLimit && addr + entrySize <= scanEnd;
         ++instId, addr += entrySize) {
        uint8_t sampleIndex = aramView.read(static_cast<uint16_t>(addr));
        uint8_t adsr1 = aramView.read(static_cast<uint16_t>(addr + 1u));
        uint8_t adsr2 = aramView.read(static_cast<uint16_t>(addr + 2u));
        uint8_t gain = aramView.read(static_cast<uint16_t>(addr + 3u));
        uint8_t basePitch = aramView.read(static_cast<uint16_t>(addr + 4u));
        uint8_t fracPitch = 0;
        if (entrySize >= 6) {
            fracPitch = aramView.read(static_cast<uint16_t>(addr + 5u));
        }

        // Treat 00/FF sample index as end-of-table once parsing has started.
        if (seenNonEmptyEntry && (sampleIndex == 0x00 || sampleIndex == 0xFF)) {
            break;
        }

        const uint8_t sampleId = sampleIndex & 0x7F;
        const bool sampleExists = std::any_of(samples_.begin(), samples_.end(),
                                              [sampleId](const BrrSample& sample) { return sample.id == sampleId; });

        const bool allFF = (sampleIndex == 0xFF && adsr1 == 0xFF && adsr2 == 0xFF && gain == 0xFF &&
                            basePitch == 0xFF) &&
                           (entrySize < 6 || fracPitch == 0xFF);
        const bool allZero = (sampleIndex == 0 && adsr1 == 0 && adsr2 == 0 && gain == 0 && basePitch == 0) &&
                             (entrySize < 6 || fracPitch == 0);

        if (allFF || allZero) {
            continue;
        }

        seenNonEmptyEntry = true;
        const bool validInstrument = sampleExists;

        if (validInstrument) {
            NspcInstrument inst{};
            inst.id = instId;
            inst.sampleIndex = sampleIndex & 0x7F;  // Lower 7 bits are sample index
            inst.adsr1 = adsr1;
            inst.adsr2 = adsr2;
            inst.gain = gain;
            inst.basePitchMult = basePitch;
            inst.fracPitchMult = fracPitch;
            inst.percussionNote = 0;
            inst.originalAddr = static_cast<uint16_t>(addr);
            inst.contentOrigin = defaultContentOrigin(inst.id, engineConfig_.defaultEngineProvidedInstrumentIds,
                                                      engineConfig_.hasDefaultEngineProvidedInstruments);

            instruments_.push_back(std::move(inst));
        }
    }

    applyPercussionTableNotes(instruments_, aramView, engineConfig_);

    // Addmusick tweak: scan for extended instrument entries placed after the sequence data of
    // each song, beyond the normal instrument table.
    if (engineConfig_.engineVariant == "addmusick") {
        parseExtendedInstruments(aramView, entrySize);
    }
}

void NspcProject::parseExtendedInstruments(emulation::AramView aramView, uint8_t entrySize) {
    // Addmusick places extra instrument entries after each song's sequence data.
    // Each song independently defines instruments starting at customInstrumentStartIndex,
    // so multiple songs can legitimately share the same instrument ID.
    // The unique key for a song-scoped instrument is (songId, id).

    // Track already-parsed (songId, instId) pairs to avoid duplicates.
    std::unordered_set<int> globalInstrumentIds;
    globalInstrumentIds.reserve(instruments_.size());
    for (const auto& inst : instruments_) {
        if (!inst.songId.has_value()) {
            globalInstrumentIds.insert(inst.id);
        }
    }

    // The base ID for custom instruments — same starting point for every song.
    const int customStartId = engineConfig_.customInstrumentStartIndex.has_value()
                                  ? static_cast<int>(*engineConfig_.customInstrumentStartIndex)
                                  : (!instruments_.empty() ? instruments_.back().id + 1 : 0);

    // Track which (songId, instId) pairs have already been added.
    std::unordered_set<int64_t> parsedSongInstKeys;
    for (const auto& inst : instruments_) {
        if (inst.songId.has_value()) {
            const int64_t key = (static_cast<int64_t>(*inst.songId) << 32) | static_cast<int64_t>(inst.id);
            parsedSongInstKeys.insert(key);
        }
    }

    for (const auto& song : songs_) {
        const uint16_t seqEnd = song.sequenceEndAddr();
        if (seqEnd == 0) {
            continue;
        }

        uint32_t addr = seqEnd;
        constexpr uint32_t kMaxExtendedInstruments = 32U;
        // Each song's custom instruments start at customStartId independently.
        int nextId = customStartId;

        for (uint32_t i = 0; i < kMaxExtendedInstruments && addr + entrySize <= 0x10000U; ++i, addr += entrySize) {
            const uint8_t sampleIndex = aramView.read(static_cast<uint16_t>(addr));
            const uint8_t adsr1 = aramView.read(static_cast<uint16_t>(addr + 1U));
            const uint8_t adsr2 = aramView.read(static_cast<uint16_t>(addr + 2U));
            const uint8_t gain = aramView.read(static_cast<uint16_t>(addr + 3U));
            const uint8_t basePitch = aramView.read(static_cast<uint16_t>(addr + 4U));
            uint8_t fracPitch = 0;
            if (entrySize >= 6) {
                fracPitch = aramView.read(static_cast<uint16_t>(addr + 5U));
            }

            const bool allFF = (sampleIndex == 0xFF && adsr1 == 0xFF && adsr2 == 0xFF && gain == 0xFF &&
                                basePitch == 0xFF) &&
                               (entrySize < 6 || fracPitch == 0xFF);
            const bool allZero = (sampleIndex == 0 && adsr1 == 0 && adsr2 == 0 && gain == 0 && basePitch == 0) &&
                                 (entrySize < 6 || fracPitch == 0);
            if (allFF || allZero) {
                break;
            }

            const uint8_t sampleId = sampleIndex & 0x7FU;
            const bool sampleExists =
                std::any_of(samples_.begin(), samples_.end(),
                            [sampleId](const BrrSample& sample) { return sample.id == sampleId; });
            if (!sampleExists) {
                break;
            }

            const int instId = nextId++;
            // Skip if this (songId, instId) pair was already added (e.g. from a saved project overlay).
            const int64_t key = (static_cast<int64_t>(song.songId()) << 32) | static_cast<int64_t>(instId);
            if (parsedSongInstKeys.contains(key)) {
                continue;
            }
            // Don't shadow a global instrument with the same id.
            if (globalInstrumentIds.contains(instId)) {
                continue;
            }

            NspcInstrument inst{};
            inst.id = instId;
            inst.sampleIndex = sampleIndex & 0x7FU;
            inst.adsr1 = adsr1;
            inst.adsr2 = adsr2;
            inst.gain = gain;
            inst.basePitchMult = basePitch;
            inst.fracPitchMult = fracPitch;
            inst.percussionNote = 0;
            inst.originalAddr = static_cast<uint16_t>(addr);
            inst.contentOrigin = NspcContentOrigin::UserProvided;
            inst.songId = song.songId();

            instruments_.push_back(std::move(inst));
            parsedSongInstKeys.insert(key);
        }
    }
}

void NspcProject::parseSamples() {
    if (engineConfig_.sampleHeaders == 0) {
        return;
    }

    emulation::AramView aramView(aram_.data(), aram_.size());

    // Sample directory entries are 4 bytes each:
    // Bytes 0-1: Sample start address (little endian)
    // Bytes 2-3: Loop point address (little endian)

    struct SampleDirectoryEntry {
        uint32_t entryIndex = 0;
        uint16_t sampleStart = 0;
        uint16_t loopPoint = 0;
    };

    std::vector<SampleDirectoryEntry> directoryEntries;
    directoryEntries.reserve(kMaxSampleDirectoryEntries);
    std::vector<uint16_t> sampleStarts;
    sampleStarts.reserve(kMaxSampleDirectoryEntries);

    for (uint32_t entryIndex = 0; entryIndex < kMaxSampleDirectoryEntries; ++entryIndex) {
        const uint32_t dirAddr = static_cast<uint32_t>(engineConfig_.sampleHeaders) + entryIndex * 4u;
        if (dirAddr + 3u >= kAramSize) {
            break;
        }

        const uint16_t sampleStart = aramView.read16(static_cast<uint16_t>(dirAddr));
        const uint16_t loopPoint = aramView.read16(static_cast<uint16_t>(dirAddr + 2u));
        directoryEntries.push_back(SampleDirectoryEntry{
            .entryIndex = entryIndex,
            .sampleStart = sampleStart,
            .loopPoint = loopPoint,
        });

        if (sampleStart != 0 && sampleStart != 0xFFFF && sampleStart >= 0x0200) {
            sampleStarts.push_back(sampleStart);
        }
    }

    std::sort(sampleStarts.begin(), sampleStarts.end());
    sampleStarts.erase(std::unique(sampleStarts.begin(), sampleStarts.end()), sampleStarts.end());
    const auto referencedSampleIds = collectReferencedSampleIdsFromInstrumentTable(aramView, engineConfig_);
    std::unordered_set<int> parsedSampleIds;

    for (const auto& entry : directoryEntries) {
        const uint16_t sampleStart = entry.sampleStart;
        const uint16_t loopPoint = entry.loopPoint;
        // Scan all entries up to max, but only keep valid BRR samples.
        if (sampleStart == 0 || sampleStart == 0xFFFF || sampleStart < 0x0200) {
            continue;
        }

        uint32_t parseLimit = kAramSize;
        const auto nextStartIt = std::upper_bound(sampleStarts.begin(), sampleStarts.end(), sampleStart);
        if (nextStartIt != sampleStarts.end()) {
            parseLimit = *nextStartIt;
        }

        const auto parsedBrr = parseBrrSample(aramView, sampleStart, parseLimit);
        if (!parsedBrr.has_value()) {
            continue;
        }

        BrrSample sample{};
        sample.id = static_cast<int>(entry.entryIndex);  // Keep directory index stable.
        sample.data = parsedBrr->bytes;
        sample.originalAddr = sampleStart;
        sample.originalLoopAddr = loopPoint;
        sample.contentOrigin = defaultContentOrigin(sample.id, engineConfig_.defaultEngineProvidedSampleIds,
                                                    engineConfig_.hasDefaultEngineProvidedSamples);

        samples_.push_back(std::move(sample));
        parsedSampleIds.insert(static_cast<int>(entry.entryIndex));
    }

    // Fallback pass: if an instrument references an entry rejected only by overlap boundary heuristics,
    // retry parsing without clipping to the next directory start.
    for (int sampleId = 0; sampleId < static_cast<int>(directoryEntries.size()); ++sampleId) {
        if (!referencedSampleIds.contains(sampleId) || parsedSampleIds.contains(sampleId)) {
            continue;
        }

        const auto& entry = directoryEntries[static_cast<size_t>(sampleId)];
        const uint16_t sampleStart = entry.sampleStart;
        const uint16_t loopPoint = entry.loopPoint;
        if (sampleStart == 0 || sampleStart == 0xFFFF || sampleStart < 0x0200) {
            continue;
        }

        const auto parsedBrr = parseBrrSample(aramView, sampleStart, kAramSize, true);
        if (!parsedBrr.has_value()) {
            continue;
        }

        BrrSample sample{};
        sample.id = static_cast<int>(entry.entryIndex);
        sample.data = parsedBrr->bytes;
        sample.originalAddr = sampleStart;
        sample.originalLoopAddr = loopPoint;
        sample.contentOrigin = defaultContentOrigin(sample.id, engineConfig_.defaultEngineProvidedSampleIds,
                                                    engineConfig_.hasDefaultEngineProvidedSamples);
        samples_.push_back(std::move(sample));
        parsedSampleIds.insert(sampleId);
    }

    std::sort(samples_.begin(), samples_.end(), [](const BrrSample& a, const BrrSample& b) { return a.id < b.id; });
}

void NspcProject::parseSongs() {
    if (engineConfig_.songIndexPointers == 0) {
        return;
    }

    emulation::AramView aramView(aram_.data(), aram_.size());
    std::unordered_set<uint16_t> discoveredPointers;
    const NspcCommandMap commandMap = engineConfig_.commandMap.value_or(NspcCommandMap{});

    // Skip sparse 0000 entries, but stop when we hit an invalid non-zero entry,
    // 0xFFFF terminator, or a duplicate pointer.
    for (int i = 0; i < static_cast<int>(kMaxSongEntries); ++i) {
        const uint32_t entryAddr32 = static_cast<uint32_t>(engineConfig_.songIndexPointers) +
                                     static_cast<uint32_t>(i) * 2u;
        if (entryAddr32 >= 0x10000u || entryAddr32 + 1u >= 0x10000u) {
            break;
        }

        const uint16_t entryAddr = static_cast<uint16_t>(entryAddr32);
        const uint16_t seqPtr = aramView.read16(entryAddr);

        // Skip 0000 gaps; some engines leave sparse song table entries.
        if (seqPtr == 0) {
            continue;
        }

        // Stop on 0xFFFF (end marker)
        if (seqPtr == 0xFFFF) {
            break;
        }

        // Stop on duplicate pointers
        if (discoveredPointers.contains(seqPtr)) {
            break;
        }

        if (!isLikelySongPointer(aramView, seqPtr, commandMap, engineConfig_)) {
            common::logInfo(std::format(
                "Stopped parsing songs at index {:02X}: pointer ${:04X} is not a valid N-SPC song", i, seqPtr));
            break;
        }

        try {
            NspcSong song(aramView, engineConfig_, i);
            song.setContentOrigin(defaultContentOrigin(song.songId(), engineConfig_.defaultEngineProvidedSongIds,
                                                       engineConfig_.hasDefaultEngineProvidedSongs));
            collectSongPointers(song, seqPtr, discoveredPointers);
            songs_.push_back(std::move(song));
        } catch (const std::exception& ex) {
            common::logInfo(std::format("Stopped parsing songs at index {:02X}: {}", i, ex.what()));
            break;
        }
    }
}

std::optional<size_t> NspcProject::addEmptySong() {
    if (songs_.size() >= kMaxSongEntries) {
        return std::nullopt;
    }

    const size_t newSongIndex = songs_.size();
    songs_.push_back(NspcSong::createEmpty(static_cast<int>(newSongIndex)));
    songs_.back().setContentOrigin(NspcContentOrigin::UserProvided);
    refreshAramUsage();
    return newSongIndex;
}

std::optional<size_t> NspcProject::duplicateSong(size_t songIndex) {
    if (songs_.size() >= kMaxSongEntries || songIndex >= songs_.size()) {
        return std::nullopt;
    }

    NspcSong duplicate = songs_[songIndex];
    duplicate.setContentOrigin(NspcContentOrigin::UserProvided);
    songs_.insert(songs_.begin() + static_cast<std::ptrdiff_t>(songIndex + 1), std::move(duplicate));
    reindexSongsAndLayouts(songs_, songAddressLayouts_);
    refreshAramUsage();
    return songIndex + 1;
}

bool NspcProject::removeSong(size_t songIndex) {
    if (songIndex >= songs_.size()) {
        return false;
    }

    songs_.erase(songs_.begin() + static_cast<std::ptrdiff_t>(songIndex));
    reindexSongsAndLayouts(songs_, songAddressLayouts_);
    refreshAramUsage();
    return true;
}

bool NspcProject::setSongContentOrigin(size_t songIndex, NspcContentOrigin origin) {
    if (songIndex >= songs_.size()) {
        return false;
    }
    songs_[songIndex].setContentOrigin(origin);
    return true;
}

bool NspcProject::setInstrumentContentOrigin(int instrumentId, NspcContentOrigin origin) {
    const auto it =
        std::find_if(instruments_.begin(), instruments_.end(),
                     [instrumentId](const NspcInstrument& instrument) { return instrument.id == instrumentId; });
    if (it == instruments_.end()) {
        return false;
    }
    it->contentOrigin = origin;
    return true;
}

bool NspcProject::setSampleContentOrigin(int sampleId, NspcContentOrigin origin) {
    const auto it = std::find_if(samples_.begin(), samples_.end(),
                                 [sampleId](const BrrSample& sample) { return sample.id == sampleId; });
    if (it == samples_.end()) {
        return false;
    }
    it->contentOrigin = origin;
    return true;
}

bool NspcProject::setEngineExtensionEnabled(std::string_view extensionName, bool enabled) {
    auto it =
        std::find_if(engineConfig_.extensions.begin(), engineConfig_.extensions.end(),
                     [extensionName](const NspcEngineExtension& extension) { return extension.name == extensionName; });
    if (it == engineConfig_.extensions.end()) {
        return false;
    }
    it->enabled = enabled;
    return true;
}

bool NspcProject::isEngineExtensionEnabled(std::string_view extensionName) const {
    const auto it =
        std::find_if(engineConfig_.extensions.begin(), engineConfig_.extensions.end(),
                     [extensionName](const NspcEngineExtension& extension) { return extension.name == extensionName; });
    if (it == engineConfig_.extensions.end()) {
        return false;
    }
    return it->enabled;
}

std::vector<std::string> NspcProject::enabledEngineExtensionNames() const {
    std::vector<std::string> names;
    names.reserve(engineConfig_.extensions.size());
    for (const auto& extension : engineConfig_.extensions) {
        if (extension.enabled) {
            names.push_back(extension.name);
        }
    }
    return names;
}

const NspcSongAddressLayout* NspcProject::songAddressLayout(int songId) const {
    const auto it = songAddressLayouts_.find(songId);
    if (it == songAddressLayouts_.end()) {
        return nullptr;
    }
    return &it->second;
}

void NspcProject::setSongAddressLayout(int songId, NspcSongAddressLayout layout) {
    songAddressLayouts_[songId] = std::move(layout);
}

void NspcProject::clearSongAddressLayout(int songId) {
    songAddressLayouts_.erase(songId);
}

void NspcProject::refreshAramUsage() {
    rebuildAramUsage();
}

void NspcProject::rebuildAramUsage() {
    NspcAramUsage usage;
    usage.totalBytes = kAramSize;

    std::vector<NspcAramRegion> regions;
    regions.reserve(engineConfig_.reserved.size() + songs_.size() * 4 + instruments_.size() + samples_.size() * 2 + 8);
    collectStaticAramRegions(engineConfig_, songs_, instruments_, samples_, regions);

    emulation::AramView aramView(aram_.data(), aram_.size());
    collectSongAramRegions(*this, aramView, regions);

    std::array<NspcAramRegionKind, NspcAramUsage::kTotalAramBytes> ownership{};
    ownership.fill(NspcAramRegionKind::Free);
    for (const auto& region : regions) {
        paintRegion(ownership, region);
    }
    tallyAramOwnership(ownership, usage);

    usage.usedBytes = usage.totalBytes - usage.freeBytes;

    std::sort(regions.begin(), regions.end(), [](const NspcAramRegion& lhs, const NspcAramRegion& rhs) {
        if (lhs.from != rhs.from) {
            return lhs.from < rhs.from;
        }
        return lhs.to < rhs.to;
    });
    usage.regions = std::move(regions);

    aramUsage_ = std::move(usage);
}

void NspcProject::syncAramToSpcData() {
    constexpr size_t kSpcHeaderSize = 0x100;
    if (sourceSpcData_.size() < kSpcHeaderSize + kAramSize) {
        return;
    }
    const auto aramAll = aram().all();
    std::copy(aramAll.begin(), aramAll.end(), sourceSpcData_.begin() + static_cast<std::ptrdiff_t>(kSpcHeaderSize));
}

void NspcProject::syncAramRangeToSpcData(uint16_t addr, size_t size) {
    constexpr size_t kSpcHeaderSize = 0x100;
    if (size == 0 || static_cast<uint32_t>(addr) + static_cast<uint32_t>(size) > kAramSize) {
        return;
    }
    if (sourceSpcData_.size() < kSpcHeaderSize + kAramSize) {
        return;
    }
    const auto src = aram().bytes(addr, size);
    auto dstIt = sourceSpcData_.begin() + static_cast<std::ptrdiff_t>(kSpcHeaderSize + static_cast<size_t>(addr));
    std::copy(src.begin(), src.end(), dstIt);
}

}  // namespace ntrak::nspc
