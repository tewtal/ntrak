#include "ntrak/nspc/NspcData.hpp"

#include "ntrak/emulation/SpcDsp.hpp"
#include "ntrak/nspc/NspcEngine.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace ntrak::nspc {

namespace {

NspcEventEntry* resolveEventEntry(std::vector<NspcTrack>& tracks, std::vector<NspcSubroutine>& subroutines,
                                  const NspcEventRef& ref) {
    auto resolve = [&](auto& owners) -> NspcEventEntry* {
        if (ref.ownerId < 0) {
            return nullptr;
        }

        const size_t ownerIndex = static_cast<size_t>(ref.ownerId);
        if (ownerIndex >= owners.size()) {
            return nullptr;
        }

        auto& owner = owners[ownerIndex];
        if (owner.id != ref.ownerId) {
            return nullptr;
        }
        if (ref.eventIndex >= owner.events.size()) {
            return nullptr;
        }

        auto& entry = owner.events[ref.eventIndex];
        if (ref.eventId != 0 && entry.id != ref.eventId) {
            return nullptr;
        }
        return &entry;
    };

    if (ref.owner == NspcEventOwner::Track) {
        return resolve(tracks);
    }
    return resolve(subroutines);
}

const NspcEventEntry* resolveEventEntry(const std::vector<NspcTrack>& tracks,
                                        const std::vector<NspcSubroutine>& subroutines, const NspcEventRef& ref) {
    auto resolve = [&](const auto& owners) -> const NspcEventEntry* {
        if (ref.ownerId < 0) {
            return nullptr;
        }

        const size_t ownerIndex = static_cast<size_t>(ref.ownerId);
        if (ownerIndex >= owners.size()) {
            return nullptr;
        }

        const auto& owner = owners[ownerIndex];
        if (owner.id != ref.ownerId) {
            return nullptr;
        }
        if (ref.eventIndex >= owner.events.size()) {
            return nullptr;
        }

        const auto& entry = owner.events[ref.eventIndex];
        if (ref.eventId != 0 && entry.id != ref.eventId) {
            return nullptr;
        }
        return &entry;
    };

    if (ref.owner == NspcEventOwner::Track) {
        return resolve(tracks);
    }
    return resolve(subroutines);
}

std::optional<uint8_t> mapReadVcmdId(const NspcCommandMap& map, uint8_t rawId) {
    const auto it = map.readVcmdMap.find(rawId);
    if (it != map.readVcmdMap.end()) {
        return it->second;
    }
    if (map.strictReadVcmdMap && !map.readVcmdMap.empty()) {
        return std::nullopt;
    }
    return rawId;
}

std::optional<uint8_t> extensionParamCountForId(const std::unordered_map<uint8_t, uint8_t>& extensionParamsById,
                                                uint8_t id) {
    if (const auto it = extensionParamsById.find(id); it != extensionParamsById.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool hasSubroutineCall(const NspcEventEntry& entry) {
    const auto* vcmd = std::get_if<Vcmd>(&entry.event);
    if (!vcmd) {
        return false;
    }
    return std::holds_alternative<VcmdSubroutineCall>(vcmd->vcmd);
}

NspcEventId nextEventIdForSong(const std::vector<NspcTrack>& tracks, const std::vector<NspcSubroutine>& subroutines) {
    NspcEventId next = 1;
    for (const auto& track : tracks) {
        for (const auto& entry : track.events) {
            next = std::max(next, entry.id + 1);
        }
    }
    for (const auto& subroutine : subroutines) {
        for (const auto& entry : subroutine.events) {
            next = std::max(next, entry.id + 1);
        }
    }
    return next;
}

bool wouldRecurse(const std::vector<int>& stack, int subroutineId) {
    return std::find(stack.begin(), stack.end(), subroutineId) != stack.end();
}

}  // namespace

uint8_t vcmdParamByteCount(uint8_t cmd) {
    switch (cmd) {
    case 0xE0:
        return 1;
    case 0xE1:
        return 1;
    case 0xE2:
        return 2;
    case 0xE3:
        return 3;
    case 0xE4:
        return 0;
    case 0xE5:
        return 1;
    case 0xE6:
        return 2;
    case 0xE7:
        return 1;
    case 0xE8:
        return 2;
    case 0xE9:
        return 1;
    case 0xEA:
        return 1;
    case 0xEB:
        return 3;
    case 0xEC:
        return 0;
    case 0xED:
        return 1;
    case 0xEE:
        return 2;
    case 0xEF:
        return 3;
    case 0xF0:
        return 1;
    case 0xF1:
        return 3;
    case 0xF2:
        return 3;
    case 0xF3:
        return 0;
    case 0xF4:
        return 1;
    case 0xF5:
        return 3;
    case 0xF6:
        return 0;
    case 0xF7:
        return 3;
    case 0xF8:
        return 3;
    case 0xF9:
        return 3;
    case 0xFA:
        return 1;
    case 0xFB:
        return 2;
    case 0xFC:
        return 0;
    case 0xFD:
        return 0;
    case 0xFE:
        return 0;
    case 0xFF:
        return 0;
    default:
        return 0;
    }
}

std::optional<Vcmd> constructVcmd(uint8_t id, const uint8_t* params) {
    switch (id) {
    case 0xE1:
        return Vcmd{VcmdPanning{params[0]}};
    case 0xE2:
        return Vcmd{VcmdPanFade{params[0], params[1]}};
    case 0xE3:
        return Vcmd{VcmdVibratoOn{params[0], params[1], params[2]}};
    case 0xE4:
        return Vcmd{VcmdVibratoOff{}};
    case 0xE5:
        return Vcmd{VcmdGlobalVolume{params[0]}};
    case 0xE6:
        return Vcmd{VcmdGlobalVolumeFade{params[0], params[1]}};
    case 0xE7:
        return Vcmd{VcmdTempo{params[0]}};
    case 0xE8:
        return Vcmd{VcmdTempoFade{params[0], params[1]}};
    case 0xE9:
        return Vcmd{VcmdGlobalTranspose{static_cast<int8_t>(params[0])}};
    case 0xEA:
        return Vcmd{VcmdPerVoiceTranspose{static_cast<int8_t>(params[0])}};
    case 0xEB:
        return Vcmd{VcmdTremoloOn{params[0], params[1], params[2]}};
    case 0xEC:
        return Vcmd{VcmdTremoloOff{}};
    case 0xEE:
        return Vcmd{VcmdVolumeFade{params[0], params[1]}};
    case 0xF0:
        return Vcmd{VcmdVibratoFadeIn{params[0]}};
    case 0xF1:
        return Vcmd{VcmdPitchEnvelopeTo{params[0], params[1], params[2]}};
    case 0xF2:
        return Vcmd{VcmdPitchEnvelopeFrom{params[0], params[1], params[2]}};
    case 0xF3:
        return Vcmd{VcmdPitchEnvelopeOff{}};
    case 0xF4:
        return Vcmd{VcmdFineTune{static_cast<int8_t>(params[0])}};
    case 0xF5:
        return Vcmd{VcmdEchoOn{params[0], params[1], params[2]}};
    case 0xF6:
        return Vcmd{VcmdEchoOff{}};
    case 0xF7:
        return Vcmd{VcmdEchoParams{params[0], params[1], params[2]}};
    case 0xF8:
        return Vcmd{VcmdEchoVolumeFade{params[0], params[1], params[2]}};
    case 0xF9:
        return Vcmd{VcmdPitchSlideToNote{params[0], params[1], params[2]}};
    case 0xFA:
        return Vcmd{VcmdPercussionBaseInstrument{params[0]}};
    case 0xFC:
        return Vcmd{VcmdMuteChannel{}};
    case 0xFD:
        return Vcmd{VcmdFastForwardOn{}};
    case 0xFE:
        return Vcmd{VcmdFastForwardOff{}};
    default:
        return std::nullopt;
    }
}

std::optional<Vcmd> constructVcmdForEngine(uint8_t id, const uint8_t* params, const NspcEngineConfig& engine) {
    if (const auto extensionParamCount = extensionVcmdParamByteCount(engine, id, true); extensionParamCount.has_value()) {
        VcmdExtension extension{};
        extension.id = id;
        extension.paramCount = *extensionParamCount;
        for (uint8_t i = 0; i < extension.paramCount && i < extension.params.size(); ++i) {
            extension.params[i] = params[i];
        }
        return Vcmd{extension};
    }

    return constructVcmd(id, params);
}

const char* vcmdNameForId(uint8_t id) {
    switch (id) {
    case VcmdInst::id:
        return VcmdInst::name.data();
    case VcmdPanning::id:
        return VcmdPanning::name.data();
    case VcmdPanFade::id:
        return VcmdPanFade::name.data();
    case VcmdVibratoOn::id:
        return VcmdVibratoOn::name.data();
    case VcmdVibratoOff::id:
        return VcmdVibratoOff::name.data();
    case VcmdGlobalVolume::id:
        return VcmdGlobalVolume::name.data();
    case VcmdGlobalVolumeFade::id:
        return VcmdGlobalVolumeFade::name.data();
    case VcmdTempo::id:
        return VcmdTempo::name.data();
    case VcmdTempoFade::id:
        return VcmdTempoFade::name.data();
    case VcmdGlobalTranspose::id:
        return VcmdGlobalTranspose::name.data();
    case VcmdPerVoiceTranspose::id:
        return VcmdPerVoiceTranspose::name.data();
    case VcmdTremoloOn::id:
        return VcmdTremoloOn::name.data();
    case VcmdTremoloOff::id:
        return VcmdTremoloOff::name.data();
    case VcmdVolume::id:
        return VcmdVolume::name.data();
    case VcmdVolumeFade::id:
        return VcmdVolumeFade::name.data();
    case VcmdSubroutineCall::id:
        return VcmdSubroutineCall::name.data();
    case VcmdVibratoFadeIn::id:
        return VcmdVibratoFadeIn::name.data();
    case VcmdPitchEnvelopeTo::id:
        return VcmdPitchEnvelopeTo::name.data();
    case VcmdPitchEnvelopeFrom::id:
        return VcmdPitchEnvelopeFrom::name.data();
    case VcmdPitchEnvelopeOff::id:
        return VcmdPitchEnvelopeOff::name.data();
    case VcmdFineTune::id:
        return VcmdFineTune::name.data();
    case VcmdEchoOn::id:
        return VcmdEchoOn::name.data();
    case VcmdEchoOff::id:
        return VcmdEchoOff::name.data();
    case VcmdEchoParams::id:
        return VcmdEchoParams::name.data();
    case VcmdEchoVolumeFade::id:
        return VcmdEchoVolumeFade::name.data();
    case VcmdPitchSlideToNote::id:
        return VcmdPitchSlideToNote::name.data();
    case VcmdPercussionBaseInstrument::id:
        return VcmdPercussionBaseInstrument::name.data();
    case VcmdNOP::id:
        return VcmdNOP::name.data();
    case VcmdMuteChannel::id:
        return VcmdMuteChannel::name.data();
    case VcmdFastForwardOn::id:
        return VcmdFastForwardOn::name.data();
    case VcmdFastForwardOff::id:
        return VcmdFastForwardOff::name.data();
    case VcmdUnused::id:
        return VcmdUnused::name.data();
    default:
        return nullptr;
    }
}

Vcmd NspcSong::parseVcmd(emulation::AramView aram, uint16_t& addr) {
    const uint8_t rawCmd = aram.read(addr++);
    const auto mappedCmd = mapReadVcmdId(commandMap_, rawCmd);
    if (!mappedCmd.has_value()) {
        throw std::runtime_error("Encountered unmapped raw VCMD");
    }
    const uint8_t cmd = *mappedCmd;

    switch (cmd) {
    case 0xE0:
        return Vcmd{VcmdInst{.instrumentIndex = aram.read(addr++)}};
    case 0xE1:
        return Vcmd{VcmdPanning{aram.read(addr++)}};
    case 0xE2: {
        uint8_t time = aram.read(addr++);
        uint8_t target = aram.read(addr++);
        return Vcmd{VcmdPanFade{time, target}};
    }
    case 0xE3: {
        uint8_t delay = aram.read(addr++);
        uint8_t rate = aram.read(addr++);
        uint8_t depth = aram.read(addr++);
        return Vcmd{VcmdVibratoOn{delay, rate, depth}};
    }
    case 0xE4:
        return Vcmd{VcmdVibratoOff{}};
    case 0xE5:
        return Vcmd{VcmdGlobalVolume{aram.read(addr++)}};
    case 0xE6: {
        uint8_t time = aram.read(addr++);
        uint8_t target = aram.read(addr++);
        return Vcmd{VcmdGlobalVolumeFade{time, target}};
    }
    case 0xE7: {
        uint8_t tempo = aram.read(addr++);
        return Vcmd{VcmdTempo{tempo}};
    }
    case 0xE8: {
        uint8_t time = aram.read(addr++);
        uint8_t target = aram.read(addr++);
        return Vcmd{VcmdTempoFade{time, target}};
    }
    case 0xE9:
        return Vcmd{VcmdGlobalTranspose{static_cast<int8_t>(aram.read(addr++))}};
    case 0xEA:
        return Vcmd{VcmdPerVoiceTranspose{static_cast<int8_t>(aram.read(addr++))}};
    case 0xEB: {
        uint8_t delay = aram.read(addr++);
        uint8_t rate = aram.read(addr++);
        uint8_t depth = aram.read(addr++);
        return Vcmd{VcmdTremoloOn{delay, rate, depth}};
    }
    case 0xEC:
        return Vcmd{VcmdTremoloOff{}};
    case 0xED:
        return Vcmd{VcmdVolume{aram.read(addr++)}};
    case 0xEE: {
        uint8_t time = aram.read(addr++);
        uint8_t target = aram.read(addr++);
        return Vcmd{VcmdVolumeFade{time, target}};
    }
    case 0xEF: {
        uint16_t subrAddr = aram.read16(addr);
        addr += 2;
        uint8_t count = aram.read(addr++);

        int subrId;
        if (!subroutineAddrToIndex_.contains(subrAddr)) {
            subrId = nextSubroutineId_++;
            subroutineAddrToIndex_[subrAddr] = subrId;
        } else {
            subrId = subroutineAddrToIndex_[subrAddr];
        }

        return Vcmd{VcmdSubroutineCall{subrId, subrAddr, count}};
    }
    case 0xF0:
        return Vcmd{VcmdVibratoFadeIn{aram.read(addr++)}};
    case 0xF1: {
        uint8_t delay = aram.read(addr++);
        uint8_t length = aram.read(addr++);
        uint8_t semitone = aram.read(addr++);
        return Vcmd{VcmdPitchEnvelopeTo{delay, length, semitone}};
    }
    case 0xF2: {
        uint8_t delay = aram.read(addr++);
        uint8_t length = aram.read(addr++);
        uint8_t semitone = aram.read(addr++);
        return Vcmd{VcmdPitchEnvelopeFrom{delay, length, semitone}};
    }
    case 0xF3:
        return Vcmd{VcmdPitchEnvelopeOff{}};
    case 0xF4:
        return Vcmd{VcmdFineTune{static_cast<int8_t>(aram.read(addr++))}};
    case 0xF5: {
        uint8_t channels = aram.read(addr++);
        uint8_t left = aram.read(addr++);
        uint8_t right = aram.read(addr++);
        return Vcmd{VcmdEchoOn{channels, left, right}};
    }
    case 0xF6:
        return Vcmd{VcmdEchoOff{}};
    case 0xF7: {
        uint8_t delay = aram.read(addr++);
        uint8_t feedback = aram.read(addr++);
        uint8_t firIndex = aram.read(addr++);
        return Vcmd{VcmdEchoParams{delay, feedback, firIndex}};
    }
    case 0xF8: {
        uint8_t time = aram.read(addr++);
        uint8_t leftTarget = aram.read(addr++);
        uint8_t rightTarget = aram.read(addr++);
        return Vcmd{VcmdEchoVolumeFade{time, leftTarget, rightTarget}};
    }
    case 0xF9: {
        uint8_t delay = aram.read(addr++);
        uint8_t length = aram.read(addr++);
        uint8_t note = aram.read(addr++);
        return Vcmd{VcmdPitchSlideToNote{delay, length, note}};
    }
    case 0xFA:
        return Vcmd{VcmdPercussionBaseInstrument{aram.read(addr++)}};
    case 0xFB: {
        uint16_t nopBytes = aram.read16(addr);
        addr += 2;
        return Vcmd{VcmdNOP{nopBytes}};
    }
    case 0xFC:
        return Vcmd{VcmdMuteChannel{}};
    case 0xFD:
        return Vcmd{VcmdFastForwardOn{}};
    case 0xFE:
        return Vcmd{VcmdFastForwardOff{}};
    default:
        throw std::runtime_error("Encountered unsupported VCMD");
    }
}

std::vector<NspcEventEntry> NspcSong::parseEvents(emulation::AramView aram, uint16_t startAddr, uint16_t& endAddr,
                                                  std::optional<uint16_t> hardStopExclusive) {
    std::vector<NspcEventEntry> events;
    uint16_t addr = startAddr;

    auto push_event = [&](NspcEvent event, uint16_t originalAddr) {
        events.push_back(NspcEventEntry{
            .id = nextEventId_++,
            .event = std::move(event),
            .originalAddr = originalAddr,
        });
    };

    while (true) {
        if (hardStopExclusive.has_value() && addr >= *hardStopExclusive) {
            endAddr = addr;
            break;
        }

        const uint16_t eventAddr = addr;
        const uint8_t byte = aram.read(addr);

        if (byte == 0x00) {
            push_event(End{}, eventAddr);
            endAddr = addr + 1;
            break;
        } else if (byte >= 0x01 && byte <= 0x7F) {
            // Duration byte - may have optional quantization and velocity
            Duration dur;
            dur.ticks = byte;
            addr++;

            const bool canReadNext = !(hardStopExclusive.has_value() && addr >= *hardStopExclusive);
            uint8_t next = canReadNext ? aram.read(addr) : 0;
            // In N-SPC, if the next byte is also in 0x01-0x7F range, it's a qv modifier
            // Quantization is bits 4-6 (0-7), velocity is bits 0-3 (0-15)
            if (canReadNext && next >= 0x01 && next <= 0x7F) {
                dur.quantization = (next >> 4) & 0x07;
                dur.velocity = next & 0x0F;
                addr++;
            }

            push_event(dur, eventAddr);
        } else if (byte >= commandMap_.noteStart && byte <= commandMap_.noteEnd) {
            // Note with pitch
            push_event(Note{static_cast<uint8_t>(byte - commandMap_.noteStart)}, eventAddr);
            addr++;
        } else if (byte == commandMap_.tie) {
            // Tie
            push_event(Tie{}, eventAddr);
            addr++;
        } else if (byte >= commandMap_.restStart && byte <= commandMap_.restEnd) {
            // Rest
            push_event(Rest{}, eventAddr);
            addr++;
        } else if (byte >= commandMap_.percussionStart && byte <= commandMap_.percussionEnd) {
            // Percussion note
            push_event(Percussion{static_cast<uint8_t>(byte - commandMap_.percussionStart)}, eventAddr);
            addr++;
        } else if (byte >= commandMap_.vcmdStart) {
            const auto translatedVcmd = mapReadVcmdId(commandMap_, byte);
            if (!translatedVcmd.has_value()) {
                throw std::runtime_error("Encountered unmapped raw VCMD in track data");
            }

            if (const auto extensionParamCount =
                    extensionParamCountForId(extensionParamCountById_, *translatedVcmd);
                extensionParamCount.has_value()) {
                const uint32_t neededBytes = 1u + *extensionParamCount;
                if (hardStopExclusive.has_value() &&
                    static_cast<uint32_t>(addr) + neededBytes > static_cast<uint32_t>(*hardStopExclusive)) {
                    endAddr = addr;
                    break;
                }

                VcmdExtension extension{};
                extension.id = *translatedVcmd;
                extension.paramCount = *extensionParamCount;
                for (uint8_t i = 0; i < extension.paramCount; ++i) {
                    extension.params[i] = aram.read(static_cast<uint16_t>(addr + 1u + i));
                }
                addr = static_cast<uint16_t>(static_cast<uint32_t>(addr) + neededBytes);
                push_event(Vcmd{extension}, eventAddr);
                continue;
            }

            if (*translatedVcmd == VcmdUnused::id) {
                throw std::runtime_error("Encountered unused VCMD in track data: " + std::to_string(*translatedVcmd));
            }
            if (hardStopExclusive.has_value()) {
                const uint32_t neededBytes = 1u + vcmdParamByteCount(*translatedVcmd);
                const uint32_t limit = static_cast<uint32_t>(*hardStopExclusive);
                if (static_cast<uint32_t>(addr) + neededBytes > limit) {
                    endAddr = addr;
                    break;
                }
            }
            // VCMD
            Vcmd vcmd = parseVcmd(aram, addr);
            push_event(vcmd, eventAddr);
        } else {
            // Unknown byte, skip
            addr++;
        }

        // Safety check to prevent infinite loops
        if (addr < startAddr || (hardStopExclusive.has_value() && addr >= *hardStopExclusive)) {
            endAddr = addr;
            break;
        }
    }

    return events;
}

void NspcSong::parseTrack(emulation::AramView aram, uint16_t trackAddr, int trackIndex,
                          std::optional<uint16_t> hardStopExclusive) {
    if (trackAddr == 0) {
        return;
    }

    // Check if we've already parsed this track
    if (static_cast<size_t>(trackIndex) < tracks_.size() && tracks_[trackIndex].originalAddr == trackAddr) {
        return;
    }

    uint16_t endAddr;
    std::vector<NspcEventEntry> events = parseEvents(aram, trackAddr, endAddr, hardStopExclusive);

    // Ensure we have space for this track
    if (static_cast<size_t>(trackIndex) >= tracks_.size()) {
        tracks_.resize(trackIndex + 1);
    }

    tracks_[trackIndex] = NspcTrack{trackIndex, std::move(events), trackAddr};
}

void NspcSong::parsePattern(emulation::AramView aram, uint16_t patternAddr, int patternIndex) {
    // Each pattern has 8 track pointers (16 bytes total)
    std::array<int, 8> channelTrackIds{};

    for (int ch = 0; ch < 8; ch++) {
        uint16_t trackAddr = aram.read16(patternAddr + ch * 2);

        if (trackAddr == 0) {
            channelTrackIds[ch] = -1;  // No track for this channel
            continue;
        }

        int trackId;
        if (!trackAddrToIndex_.contains(trackAddr)) {
            trackId = nextTrackId_++;
            trackAddrToIndex_[trackAddr] = trackId;
        } else {
            trackId = trackAddrToIndex_[trackAddr];
        }

        channelTrackIds[ch] = trackId;
    }

    // Ensure we have space for this pattern
    if (static_cast<size_t>(patternIndex) >= patterns_.size()) {
        patterns_.resize(patternIndex + 1);
    }

    patterns_[patternIndex] = NspcPattern{patternIndex, channelTrackIds, patternAddr};
}

NspcSong NspcSong::createEmpty(int songId) {
    NspcSong song;
    song.songId_ = songId;
    song.contentOrigin_ = NspcContentOrigin::UserProvided;
    song.sequence_.push_back(PlayPattern{
        .patternId = 0,
        .trackTableAddr = 0,
    });
    song.sequence_.push_back(EndSequence{});
    song.patterns_.push_back(NspcPattern{
        .id = 0,
        .channelTrackIds = std::array<int, 8>{-1, -1, -1, -1, -1, -1, -1, -1},
        .trackTableAddr = 0,
    });
    song.nextPatternId_ = 1;
    return song;
}

NspcSong::NspcSong(emulation::AramView aram, const NspcEngineConfig& config, int songIndex)
    : songId_(songIndex), commandMap_(config.commandMap.value_or(NspcCommandMap{})) {
    for (const auto& extension : config.extensions) {
        if (!extension.enabled) {
            continue;
        }
        for (const auto& vcmd : extension.vcmds) {
            extensionParamCountById_[vcmd.id] = static_cast<uint8_t>(std::min<uint8_t>(vcmd.paramCount, 4u));
        }
    }

    uint16_t seqPointer = aram.read16(config.songIndexPointers + songIndex * 2);

    if (seqPointer == 0) {
        throw std::runtime_error("Invalid song index: pointer is null");
    }

    std::unordered_map<uint16_t, int> sequenceAddrToIndex;
    std::unordered_map<uint16_t, int> patternAddrToIndex;

    // Parse song sequence data
    sequence_.clear();
    loopPatternIndex_.reset();
    while (true) {
        const uint16_t opAddr = seqPointer;
        const int opIndex = static_cast<int>(sequence_.size());
        sequenceAddrToIndex[opAddr] = opIndex;

        uint16_t seqWord = aram.read16(seqPointer);

        if (seqWord == 0x0000) {
            // End of song
            sequence_.push_back(EndSequence{});
            break;
        } else if ((seqWord & 0xFF00) == 0x0000) {
            // Special commands when high byte is 0
            uint8_t lowByte = seqWord & 0xFF;

            if (lowByte >= 0x01 && lowByte <= 0x7F) {
                // Jump X times
                uint16_t jumpAddr = aram.read16(seqPointer + 2);
                std::optional<int> targetIndex = std::nullopt;
                if (const auto it = sequenceAddrToIndex.find(jumpAddr); it != sequenceAddrToIndex.end()) {
                    targetIndex = it->second;
                }
                sequence_.push_back(JumpTimes{lowByte, SequenceTarget{targetIndex, jumpAddr}});
                seqPointer += 4;
            } else if (lowByte == 0x80) {
                // Fast forward on
                sequence_.push_back(FastForwardOn{});
                seqPointer += 2;
            } else if (lowByte == 0x81) {
                // Fast forward off
                sequence_.push_back(FastForwardOff{});
                seqPointer += 2;
            } else if (lowByte >= 0x82) {
                // Conditional jump (always jump unless fast-forward)
                uint16_t jumpAddr = aram.read16(seqPointer + 2);
                std::optional<int> targetIndex = std::nullopt;
                if (const auto it = sequenceAddrToIndex.find(jumpAddr); it != sequenceAddrToIndex.end()) {
                    targetIndex = it->second;
                }
                sequence_.push_back(AlwaysJump{lowByte, SequenceTarget{targetIndex, jumpAddr}});
                seqPointer += 4;
            } else {
                // Unknown, skip
                seqPointer += 2;
            }
        } else {
            // Pattern pointer - the word is the address of the track table
            uint16_t patternAddr = seqWord;

            int patternIndex;
            if (!patternAddrToIndex.contains(patternAddr)) {
                patternIndex = nextPatternId_++;
                patternAddrToIndex[patternAddr] = patternIndex;
            } else {
                patternIndex = patternAddrToIndex[patternAddr];
            }

            sequence_.push_back(PlayPattern{patternIndex, patternAddr});
            seqPointer += 2;
        }
    }

    // Resolve jump targets now that all sequence row addresses are known.
    for (auto& op : sequence_) {
        if (auto* jump = std::get_if<JumpTimes>(&op)) {
            if (const auto it = sequenceAddrToIndex.find(jump->target.addr); it != sequenceAddrToIndex.end()) {
                jump->target.index = it->second;
            }
            continue;
        }

        if (auto* always = std::get_if<AlwaysJump>(&op)) {
            if (const auto it = sequenceAddrToIndex.find(always->target.addr); it != sequenceAddrToIndex.end()) {
                always->target.index = it->second;
                // Keep the previous behavior of tracking the latest always-jump destination.
                loopPatternIndex_ = it->second;
            }
        }
    }

    // Parse all patterns and their tracks
    for (const auto& [patternAddr, patternIndex] : patternAddrToIndex) {
        parsePattern(aram, patternAddr, patternIndex);
    }

    // Parse all tracks
    std::vector<std::pair<uint16_t, int>> trackEntries;
    trackEntries.reserve(trackAddrToIndex_.size());
    for (const auto& [trackAddr, trackIndex] : trackAddrToIndex_) {
        trackEntries.emplace_back(trackAddr, trackIndex);
    }
    std::sort(trackEntries.begin(), trackEntries.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    for (size_t i = 0; i < trackEntries.size(); ++i) {
        const uint16_t trackAddr = trackEntries[i].first;
        const int trackIndex = trackEntries[i].second;

        std::optional<uint16_t> hardStopExclusive = std::nullopt;
        if (i + 1 < trackEntries.size()) {
            hardStopExclusive = trackEntries[i + 1].first;
        }

        parseTrack(aram, trackAddr, trackIndex, hardStopExclusive);
    }

    // Parse subroutines discovered during track parsing. parseEvents() can discover additional
    // subroutines, so iterate over a stable snapshot each pass to avoid mutating the map while
    // iterating it.
    std::unordered_set<uint16_t> parsedSubroutines;
    while (true) {
        std::vector<std::pair<uint16_t, int>> pendingSubroutines;
        pendingSubroutines.reserve(subroutineAddrToIndex_.size());
        for (const auto& [subrAddr, subrIndex] : subroutineAddrToIndex_) {
            if (!parsedSubroutines.contains(subrAddr)) {
                pendingSubroutines.emplace_back(subrAddr, subrIndex);
            }
        }

        if (pendingSubroutines.empty()) {
            break;
        }

        std::sort(pendingSubroutines.begin(), pendingSubroutines.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });

        for (const auto& [subrAddr, subrIndex] : pendingSubroutines) {
            parsedSubroutines.insert(subrAddr);

            uint16_t endAddr;
            std::vector<NspcEventEntry> events = parseEvents(aram, subrAddr, endAddr);

            if (static_cast<size_t>(subrIndex) >= subroutines_.size()) {
                subroutines_.resize(subrIndex + 1);
            }

            subroutines_[subrIndex] = NspcSubroutine{subrIndex, std::move(events), subrAddr};
        }
    }
}

const NspcEvent* NspcSong::resolveEvent(const NspcEventRef& ref) const {
    const auto* entry = resolveEventEntry(tracks_, subroutines_, ref);
    if (!entry) {
        return nullptr;
    }
    return &entry->event;
}

NspcEvent* NspcSong::resolveEvent(const NspcEventRef& ref) {
    auto* entry = resolveEventEntry(tracks_, subroutines_, ref);
    if (!entry) {
        return nullptr;
    }
    return &entry->event;
}

bool NspcSong::replaceEvent(const NspcEventRef& ref, const NspcEvent& replacement) {
    auto* event = resolveEvent(ref);
    if (!event) {
        return false;
    }
    *event = replacement;
    return true;
}

void NspcSong::flattenSubroutines() {
    if (tracks_.empty()) {
        subroutines_.clear();
        subroutineAddrToIndex_.clear();
        nextSubroutineId_ = 0;
        return;
    }

    NspcEventId nextId = nextEventIdForSong(tracks_, subroutines_);
    auto cloneWithNewId = [&](const NspcEventEntry& entry) {
        NspcEventEntry clone = entry;
        clone.id = nextId++;
        return clone;
    };

    // Go though all tracks
    for (auto &track : tracks_) {
        auto flatEvents = std::vector<NspcEventEntry>{};
        for (const auto &entry : track.events) {
            if (std::holds_alternative<Vcmd>(entry.event) &&
                std::holds_alternative<VcmdSubroutineCall>(std::get<Vcmd>(entry.event).vcmd)) {
                const auto &subCall = std::get<VcmdSubroutineCall>(std::get<Vcmd>(entry.event).vcmd);
                const auto subrIt = std::find_if(subroutines_.begin(), subroutines_.end(),
                                                 [&](const NspcSubroutine &subr) { return subr.id == subCall.subroutineId; });
                if (subrIt != subroutines_.end()) {
                    for(int i = 0; i < subCall.count; ++i) {
                        // Skip the last end event in the subroutine since it's not needed when inlining
                        for (size_t j = 0; j < subrIt->events.size(); ++j) {
                            if (j == subrIt->events.size() - 1 && std::holds_alternative<End>(subrIt->events[j].event)) {
                                continue;
                            }
                            flatEvents.push_back(cloneWithNewId(subrIt->events[j]));
                        }
                    }
                    continue;
                }
            } else {
                // Not a subroutine call, keep the event as is
                flatEvents.push_back(entry);
            }
        }
        track.events = std::move(flatEvents);
    }

    // Clear subroutines since they've been inlined
    subroutines_.clear();
    subroutineAddrToIndex_.clear();
    nextSubroutineId_ = 0;
    nextEventId_ = nextId;
}


}  // namespace ntrak::nspc
