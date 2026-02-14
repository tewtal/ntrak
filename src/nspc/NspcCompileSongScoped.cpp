#include "NspcCompileShared.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <unordered_map>

namespace ntrak::nspc {
using namespace compile_detail;

std::expected<NspcCompileOutput, std::string> buildSongScopedUpload(NspcProject& project, int songIndex,
                                                                    NspcBuildOptions options) {
    auto& songs = project.songs();
    if (songIndex < 0 || songIndex >= static_cast<int>(songs.size())) {
        return std::unexpected(std::format("Song index {} is out of range", songIndex));
    }

    const auto& engine = project.engineConfig();
    NspcSong song = songs[static_cast<size_t>(songIndex)];
    const auto& sequence = song.sequence();
    if (sequence.empty()) {
        return std::unexpected("Selected song has an empty sequence");
    }

    if (engine.songIndexPointers == 0) {
        return std::unexpected("Engine config has no song index pointer table");
    }

    if (options.optimizeSubroutines) {
        nspc::optimizeSongSubroutines(song, options.optimizerOptions);
    }
    const bool persistOptimizedSong = options.optimizeSubroutines && options.applyOptimizedSongToProject;

    const auto aram = project.aram();
    const uint32_t songIndexEntryAddr32 = static_cast<uint32_t>(engine.songIndexPointers) +
                                          static_cast<uint32_t>(songIndex) * 2u;
    if (songIndexEntryAddr32 + 1u >= kAramSize) {
        return std::unexpected(std::format("Song index {} table entry is outside ARAM at ${:04X}", songIndex,
                                           static_cast<uint16_t>(songIndexEntryAddr32 & 0xFFFFu)));
    }
    const uint16_t songIndexEntryAddr = static_cast<uint16_t>(songIndexEntryAddr32);

    const int songId = song.songId();
    const NspcSongAddressLayout* activeLayout = project.songAddressLayout(songId);

    std::optional<uint16_t> preferredSequenceAddr = std::nullopt;
    if (!options.compactAramLayout) {
        if (activeLayout && activeLayout->sequenceAddr != 0) {
            preferredSequenceAddr = activeLayout->sequenceAddr;
        } else {
            const auto sequenceAddrOpt = readSongSequencePointer(aram, engine, static_cast<size_t>(songIndex));
            if (sequenceAddrOpt.has_value() && *sequenceAddrOpt != 0 && *sequenceAddrOpt != 0xFFFF) {
                preferredSequenceAddr = *sequenceAddrOpt;
            }
        }
    }

    std::vector<std::string> warnings;

    project.refreshAramUsage();
    const auto& aramUsage = project.aramUsage();

    std::vector<AddressRange> blockedRanges;
    addClampedRange(blockedRanges, 0, 1);  // Null pointer value should never be allocated.

    for (const auto& region : aramUsage.regions) {
        if (isRelocatableSongRegion(region, songId)) {
            continue;
        }
        addClampedRange(blockedRanges, region.from, region.to);
    }

    normalizeRanges(blockedRanges);
    std::vector<AddressRange> freeRanges = invertRanges(blockedRanges);
    if (freeRanges.empty()) {
        return std::unexpected("No writable ARAM ranges available for song-scoped upload");
    }

    std::unordered_map<int, uint16_t> originalSubroutineAddrById;
    originalSubroutineAddrById.reserve(song.subroutines().size());
    for (const auto& subroutine : song.subroutines()) {
        uint16_t subroutineAddr = subroutine.originalAddr;
        if (activeLayout) {
            const auto it = activeLayout->subroutineAddrById.find(subroutine.id);
            if (it != activeLayout->subroutineAddrById.end() && it->second != 0) {
                subroutineAddr = it->second;
            }
        }
        originalSubroutineAddrById[subroutine.id] = subroutineAddr;
    }

    std::unordered_map<int, uint32_t> trackSizeById;
    trackSizeById.reserve(song.tracks().size());
    for (const auto& track : song.tracks()) {
        std::vector<std::string> sizingWarnings;
        auto encoded = encodeEventStream(track.events, originalSubroutineAddrById, sizingWarnings, engine);
        if (!encoded.has_value()) {
            return std::unexpected(std::format("Failed to encode track {}: {}", track.id, encoded.error()));
        }
        if (encoded->empty()) {
            warnings.push_back(std::format("Track {} encoded to 0 bytes; forcing End marker", track.id));
        }
        trackSizeById[track.id] = std::max<uint32_t>(1u, static_cast<uint32_t>(encoded->size()));
    }

    std::unordered_map<int, uint32_t> subroutineSizeById;
    subroutineSizeById.reserve(song.subroutines().size());
    for (const auto& subroutine : song.subroutines()) {
        std::vector<std::string> sizingWarnings;
        auto encoded = encodeEventStream(subroutine.events, originalSubroutineAddrById, sizingWarnings, engine);
        if (!encoded.has_value()) {
            return std::unexpected(std::format("Failed to encode subroutine {}: {}", subroutine.id, encoded.error()));
        }
        if (encoded->empty()) {
            warnings.push_back(std::format("Subroutine {} encoded to 0 bytes; forcing End marker", subroutine.id));
        }
        subroutineSizeById[subroutine.id] = std::max<uint32_t>(1u, static_cast<uint32_t>(encoded->size()));
    }

    uint32_t sequenceSize = 0;
    for (const auto& op : sequence) {
        sequenceSize += sequenceOpSize(op);
        if (sequenceSize > kAramSize) {
            return std::unexpected("Sequence data exceeds ARAM addressable range");
        }
    }
    sequenceSize = std::max<uint32_t>(1u, sequenceSize);

    std::vector<AllocRequest> allocRequests;
    allocRequests.reserve(1 + song.patterns().size() + song.tracks().size() + song.subroutines().size());

    allocRequests.push_back(AllocRequest{
        .kind = AllocObjectKind::Sequence,
        .id = -1,
        .preferredAddr = preferredSequenceAddr,
        .size = sequenceSize,
        .label = std::format("Song {:02X} Sequence", songIndex),
    });

    for (const auto& pattern : song.patterns()) {
        std::optional<uint16_t> preferredAddr;
        if (!options.compactAramLayout) {
            if (activeLayout) {
                const auto it = activeLayout->patternAddrById.find(pattern.id);
                if (it != activeLayout->patternAddrById.end() && it->second != 0) {
                    preferredAddr = it->second;
                }
            }
            if (!preferredAddr.has_value() && pattern.trackTableAddr != 0) {
                preferredAddr = pattern.trackTableAddr;
            }
        }
        allocRequests.push_back(AllocRequest{
            .kind = AllocObjectKind::Pattern,
            .id = pattern.id,
            .preferredAddr = preferredAddr,
            .size = 16u,
            .label = std::format("Pattern {:02X} TrackTable", pattern.id),
        });
    }

    for (const auto& track : song.tracks()) {
        const auto sizeIt = trackSizeById.find(track.id);
        if (sizeIt == trackSizeById.end()) {
            return std::unexpected(std::format("Missing size estimate for track {}", track.id));
        }
        std::optional<uint16_t> preferredAddr;
        if (!options.compactAramLayout) {
            if (activeLayout) {
                const auto it = activeLayout->trackAddrById.find(track.id);
                if (it != activeLayout->trackAddrById.end() && it->second != 0) {
                    preferredAddr = it->second;
                }
            }
            if (!preferredAddr.has_value() && track.originalAddr != 0) {
                preferredAddr = track.originalAddr;
            }
        }
        allocRequests.push_back(AllocRequest{
            .kind = AllocObjectKind::Track,
            .id = track.id,
            .preferredAddr = preferredAddr,
            .size = sizeIt->second,
            .label = std::format("Track {:02X}", track.id),
        });
    }

    for (const auto& subroutine : song.subroutines()) {
        const auto sizeIt = subroutineSizeById.find(subroutine.id);
        if (sizeIt == subroutineSizeById.end()) {
            return std::unexpected(std::format("Missing size estimate for subroutine {}", subroutine.id));
        }
        std::optional<uint16_t> preferredAddr;
        if (!options.compactAramLayout) {
            if (activeLayout) {
                const auto it = activeLayout->subroutineAddrById.find(subroutine.id);
                if (it != activeLayout->subroutineAddrById.end() && it->second != 0) {
                    preferredAddr = it->second;
                }
            }
            if (!preferredAddr.has_value() && subroutine.originalAddr != 0) {
                preferredAddr = subroutine.originalAddr;
            }
        }
        allocRequests.push_back(AllocRequest{
            .kind = AllocObjectKind::Subroutine,
            .id = subroutine.id,
            .preferredAddr = preferredAddr,
            .size = sizeIt->second,
            .label = std::format("Subroutine {:02X}", subroutine.id),
        });
    }

    std::stable_sort(allocRequests.begin(), allocRequests.end(), [](const AllocRequest& lhs, const AllocRequest& rhs) {
        if (lhs.preferredAddr.has_value() != rhs.preferredAddr.has_value()) {
            return lhs.preferredAddr.has_value();
        }
        if (lhs.preferredAddr.has_value() && rhs.preferredAddr.has_value() &&
            lhs.preferredAddr.value() != rhs.preferredAddr.value()) {
            return lhs.preferredAddr.value() < rhs.preferredAddr.value();
        }
        if (lhs.size != rhs.size) {
            return lhs.size > rhs.size;
        }
        if (lhs.kind != rhs.kind) {
            return static_cast<uint8_t>(lhs.kind) < static_cast<uint8_t>(rhs.kind);
        }
        return lhs.id < rhs.id;
    });

    uint16_t sequenceAddr = 0;
    std::unordered_map<int, uint16_t> patternAddrById;
    std::unordered_map<int, uint16_t> trackAddrById;
    std::unordered_map<int, uint16_t> subroutineAddrById;
    patternAddrById.reserve(song.patterns().size());
    trackAddrById.reserve(song.tracks().size());
    subroutineAddrById.reserve(song.subroutines().size());

    for (const auto& request : allocRequests) {
        const auto allocatedAddr = allocateFromFreeRanges(freeRanges, request.size, request.preferredAddr);
        if (!allocatedAddr.has_value()) {
            const uint32_t freeBytes = totalRangeBytes(freeRanges);
            std::string rangeInfo;
            for (const auto& range : freeRanges) {
                rangeInfo += std::format(" ${:04X}-${:04X}({} bytes)", range.from, range.to, range.to - range.from);
            }
            return std::unexpected(
                std::format("Out of ARAM while allocating {} (needs {} bytes, {} bytes still free in {} ranges:{})",
                            request.label, request.size, freeBytes, freeRanges.size(), rangeInfo));
        }

        switch (request.kind) {
        case AllocObjectKind::Sequence:
            sequenceAddr = *allocatedAddr;
            break;
        case AllocObjectKind::Pattern:
            patternAddrById[request.id] = *allocatedAddr;
            break;
        case AllocObjectKind::Track:
            trackAddrById[request.id] = *allocatedAddr;
            break;
        case AllocObjectKind::Subroutine:
            subroutineAddrById[request.id] = *allocatedAddr;
            break;
        }
    }

    if (sequenceAddr == 0) {
        return std::unexpected("Failed to allocate sequence address");
    }

    NspcUploadList upload;
    if (options.includeEngineExtensions) {
        auto extensionChunks = buildEnabledEngineExtensionPatchChunks(engine);
        upload.chunks.insert(upload.chunks.end(), std::make_move_iterator(extensionChunks.begin()),
                             std::make_move_iterator(extensionChunks.end()));
    }

    std::vector<uint32_t> sequenceOffsets(sequence.size(), 0);
    uint32_t sequenceRunningSize = 0;
    for (size_t i = 0; i < sequence.size(); ++i) {
        sequenceOffsets[i] = sequenceRunningSize;
        sequenceRunningSize += sequenceOpSize(sequence[i]);
    }

    std::vector<uint8_t> sequenceBytes;
    sequenceBytes.reserve(sequenceRunningSize);
    for (size_t i = 0; i < sequence.size(); ++i) {
        const auto& op = sequence[i];
        std::visit(
            nspc::overloaded{
                [&](const PlayPattern& value) {
                    uint16_t patternAddr = value.trackTableAddr;
                    const auto patternIt = patternAddrById.find(value.patternId);
                    if (patternIt != patternAddrById.end()) {
                        patternAddr = patternIt->second;
                    } else if (patternAddr == 0) {
                        warnings.push_back(std::format(
                            "Sequence PlayPattern id {} has no track table address; writing null", value.patternId));
                    } else {
                        warnings.push_back(std::format(
                            "Sequence PlayPattern id {} missing from pattern list; using stored address ${:04X}",
                            value.patternId, patternAddr));
                    }
                    appendU16(sequenceBytes, patternAddr);
                },
                [&](const JumpTimes& value) {
                    appendU16(sequenceBytes, static_cast<uint16_t>(std::clamp<int>(value.count, 1, 0x7F)));
                    uint16_t targetAddr = value.target.addr;
                    if (value.target.index.has_value()) {
                        const int targetIndex = *value.target.index;
                        if (targetIndex >= 0 && targetIndex < static_cast<int>(sequenceOffsets.size())) {
                            targetAddr = static_cast<uint16_t>(static_cast<uint32_t>(sequenceAddr) +
                                                               sequenceOffsets[static_cast<size_t>(targetIndex)]);
                        } else {
                            warnings.push_back(std::format(
                                "JumpTimes target index {} is out of sequence range; using stored address ${:04X}",
                                targetIndex, targetAddr));
                        }
                    }
                    appendU16(sequenceBytes, targetAddr);
                },
                [&](const AlwaysJump& value) {
                    appendU16(sequenceBytes, static_cast<uint16_t>(std::clamp<int>(value.opcode, 0x82, 0xFF)));
                    uint16_t targetAddr = value.target.addr;
                    if (value.target.index.has_value()) {
                        const int targetIndex = *value.target.index;
                        if (targetIndex >= 0 && targetIndex < static_cast<int>(sequenceOffsets.size())) {
                            targetAddr = static_cast<uint16_t>(static_cast<uint32_t>(sequenceAddr) +
                                                               sequenceOffsets[static_cast<size_t>(targetIndex)]);
                        } else {
                            warnings.push_back(std::format(
                                "AlwaysJump target index {} is out of sequence range; using stored address ${:04X}",
                                targetIndex, targetAddr));
                        }
                    }
                    appendU16(sequenceBytes, targetAddr);
                },
                [&](const FastForwardOn&) { appendU16(sequenceBytes, 0x0080); },
                [&](const FastForwardOff&) { appendU16(sequenceBytes, 0x0081); },
                [&](const EndSequence&) { appendU16(sequenceBytes, 0x0000); },
            },
            op);
    }
    if (sequenceBytes.empty()) {
        sequenceBytes.push_back(0x00);
        warnings.push_back("Sequence encoded to 0 bytes; inserted End marker");
    }

    upload.chunks.push_back(NspcUploadChunk{
        .address = sequenceAddr,
        .bytes = std::move(sequenceBytes),
        .label = std::format("Song {:02X} Sequence", songIndex),
    });

    for (const auto& pattern : song.patterns()) {
        const auto patternAddrIt = patternAddrById.find(pattern.id);
        if (patternAddrIt == patternAddrById.end()) {
            return std::unexpected(std::format("Pattern {} was not allocated an address", pattern.id));
        }

        std::vector<uint8_t> bytes;
        bytes.reserve(16);
        const std::array<int, 8> trackIds =
            pattern.channelTrackIds.value_or(std::array<int, 8>{-1, -1, -1, -1, -1, -1, -1, -1});

        for (const int trackId : trackIds) {
            uint16_t trackAddr = 0;
            if (trackId >= 0) {
                const auto trackIt = trackAddrById.find(trackId);
                if (trackIt != trackAddrById.end()) {
                    trackAddr = trackIt->second;
                } else {
                    warnings.push_back(std::format("Pattern {} references missing track id {}; writing null pointer",
                                                   pattern.id, trackId));
                }
            }
            appendU16(bytes, trackAddr);
        }

        upload.chunks.push_back(NspcUploadChunk{
            .address = patternAddrIt->second,
            .bytes = std::move(bytes),
            .label = std::format("Pattern {:02X} TrackTable", pattern.id),
        });
    }

    for (const auto& track : song.tracks()) {
        const auto trackAddrIt = trackAddrById.find(track.id);
        if (trackAddrIt == trackAddrById.end()) {
            return std::unexpected(std::format("Track {} was not allocated an address", track.id));
        }

        auto encoded = encodeEventStream(track.events, subroutineAddrById, warnings, engine);
        if (!encoded.has_value()) {
            return std::unexpected(std::format("Failed to encode track {}: {}", track.id, encoded.error()));
        }
        if (encoded->empty()) {
            encoded->push_back(0x00);
            warnings.push_back(std::format("Track {} encoded to 0 bytes; inserted End marker", track.id));
        }

        upload.chunks.push_back(NspcUploadChunk{
            .address = trackAddrIt->second,
            .bytes = std::move(*encoded),
            .label = std::format("Track {:02X}", track.id),
        });
    }

    for (const auto& subroutine : song.subroutines()) {
        const auto subroutineAddrIt = subroutineAddrById.find(subroutine.id);
        if (subroutineAddrIt == subroutineAddrById.end()) {
            return std::unexpected(std::format("Subroutine {} was not allocated an address", subroutine.id));
        }

        auto encoded = encodeEventStream(subroutine.events, subroutineAddrById, warnings, engine);
        if (!encoded.has_value()) {
            return std::unexpected(std::format("Failed to encode subroutine {}: {}", subroutine.id, encoded.error()));
        }
        if (encoded->empty()) {
            encoded->push_back(0x00);
            warnings.push_back(std::format("Subroutine {} encoded to 0 bytes; inserted End marker", subroutine.id));
        }

        upload.chunks.push_back(NspcUploadChunk{
            .address = subroutineAddrIt->second,
            .bytes = std::move(*encoded),
            .label = std::format("Subroutine {:02X}", subroutine.id),
        });
    }

    std::vector<uint8_t> songIndexBytes;
    songIndexBytes.reserve(2);
    appendU16(songIndexBytes, sequenceAddr);
    upload.chunks.push_back(NspcUploadChunk{
        .address = songIndexEntryAddr,
        .bytes = std::move(songIndexBytes),
        .label = std::format("Song {:02X} IndexPtr", songIndex),
    });

    sortUploadChunksByAddress(upload.chunks, false);
    if (auto validated = validateUploadChunkBoundsAndOverlap(upload.chunks, true); !validated.has_value()) {
        return std::unexpected(validated.error());
    }

    NspcSongAddressLayout newLayout;
    newLayout.sequenceAddr = sequenceAddr;
    newLayout.patternAddrById = patternAddrById;
    newLayout.trackAddrById = trackAddrById;
    newLayout.subroutineAddrById = subroutineAddrById;
    newLayout.trackSizeById = trackSizeById;
    newLayout.subroutineSizeById = subroutineSizeById;
    if (persistOptimizedSong) {
        songs[static_cast<size_t>(songIndex)] = song;
    }
    project.setSongAddressLayout(songId, std::move(newLayout));
    project.refreshAramUsage();

    return NspcCompileOutput{
        .upload = std::move(upload),
        .warnings = std::move(warnings),
    };
}

}  // namespace ntrak::nspc
