#include "ntrak/nspc/NspcCompileShared.hpp"

#include <algorithm>
#include <format>
#include <iterator>
#include <map>
#include <utility>

namespace ntrak::nspc {
using namespace compile_detail;

std::expected<NspcUploadList, std::string> buildUserContentUpload(NspcProject& project, NspcBuildOptions options) {
    NspcUploadList upload;
    bool hasUserContent = false;
    const auto& engine = project.engineConfig();
    const bool includeEngineExtensions = options.includeEngineExtensions;
    NspcBuildOptions songBuildOptions = options;
    songBuildOptions.includeEngineExtensions = false;
    const uint8_t instrumentEntrySize = std::clamp<uint8_t>(engine.instrumentEntryBytes, 5, 6);
    const uint8_t percEntrySize = std::clamp<uint8_t>(engine.percussionEntryBytes, 6, 7);
    const bool isSmwV00Engine = (engine.engineVersion == "0.0");
    const auto commandMap = engine.commandMap.value_or(NspcCommandMap{});
    const int percussionCount = static_cast<int>(commandMap.percussionEnd) -
                                static_cast<int>(commandMap.percussionStart) + 1;
    const auto rangeEndDisplay = [](uint32_t endExclusive) -> uint16_t {
        if (endExclusive == 0) {
            return 0;
        }
        return static_cast<uint16_t>(std::min<uint32_t>(endExclusive - 1u, 0xFFFFu));
    };

    struct UserSampleBrrRange {
        int sampleId = -1;
        uint16_t from = 0;
        uint32_t to = 0;
        const std::vector<uint8_t>* data = nullptr;
    };

    std::vector<UserSampleBrrRange> userSampleBrrRanges;
    userSampleBrrRanges.reserve(project.samples().size());

    if (engine.instrumentHeaders != 0) {
        for (auto& instrument : project.instruments()) {
            if (instrument.contentOrigin != NspcContentOrigin::UserProvided || instrument.id < 0 ||
                instrument.originalAddr != 0 || instrument.songId.has_value()) {
                continue;
            }
            const uint32_t address = static_cast<uint32_t>(engine.instrumentHeaders) +
                                     static_cast<uint32_t>(instrument.id) * static_cast<uint32_t>(instrumentEntrySize);
            if (address + instrumentEntrySize <= kAramSize) {
                instrument.originalAddr = static_cast<uint16_t>(address);
            }
        }
    }
    project.refreshAramUsage();

    for (size_t songIndex = 0; songIndex < project.songs().size(); ++songIndex) {
        if (!project.songs()[songIndex].isUserProvided()) {
            continue;
        }

        auto songCompile = buildSongScopedUpload(project, static_cast<int>(songIndex), songBuildOptions);
        if (!songCompile.has_value()) {
            return std::unexpected(
                std::format("Failed to compile user song {:02X}: {}", songIndex, songCompile.error()));
        }

        hasUserContent = true;
        auto& songChunks = songCompile->upload.chunks;
        upload.chunks.insert(upload.chunks.end(), std::make_move_iterator(songChunks.begin()),
                             std::make_move_iterator(songChunks.end()));
    }

    for (const auto& instrument : project.instruments()) {
        if (instrument.contentOrigin != NspcContentOrigin::UserProvided) {
            continue;
        }
        // Custom instruments (those with a songId) are emitted directly after their song's
        // sequence data by buildSongScopedUpload — skip them here.
        if (instrument.songId.has_value()) {
            continue;
        }
        if (engine.instrumentHeaders == 0) {
            return std::unexpected("Engine config has no instrument table for user-provided instruments");
        }
        if (instrument.id < 0) {
            return std::unexpected("User-provided instrument has a negative id");
        }

        const uint32_t address = static_cast<uint32_t>(engine.instrumentHeaders) +
                                 static_cast<uint32_t>(instrument.id) * static_cast<uint32_t>(instrumentEntrySize);
        const uint32_t end = address + instrumentEntrySize;
        if (end > kAramSize) {
            return std::unexpected(std::format("Instrument {:02X} table write at ${:04X} exceeds ARAM bounds",
                                               instrument.id, static_cast<uint16_t>(address & 0xFFFFu)));
        }

        std::vector<uint8_t> bytes;
        bytes.reserve(instrumentEntrySize);
        bytes.push_back(instrument.sampleIndex);
        bytes.push_back(instrument.adsr1);
        bytes.push_back(instrument.adsr2);
        bytes.push_back(instrument.gain);
        bytes.push_back(instrument.basePitchMult);
        if (instrumentEntrySize >= 6) {
            bytes.push_back(instrument.fracPitchMult);
        }

        upload.chunks.push_back(NspcUploadChunk{
            .address = static_cast<uint16_t>(address),
            .bytes = std::move(bytes),
            .label = std::format("Instrument {:02X}", instrument.id),
        });

        if (isSmwV00Engine && engine.percussionHeaders != 0 && instrument.id >= 0 && instrument.id < percussionCount) {
            const uint32_t percussionAddress = static_cast<uint32_t>(engine.percussionHeaders) +
                                               static_cast<uint32_t>(instrument.id) * percEntrySize;
            if (percussionAddress + percEntrySize > kAramSize) {
                return std::unexpected(std::format("Percussion instrument {:02X} write at ${:04X} exceeds ARAM bounds",
                                                   instrument.id, static_cast<uint16_t>(percussionAddress & 0xFFFFu)));
            }

            std::vector<uint8_t> percussionBytes;
            percussionBytes.reserve(percEntrySize);
            percussionBytes.push_back(instrument.sampleIndex);
            percussionBytes.push_back(instrument.adsr1);
            percussionBytes.push_back(instrument.adsr2);
            percussionBytes.push_back(instrument.gain);
            percussionBytes.push_back(instrument.basePitchMult);
            if (percEntrySize >= 7) {
                percussionBytes.push_back(instrument.fracPitchMult);
            }
            percussionBytes.push_back(instrument.percussionNote);

            upload.chunks.push_back(NspcUploadChunk{
                .address = static_cast<uint16_t>(percussionAddress),
                .bytes = std::move(percussionBytes),
                .label = std::format("Percussion {:02X}", instrument.id),
            });
        }
        hasUserContent = true;
    }

    for (const auto& sample : project.samples()) {
        if (sample.contentOrigin != NspcContentOrigin::UserProvided) {
            continue;
        }
        if (sample.id < 0) {
            return std::unexpected("User-provided sample has a negative id");
        }
        if (sample.data.empty()) {
            return std::unexpected(std::format("User sample {:02X} has empty BRR data", sample.id));
        }
        if (sample.originalAddr == 0) {
            return std::unexpected(std::format("User sample {:02X} has no ARAM start address", sample.id));
        }
        const uint32_t sampleEnd = static_cast<uint32_t>(sample.originalAddr) +
                                   static_cast<uint32_t>(sample.data.size());
        if (sampleEnd > kAramSize) {
            return std::unexpected(
                std::format("User sample {:02X} data at ${:04X} exceeds ARAM bounds", sample.id, sample.originalAddr));
        }
        if (engine.sampleHeaders == 0) {
            return std::unexpected("Engine config has no sample directory for user-provided samples");
        }

        const uint32_t directoryAddr = static_cast<uint32_t>(engine.sampleHeaders) +
                                       static_cast<uint32_t>(sample.id) * 4u;
        if (directoryAddr + 4u > kAramSize) {
            return std::unexpected(std::format("Sample {:02X} directory entry at ${:04X} exceeds ARAM bounds",
                                               sample.id, static_cast<uint16_t>(directoryAddr & 0xFFFFu)));
        }

        std::vector<uint8_t> sampleDirectoryBytes;
        sampleDirectoryBytes.reserve(4);
        appendU16(sampleDirectoryBytes, sample.originalAddr);
        appendU16(sampleDirectoryBytes, sample.originalLoopAddr);
        upload.chunks.push_back(NspcUploadChunk{
            .address = static_cast<uint16_t>(directoryAddr),
            .bytes = std::move(sampleDirectoryBytes),
            .label = std::format("Sample {:02X} Directory", sample.id),
        });

        bool skipBrrUpload = false;
        for (const auto& existingRange : userSampleBrrRanges) {
            const bool overlaps = static_cast<uint32_t>(sample.originalAddr) < existingRange.to &&
                                  static_cast<uint32_t>(existingRange.from) < sampleEnd;
            if (!overlaps) {
                continue;
            }

            const bool exactAlias =
                static_cast<uint32_t>(sample.originalAddr) == static_cast<uint32_t>(existingRange.from) &&
                sampleEnd == existingRange.to && existingRange.data != nullptr && sample.data == *existingRange.data;
            if (exactAlias) {
                skipBrrUpload = true;
                break;
            }

            return std::unexpected(std::format(
                "User sample {:02X} BRR at ${:04X}-${:04X} overlaps user sample {:02X} BRR at ${:04X}-${:04X}",
                sample.id, sample.originalAddr, rangeEndDisplay(sampleEnd), existingRange.sampleId, existingRange.from,
                rangeEndDisplay(existingRange.to)));
        }

        if (!skipBrrUpload) {
            upload.chunks.push_back(NspcUploadChunk{
                .address = sample.originalAddr,
                .bytes = sample.data,
                .label = std::format("Sample {:02X} BRR", sample.id),
            });
            userSampleBrrRanges.push_back(UserSampleBrrRange{
                .sampleId = sample.id,
                .from = sample.originalAddr,
                .to = sampleEnd,
                .data = &sample.data,
            });
        }
        hasUserContent = true;
    }

    if (includeEngineExtensions) {
        auto extensionChunks = buildEnabledEngineExtensionPatchChunks(engine);
        if (!extensionChunks.empty()) {
            hasUserContent = true;
            upload.chunks.insert(upload.chunks.end(), std::make_move_iterator(extensionChunks.begin()),
                                 std::make_move_iterator(extensionChunks.end()));
        }
    }

    if (!hasUserContent) {
        return std::unexpected("Project has no user-provided content to export");
    }

    sortUploadChunksByAddress(upload.chunks, true);
    if (auto validated = validateUploadChunkBoundsAndOverlap(upload.chunks, false); !validated.has_value()) {
        return std::unexpected(validated.error());
    }

    return upload;
}

}  // namespace ntrak::nspc
