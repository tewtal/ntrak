#include "ntrak/nspc/NspcConverter.hpp"

#include "ntrak/nspc/NspcData.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <set>

namespace ntrak::nspc {
namespace {

constexpr uint32_t kAramSize = 0x10000u;
constexpr uint32_t kMaxSampleDirectoryEntries = 64u;
constexpr uint32_t kMaxInstrumentEntries = 64u;
constexpr uint32_t kMaxSongEntries = 256u;

/// Walk all event entries in every track and subroutine of the song,
/// calling the callback for each entry.
template <typename Fn>
void walkAllEvents(NspcSong& song, Fn callback) {
    for (auto& track : song.tracks()) {
        for (auto& entry : track.events) {
            callback(entry);
        }
    }
    for (auto& sub : song.subroutines()) {
        for (auto& entry : sub.events) {
            callback(entry);
        }
    }
}

template <typename Fn>
void walkAllEventsConst(const NspcSong& song, Fn callback) {
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            callback(entry);
        }
    }
    for (const auto& sub : song.subroutines()) {
        for (const auto& entry : sub.events) {
            callback(entry);
        }
    }
}

int maxInstrumentId(const NspcProject& project) {
    int maxId = -1;
    for (const auto& inst : project.instruments()) {
        maxId = std::max(maxId, inst.id);
    }
    return maxId;
}

int maxSampleId(const NspcProject& project) {
    int maxId = -1;
    for (const auto& sample : project.samples()) {
        maxId = std::max(maxId, sample.id);
    }
    return maxId;
}

const BrrSample* findSampleById(const NspcProject& project, int id) {
    for (const auto& sample : project.samples()) {
        if (sample.id == id) {
            return &sample;
        }
    }
    return nullptr;
}

const NspcInstrument* findInstrumentById(const NspcProject& project, int id) {
    for (const auto& inst : project.instruments()) {
        if (inst.id == id) {
            return &inst;
        }
    }
    return nullptr;
}

/// Find a sample in target that has identical BRR data. Returns its id, or -1 if not found.
int findMatchingSampleId(const NspcProject& target, const std::vector<uint8_t>& brrData) {
    for (const auto& sample : target.samples()) {
        if (sample.data == brrData) {
            return sample.id;
        }
    }
    return -1;
}

void collectUsedInstrumentIdsFromEventStream(const std::vector<NspcEventEntry>& events, std::set<int>& ids) {
    int percussionBaseId = 0;

    for (const auto& entry : events) {
        if (const auto* vcmd = std::get_if<Vcmd>(&entry.event)) {
            std::visit(overloaded{
                           [&](const VcmdInst& v) { ids.insert(v.instrumentIndex & 0x7F); },
                           [&](const VcmdPercussionBaseInstrument& v) { percussionBaseId = v.index & 0x7F; },
                           [](const auto&) {},
                       },
                       vcmd->vcmd);
            continue;
        }

        if (const auto* percussion = std::get_if<Percussion>(&entry.event)) {
            ids.insert((percussionBaseId + percussion->index) & 0x7F);
        }
    }
}

