#include "NspcCompileShared.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <unordered_map>

namespace ntrak::nspc {
using namespace compile_detail;

std::expected<NspcRoundTripReport, std::string> verifySongRoundTrip(const NspcProject& project, int songIndex) {
    const auto& songs = project.songs();
    if (songIndex < 0 || songIndex >= static_cast<int>(songs.size())) {
        return std::unexpected(std::format("Song index {} is out of range", songIndex));
    }

    const auto& song = songs[static_cast<size_t>(songIndex)];
    const auto& sequence = song.sequence();
    const auto& engine = project.engineConfig();
    if (engine.songIndexPointers == 0) {
        return std::unexpected("Engine config has no song index pointer table");
    }

    const auto aram = project.aram();
    const auto sequenceAddrOpt = readSongSequencePointer(aram, engine, static_cast<size_t>(songIndex));
    if (!sequenceAddrOpt.has_value() || *sequenceAddrOpt == 0 || *sequenceAddrOpt == 0xFFFF) {
        return std::unexpected("Selected song has no valid sequence pointer in index table");
    }
    const uint16_t sequenceAddr = *sequenceAddrOpt;

    NspcRoundTripReport report;
    report.equivalent = true;

    std::unordered_map<int, uint16_t> patternAddrById;
    patternAddrById.reserve(song.patterns().size());
    for (const auto& pattern : song.patterns()) {
        patternAddrById[pattern.id] = pattern.trackTableAddr;
    }

    std::unordered_map<int, uint16_t> trackAddrById;
    trackAddrById.reserve(song.tracks().size());
    for (const auto& track : song.tracks()) {
        trackAddrById[track.id] = track.originalAddr;
    }

    std::unordered_map<int, uint16_t> subroutineAddrById;
    subroutineAddrById.reserve(song.subroutines().size());
    for (const auto& subroutine : song.subroutines()) {
        subroutineAddrById[subroutine.id] = subroutine.originalAddr;
    }

    std::vector<uint32_t> sequenceOffsets(sequence.size(), 0);
    uint32_t sequenceSize = 0;
    for (size_t i = 0; i < sequence.size(); ++i) {
        sequenceOffsets[i] = sequenceSize;
        sequenceSize += sequenceOpSize(sequence[i]);
        if (sequenceSize > kAramSize) {
            return std::unexpected("Sequence data exceeds ARAM bounds during verification");
        }
    }

    std::vector<uint8_t> rebuiltSequence;
    rebuiltSequence.reserve(sequenceSize);
    for (size_t i = 0; i < sequence.size(); ++i) {
        const auto& op = sequence[i];
        std::visit(nspc::overloaded{
                       [&](const PlayPattern& value) {
                           uint16_t patternAddr = value.trackTableAddr;
                           if (const auto it = patternAddrById.find(value.patternId); it != patternAddrById.end()) {
                               patternAddr = it->second;
                           }
                           appendU16(rebuiltSequence, patternAddr);
                       },
                       [&](const JumpTimes& value) {
                           appendU16(rebuiltSequence, static_cast<uint16_t>(std::clamp<int>(value.count, 1, 0x7F)));
                           uint16_t targetAddr = value.target.addr;
                           if (value.target.index.has_value()) {
                               const int idx = *value.target.index;
                               if (idx >= 0 && idx < static_cast<int>(sequenceOffsets.size())) {
                                   targetAddr = static_cast<uint16_t>(static_cast<uint32_t>(sequenceAddr) +
                                                                      sequenceOffsets[static_cast<size_t>(idx)]);
                               }
                           }
                           appendU16(rebuiltSequence, targetAddr);
                       },
                       [&](const AlwaysJump& value) {
                           appendU16(rebuiltSequence, static_cast<uint16_t>(std::clamp<int>(value.opcode, 0x82, 0xFF)));
                           uint16_t targetAddr = value.target.addr;
                           if (value.target.index.has_value()) {
                               const int idx = *value.target.index;
                               if (idx >= 0 && idx < static_cast<int>(sequenceOffsets.size())) {
                                   targetAddr = static_cast<uint16_t>(static_cast<uint32_t>(sequenceAddr) +
                                                                      sequenceOffsets[static_cast<size_t>(idx)]);
                               }
                           }
                           appendU16(rebuiltSequence, targetAddr);
                       },
                       [&](const FastForwardOn&) { appendU16(rebuiltSequence, 0x0080); },
                       [&](const FastForwardOff&) { appendU16(rebuiltSequence, 0x0081); },
                       [&](const EndSequence&) { appendU16(rebuiltSequence, 0x0000); },
                   },
                   op);
    }
    if (rebuiltSequence.empty()) {
        rebuiltSequence.push_back(0x00);
    }

    auto originalSequence = readAramBytes(aram, sequenceAddr, rebuiltSequence.size(),
                                          std::format("Song {:02X} Sequence", songIndex));
    if (!originalSequence.has_value()) {
        return std::unexpected(originalSequence.error());
    }
    const auto sequenceMask = buildSequencePointerMask(sequence, rebuiltSequence.size());
    compareBinaryObject(std::format("Song {:02X} Sequence", songIndex), *originalSequence, rebuiltSequence,
                        sequenceMask, report);

    for (const auto& pattern : song.patterns()) {
        if (pattern.trackTableAddr == 0) {
            continue;
        }

        std::vector<uint8_t> rebuiltPattern;
        rebuiltPattern.reserve(16);
        const std::array<int, 8> trackIds =
            pattern.channelTrackIds.value_or(std::array<int, 8>{-1, -1, -1, -1, -1, -1, -1, -1});
        for (const int trackId : trackIds) {
            uint16_t trackAddr = 0;
            if (trackId >= 0) {
                if (const auto it = trackAddrById.find(trackId); it != trackAddrById.end()) {
                    trackAddr = it->second;
                }
            }
            appendU16(rebuiltPattern, trackAddr);
        }

        auto originalPattern = readAramBytes(aram, pattern.trackTableAddr, rebuiltPattern.size(),
                                             std::format("Pattern {:02X}", pattern.id));
        if (!originalPattern.has_value()) {
            return std::unexpected(originalPattern.error());
        }
        const auto patternMask = buildPatternPointerMask(rebuiltPattern.size());
        compareBinaryObject(std::format("Pattern {:02X}", pattern.id), *originalPattern, rebuiltPattern, patternMask,
                            report);
    }

    for (const auto& track : song.tracks()) {
        if (track.originalAddr == 0) {
            continue;
        }
        std::vector<std::string> warnings;
        auto rebuiltTrack = encodeEventStream(track.events, subroutineAddrById, warnings, engine);
        if (!rebuiltTrack.has_value()) {
            return std::unexpected(
                std::format("Failed to encode track {:02X} during verification: {}", track.id, rebuiltTrack.error()));
        }
        if (rebuiltTrack->empty()) {
            rebuiltTrack->push_back(0x00);
        }

        auto originalTrack = readAramBytes(aram, track.originalAddr, rebuiltTrack->size(),
                                           std::format("Track {:02X}", track.id));
        if (!originalTrack.has_value()) {
            return std::unexpected(originalTrack.error());
        }
        const auto trackMask = buildStreamPointerMask(track.events, rebuiltTrack->size());
        compareBinaryObject(std::format("Track {:02X}", track.id), *originalTrack, *rebuiltTrack, trackMask, report);
    }

    for (const auto& subroutine : song.subroutines()) {
        if (subroutine.originalAddr == 0) {
            continue;
        }
        std::vector<std::string> warnings;
        auto rebuiltSub = encodeEventStream(subroutine.events, subroutineAddrById, warnings, engine);
        if (!rebuiltSub.has_value()) {
            return std::unexpected(std::format("Failed to encode subroutine {:02X} during verification: {}",
                                               subroutine.id, rebuiltSub.error()));
        }
        if (rebuiltSub->empty()) {
            rebuiltSub->push_back(0x00);
        }

        auto originalSub = readAramBytes(aram, subroutine.originalAddr, rebuiltSub->size(),
                                         std::format("Subroutine {:02X}", subroutine.id));
        if (!originalSub.has_value()) {
            return std::unexpected(originalSub.error());
        }
        const auto subMask = buildStreamPointerMask(subroutine.events, rebuiltSub->size());
        compareBinaryObject(std::format("Subroutine {:02X}", subroutine.id), *originalSub, *rebuiltSub, subMask,
                            report);
    }

    report.equivalent = (report.differingBytes == 0);
    report.messages.insert(report.messages.begin(),
                           std::format("Roundtrip {} | objects={} bytes={} diffs={} (ignored pointer diffs={})",
                                       report.equivalent ? "OK" : "FAILED", report.objectsCompared,
                                       report.bytesCompared, report.differingBytes, report.pointerDifferencesIgnored));

    return report;
}

}  // namespace ntrak::nspc