std::optional<int> resolveSmwPercussionInstrumentId(const NspcProject& source, uint8_t percussionIndex) {
    const auto& cfg = source.engineConfig();
    if (cfg.engineVersion != "0.0" || cfg.percussionHeaders == 0) {
        return std::nullopt;
    }

    const auto commandMap = cfg.commandMap.value_or(NspcCommandMap{});
    const int percussionCount =
        static_cast<int>(commandMap.percussionEnd) - static_cast<int>(commandMap.percussionStart) + 1;
    if (percussionIndex >= static_cast<uint8_t>(std::max(0, percussionCount))) {
        return std::nullopt;
    }

    const uint8_t percEntrySize = std::clamp<uint8_t>(cfg.percussionEntryBytes, 6, 7);
    const uint32_t addr = static_cast<uint32_t>(cfg.percussionHeaders) +
                          static_cast<uint32_t>(percussionIndex) * percEntrySize;
    if (addr + percEntrySize > kAramSize) {
        return std::nullopt;
    }

    const auto aram = source.aram();
    const uint8_t sampleIndex = aram.read(static_cast<uint16_t>(addr + 0u));
    const uint8_t adsr1 = aram.read(static_cast<uint16_t>(addr + 1u));
    const uint8_t adsr2 = aram.read(static_cast<uint16_t>(addr + 2u));
    const uint8_t gain = aram.read(static_cast<uint16_t>(addr + 3u));
    const uint8_t basePitch = aram.read(static_cast<uint16_t>(addr + 4u));
    const uint8_t note = aram.read(static_cast<uint16_t>(addr + percEntrySize - 1u));

    for (const auto& inst : source.instruments()) {
        if (inst.sampleIndex == sampleIndex && inst.adsr1 == adsr1 && inst.adsr2 == adsr2 && inst.gain == gain &&
            inst.basePitchMult == basePitch && inst.percussionNote == note) {
            return inst.id;
        }
    }

    for (const auto& inst : source.instruments()) {
        if (inst.sampleIndex == sampleIndex && inst.adsr1 == adsr1 && inst.adsr2 == adsr2 && inst.gain == gain &&
            inst.basePitchMult == basePitch) {
            return inst.id;
        }
    }

    return std::nullopt;
}

uint8_t convertPercussionNoteToPitch(uint8_t rawNote, const NspcCommandMap& sourceCommandMap) {
    if (rawNote >= sourceCommandMap.noteStart && rawNote <= sourceCommandMap.noteEnd) {
        return static_cast<uint8_t>(rawNote - sourceCommandMap.noteStart);
    }
    return rawNote;
}

void convertSmwPercussionToNotes(NspcSong& song, const NspcProject& source) {
    const bool isSmwProto = source.engineConfig().engineVersion == "0.0";
    if (!isSmwProto) {
        return;
    }

    const auto sourceCommandMap = source.engineConfig().commandMap.value_or(NspcCommandMap{});
    NspcEventId nextEventId = song.peekNextEventId();

    auto convertStream = [&](std::vector<NspcEventEntry>& events) {
        std::vector<NspcEventEntry> converted;
        converted.reserve(events.size());

        int percussionBaseId = 0;
        std::optional<int> currentInstrumentId = std::nullopt;

        for (const auto& entry : events) {
            if (const auto* vcmd = std::get_if<Vcmd>(&entry.event)) {
                if (const auto* inst = std::get_if<VcmdInst>(&vcmd->vcmd)) {
                    currentInstrumentId = inst->instrumentIndex & 0x7F;
                } else if (const auto* base = std::get_if<VcmdPercussionBaseInstrument>(&vcmd->vcmd)) {
                    percussionBaseId = base->index & 0x7F;
                }
                converted.push_back(entry);
                continue;
            }

            const auto* percussion = std::get_if<Percussion>(&entry.event);
            if (percussion == nullptr) {
                converted.push_back(entry);
                continue;
            }

            const int sourceInstrumentId =
                resolveSmwPercussionInstrumentId(source, percussion->index).value_or((percussionBaseId + percussion->index) & 0x7F);
            const NspcInstrument* sourceInst = findInstrumentById(source, sourceInstrumentId);
            if (sourceInst == nullptr) {
                converted.push_back(entry);
                continue;
            }

            if (!currentInstrumentId.has_value() || *currentInstrumentId != sourceInstrumentId) {
                NspcEventEntry instrumentSelect{};
                instrumentSelect.id = nextEventId++;
                instrumentSelect.event = NspcEvent{Vcmd{VcmdInst{static_cast<uint8_t>(sourceInstrumentId)}}};
                instrumentSelect.originalAddr = entry.originalAddr;
                converted.push_back(std::move(instrumentSelect));
                currentInstrumentId = sourceInstrumentId;
            }

            NspcEventEntry noteEvent = entry;
            noteEvent.event =
                NspcEvent{Note{convertPercussionNoteToPitch(sourceInst->percussionNote, sourceCommandMap)}};
            converted.push_back(std::move(noteEvent));
        }

        events = std::move(converted);
    };

    for (auto& track : song.tracks()) {
        convertStream(track.events);
    }
    for (auto& subroutine : song.subroutines()) {
        convertStream(subroutine.events);
    }

    song.setNextEventId(nextEventId);
}

}  // namespace

std::vector<int> findUsedInstrumentIds(const NspcProject& project, int songIndex) {
    if (songIndex < 0 || songIndex >= static_cast<int>(project.songs().size())) {
        return {};
    }

    std::set<int> ids;
    const auto& song = project.songs()[songIndex];
    const bool isSmwProto = project.engineConfig().engineVersion == "0.0";
    if (!isSmwProto) {
        for (const auto& track : song.tracks()) {
            collectUsedInstrumentIdsFromEventStream(track.events, ids);
        }
        for (const auto& subroutine : song.subroutines()) {
            collectUsedInstrumentIdsFromEventStream(subroutine.events, ids);
        }
        return {ids.begin(), ids.end()};
    }

    auto collectSmwStream = [&](const std::vector<NspcEventEntry>& events) {
        for (const auto& entry : events) {
            if (const auto* vcmd = std::get_if<Vcmd>(&entry.event)) {
                if (const auto* inst = std::get_if<VcmdInst>(&vcmd->vcmd)) {
                    ids.insert(inst->instrumentIndex & 0x7F);
                }
                continue;
            }
            if (const auto* perc = std::get_if<Percussion>(&entry.event)) {
                if (const auto resolved = resolveSmwPercussionInstrumentId(project, perc->index); resolved.has_value()) {
                    ids.insert(*resolved);
                } else {
                    ids.insert(perc->index & 0x7F);
                }
            }
        }
    };
    for (const auto& track : song.tracks()) {
        collectSmwStream(track.events);
    }
    for (const auto& subroutine : song.subroutines()) {
        collectSmwStream(subroutine.events);
    }

    return {ids.begin(), ids.end()};
}

std::vector<InstrumentMapping> buildDefaultMappings(const NspcProject& source, const NspcProject& target,
                                                    int sourceSongIndex) {
    const auto usedIds = findUsedInstrumentIds(source, sourceSongIndex);
    std::vector<InstrumentMapping> mappings;
    mappings.reserve(usedIds.size());

    int nextTargetId = maxInstrumentId(target) + 1;
    for (const int srcId : usedIds) {
        InstrumentMapping m;
        m.sourceInstrumentId = srcId;
        m.action = InstrumentMapping::Action::Copy;
        m.targetInstrumentId = nextTargetId++;
        mappings.push_back(m);
    }
    return mappings;
}

SongPortResult portSong(const NspcProject& source, NspcProject& target, const SongPortRequest& request) {
    SongPortResult result;

    // Validate source
    if (request.sourceSongIndex < 0 || request.sourceSongIndex >= static_cast<int>(source.songs().size())) {
        result.error = "Invalid source song index";
        return result;
    }

    // Validate target overwrite index
    if (request.targetSongIndex >= static_cast<int>(target.songs().size())) {
        result.error = "Invalid target song index";
        return result;
    }

    std::set<int> explicitlyDeletedInstrumentIds(request.instrumentsToDelete.begin(), request.instrumentsToDelete.end());
    std::set<int> samplesRequestedForDeletion(request.samplesToDelete.begin(), request.samplesToDelete.end());

    // Remove explicitly deleted instruments and mark their samples for possible deletion.
    {
        auto& instruments = target.instruments();
        auto it = instruments.begin();
        while (it != instruments.end()) {
            if (!explicitlyDeletedInstrumentIds.contains(it->id)) {
                ++it;
                continue;
            }
            samplesRequestedForDeletion.insert(it->sampleIndex & 0x7F);
            it = instruments.erase(it);
        }
    }

    // Delete selected samples only when no remaining instrument references them.
    {
        std::set<int> inUseSampleIds;
        for (const auto& inst : target.instruments()) {
            inUseSampleIds.insert(inst.sampleIndex & 0x7F);
        }

        auto& samples = target.samples();
        auto it = samples.begin();
        while (it != samples.end()) {
            if (!samplesRequestedForDeletion.contains(it->id) || inUseSampleIds.contains(it->id)) {
                ++it;
                continue;
            }
            it = samples.erase(it);
        }
    }

    // Capture ARAM usage AFTER deletions so freed space is available for new samples.
    // Replaced samples will additionally be excluded from the blocked list.
    target.refreshAramUsage();
    std::vector<NspcAramRegion> savedRegions = target.aramUsage().regions;

    // Deep copy source song
    NspcSong portedSong = source.songs()[request.sourceSongIndex];

    // Process instrument mappings
    int nextInstId = maxInstrumentId(target) + 1;
    int nextSampleId = maxSampleId(target) + 1;
    std::set<int> replacedSampleIds;  // Samples being overwritten — their old ARAM space is freed

    for (const auto& mapping : request.instrumentMappings) {
        if (mapping.action == InstrumentMapping::Action::MapToExisting) {
            if (mapping.targetInstrumentId < 0) {
                result.error = "MapToExisting mapping has invalid target id";
                return result;
            }
            if (findInstrumentById(target, mapping.targetInstrumentId) == nullptr) {
                result.error = std::format("Mapped target instrument ${:02X} does not exist", mapping.targetInstrumentId);
                return result;
            }
            result.instrumentRemap[mapping.sourceInstrumentId] = mapping.targetInstrumentId;
            continue;
        }

        // Copy action
        const NspcInstrument* srcInst = findInstrumentById(source, mapping.sourceInstrumentId);
        if (srcInst == nullptr) {
            result.error = std::format("Source instrument ${:02X} not found in project", mapping.sourceInstrumentId);
            result.success = false;
            return result;
        }

        const bool isNoise = (srcInst->sampleIndex & 0x80) != 0;
        const BrrSample* srcSample = nullptr;
        if (!isNoise) {
            srcSample = findSampleById(source, srcInst->sampleIndex & 0x7F);
            if (srcSample == nullptr) {
                result.error = std::format("Source sample ${:02X} (referenced by instrument ${:02X}) not found in project", 
                                           srcInst->sampleIndex & 0x7F, srcInst->id);
                result.success = false;
                return result;
            }
        }

        // Compute relative loop offset from source (preserved across ARAM relocation)
        uint16_t srcLoopOffset = 0;
        if (srcSample != nullptr && srcSample->originalAddr != 0 &&
            srcSample->originalLoopAddr >= srcSample->originalAddr) {
            srcLoopOffset = srcSample->originalLoopAddr - srcSample->originalAddr;
        }

        int chosenSampleId = -1;

        if (mapping.sampleAction == InstrumentMapping::SampleAction::UseExisting) {
            // Only valid if the sample wasn't deleted
            if (findSampleById(target, mapping.targetSampleId) != nullptr) {
                chosenSampleId = mapping.targetSampleId;
            }
            // else: fall through to CopyNew
        } else if (mapping.sampleAction == InstrumentMapping::SampleAction::ReplaceExisting &&
                   srcSample != nullptr && mapping.targetSampleId >= 0) {
            // Overwrite the existing target sample's data; reuse its directory slot
            const auto it = std::find_if(target.samples().begin(), target.samples().end(),
                                         [&](const BrrSample& s) { return s.id == mapping.targetSampleId; });
            if (it != target.samples().end()) {
                it->data = srcSample->data;
                it->originalAddr = 0;         // Cleared so ARAM allocator re-places it
                it->originalLoopAddr = srcLoopOffset;  // Temporarily relative
                it->contentOrigin = NspcContentOrigin::UserProvided;
                replacedSampleIds.insert(mapping.targetSampleId);
                chosenSampleId = mapping.targetSampleId;
            }
            // If target sample not found, fall through to CopyNew below
        }

        if (!isNoise && chosenSampleId < 0) {
            // CopyNew (default) or ReplaceExisting fallback: allocate a new sample
            // Deduplicate by BRR data for CopyNew
            if (mapping.sampleAction == InstrumentMapping::SampleAction::CopyNew) {
                chosenSampleId = findMatchingSampleId(target, srcSample->data);
            }
            if (chosenSampleId < 0) {
                chosenSampleId = nextSampleId++;
                BrrSample newSample;
                newSample.id = chosenSampleId;
                newSample.name = srcSample->name;
                newSample.data = srcSample->data;
                newSample.originalAddr = 0;
                newSample.originalLoopAddr = srcLoopOffset;  // Temporarily relative
                newSample.contentOrigin = NspcContentOrigin::UserProvided;
                target.samples().push_back(std::move(newSample));
            }
        }

        // Copy instrument and write its bytes into the target ARAM instrument table
        const int newInstId = nextInstId++;
        NspcInstrument newInst = *srcInst;
        newInst.id = newInstId;
        if (!isNoise && chosenSampleId >= 0) {
            newInst.sampleIndex = static_cast<uint8_t>(chosenSampleId);
        }
        newInst.contentOrigin = NspcContentOrigin::UserProvided;
        newInst.songId = std::nullopt;  // Converted instruments always go into the global table

        // Calculate the address in the instrument table and write the entry bytes
        const auto& tgtEngine = target.engineConfig();
        const uint8_t instEntrySize = std::clamp<uint8_t>(tgtEngine.instrumentEntryBytes, 5, 6);
        if (tgtEngine.instrumentHeaders != 0) {
            const uint32_t instAddr = static_cast<uint32_t>(tgtEngine.instrumentHeaders) +
                                      static_cast<uint32_t>(newInstId) * instEntrySize;
            if (instAddr + instEntrySize <= 0x10000u) {
                newInst.originalAddr = static_cast<uint16_t>(instAddr);
                auto aramView = target.aram();
                aramView.write(newInst.originalAddr + 0, newInst.sampleIndex);
                aramView.write(newInst.originalAddr + 1, newInst.adsr1);
                aramView.write(newInst.originalAddr + 2, newInst.adsr2);
                aramView.write(newInst.originalAddr + 3, newInst.gain);
                aramView.write(newInst.originalAddr + 4, newInst.basePitchMult);
                if (instEntrySize >= 6) {
                    aramView.write(newInst.originalAddr + 5, newInst.fracPitchMult);
                }

                if (tgtEngine.engineVersion == "0.0" && tgtEngine.percussionHeaders != 0 && newInst.id >= 0) {
                    const auto commandMap = tgtEngine.commandMap.value_or(NspcCommandMap{});
                    const int percussionCount = static_cast<int>(commandMap.percussionEnd) -
                                                static_cast<int>(commandMap.percussionStart) + 1;
                    if (newInst.id < percussionCount) {
                        const uint8_t tgtPercEntrySize = std::clamp<uint8_t>(tgtEngine.percussionEntryBytes, 6, 7);
                        const uint32_t percussionAddr =
                            static_cast<uint32_t>(tgtEngine.percussionHeaders) +
                            static_cast<uint32_t>(newInst.id) * tgtPercEntrySize;
                        if (percussionAddr + tgtPercEntrySize <= 0x10000u) {
                            aramView.write(static_cast<uint16_t>(percussionAddr + 0u), newInst.sampleIndex);
                            aramView.write(static_cast<uint16_t>(percussionAddr + 1u), newInst.adsr1);
                            aramView.write(static_cast<uint16_t>(percussionAddr + 2u), newInst.adsr2);
                            aramView.write(static_cast<uint16_t>(percussionAddr + 3u), newInst.gain);
                            aramView.write(static_cast<uint16_t>(percussionAddr + 4u), newInst.basePitchMult);
                            if (tgtPercEntrySize >= 7) {
                                aramView.write(static_cast<uint16_t>(percussionAddr + 5u), newInst.fracPitchMult);
                            }
                            aramView.write(static_cast<uint16_t>(percussionAddr + tgtPercEntrySize - 1u),
                                           newInst.percussionNote);
                        }
                    }
                }
            }
        } else {
            newInst.originalAddr = 0;
        }

        target.instruments().push_back(std::move(newInst));

        result.instrumentRemap[mapping.sourceInstrumentId] = newInstId;
    }

    // SMW proto engines store per-instrument percussion notes.
    // Convert Percussion events to explicit instrument+Note pairs when targeting non-SMW engines.
    if (source.engineConfig().engineVersion == "0.0" && target.engineConfig().engineVersion != "0.0") {
        convertSmwPercussionToNotes(portedSong, source);
    }

    // Remap instrument references in the copied song
    walkAllEvents(portedSong, [&](NspcEventEntry& entry) {
        if (auto* vcmd = std::get_if<Vcmd>(&entry.event)) {
            std::visit(overloaded{
                           [&](VcmdInst& v) {
                               const uint8_t rawId = v.instrumentIndex;
                               const uint8_t cleanId = rawId & 0x7F;
                               const uint8_t flag = rawId & 0x80;
                               if (auto it = result.instrumentRemap.find(cleanId);
                                   it != result.instrumentRemap.end()) {
                                   v.instrumentIndex = static_cast<uint8_t>(it->second) | flag;
                               }
                           },
                           [&](VcmdPercussionBaseInstrument& v) {
                               const uint8_t rawId = v.index;
                               const uint8_t cleanId = rawId & 0x7F;
                               const uint8_t flag = rawId & 0x80;
                               if (auto it = result.instrumentRemap.find(cleanId);
                                   it != result.instrumentRemap.end()) {
                                   v.index = static_cast<uint8_t>(it->second) | flag;
                               }
                           },
                           [](auto&) {},
                       },
                       vcmd->vcmd);
        }
    });

    portedSong.setContentOrigin(NspcContentOrigin::UserProvided);

    // Allocate ARAM addresses for samples that need placement (originalAddr == 0).
    // Uses the saved pre-modification regions; skips regions belonging to replaced samples
    // so their old ARAM space can be reclaimed.
    {
        struct AramRange {
            uint32_t from = 0;
            uint32_t to = 0;
        };

        std::vector<AramRange> blocked;
        const auto addBlockedRange = [&](uint32_t from, uint32_t to) {
            from = std::min(from, kAramSize);
            to = std::min(to, kAramSize);
            if (to > from) {
                blocked.push_back(AramRange{from, to});
            }
        };

        addBlockedRange(0, 1);  // Never allocate at address 0
        for (const auto& region : savedRegions) {
            // Free up space from samples we're replacing
            if (region.kind == NspcAramRegionKind::SampleData && replacedSampleIds.contains(region.objectId)) {
                continue;
            }
            if (region.to > region.from) {
                addBlockedRange(region.from, region.to);
            }
        }

        // Always reserve structural engine tables; these must never hold BRR data,
        // even if all current assets in a table are deleted.
        const auto& tgtEngine = target.engineConfig();
        if (tgtEngine.songIndexPointers != 0) {
            addBlockedRange(tgtEngine.songIndexPointers, static_cast<uint32_t>(tgtEngine.songIndexPointers) + kMaxSongEntries * 2u);
        }
        if (tgtEngine.instrumentHeaders != 0) {
            const uint32_t entrySize = std::clamp<uint8_t>(tgtEngine.instrumentEntryBytes, 5, 6);
            addBlockedRange(tgtEngine.instrumentHeaders,
                            static_cast<uint32_t>(tgtEngine.instrumentHeaders) + kMaxInstrumentEntries * entrySize);
        }
        if (tgtEngine.sampleHeaders != 0) {
            addBlockedRange(tgtEngine.sampleHeaders,
                            static_cast<uint32_t>(tgtEngine.sampleHeaders) + kMaxSampleDirectoryEntries * 4u);
        }

        std::sort(blocked.begin(), blocked.end(), [](const AramRange& a, const AramRange& b) {
            return a.from < b.from;
        });
        std::vector<AramRange> merged;
        for (const auto& r : blocked) {
            if (!merged.empty() && r.from <= merged.back().to) {
                merged.back().to = std::max(merged.back().to, r.to);
            } else {
                merged.push_back(r);
            }
        }

        for (auto& sample : target.samples()) {
            if (sample.contentOrigin != NspcContentOrigin::UserProvided || sample.originalAddr != 0 ||
                sample.data.empty()) {
                continue;
            }

            const auto size = static_cast<uint32_t>(sample.data.size());
            const uint16_t savedLoopOffset = sample.originalLoopAddr;  // Was stored as relative

            // First-fit: find a gap large enough
            uint32_t cursor = 1;
            bool found = false;
            for (const auto& block : merged) {
                if (cursor + size <= block.from) {
                    found = true;
                    break;
                }
                cursor = std::max(cursor, block.to);
            }
            if (!found && cursor + size <= kAramSize) {
                found = true;
            }

            if (!found) {
                result.success = false;
                result.error = std::format("Not enough free ARAM for sample {:02X} ({} bytes)", sample.id, size);
                return result;
            }

            // Assign absolute addresses
            sample.originalAddr = static_cast<uint16_t>(cursor);
            sample.originalLoopAddr = static_cast<uint16_t>(cursor + savedLoopOffset);

            // Write BRR data and update Sample Directory (DIR) in target ARAM
            {
                auto aramView = target.aram();
                // 1. Write BRR bytes
                auto dst = aramView.bytes(sample.originalAddr, sample.data.size());
                std::copy(sample.data.begin(), sample.data.end(), dst.begin());

                // 2. Update DIR entry (4 bytes: start_addr_le, loop_addr_le)
                const auto& tgtEngine = target.engineConfig();
                if (tgtEngine.sampleHeaders != 0) {
                    const uint32_t dirAddr = static_cast<uint32_t>(tgtEngine.sampleHeaders) +
                                              static_cast<uint32_t>(sample.id) * 4u;
                    if (dirAddr + 4u <= 0x10000u) {
                        aramView.write16(static_cast<uint16_t>(dirAddr), sample.originalAddr);
                        aramView.write16(static_cast<uint16_t>(dirAddr + 2), sample.originalLoopAddr);
                    }
                }
            }

            // Reserve this range for subsequent samples
            const AramRange newRange{cursor, cursor + size};
            const auto insertPos =
                std::lower_bound(merged.begin(), merged.end(), newRange,
                                 [](const AramRange& a, const AramRange& b) { return a.from < b.from; });
            merged.insert(insertPos, newRange);
        }
    }

    // Place song in target
    if (request.targetSongIndex < 0) {
        // Append new
        const int newSongId = static_cast<int>(target.songs().size());
        portedSong.setSongId(newSongId);
        target.songs().push_back(std::move(portedSong));
        result.resultSongIndex = static_cast<int>(target.songs().size()) - 1;
    } else {
        // Overwrite existing — preserve original song id
        const int existingId = target.songs()[request.targetSongIndex].songId();
        portedSong.setSongId(existingId);
        target.songs()[request.targetSongIndex] = std::move(portedSong);
        result.resultSongIndex = request.targetSongIndex;
    }

    result.success = true;
    return result;
}

}  // namespace ntrak::nspc
