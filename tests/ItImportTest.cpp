#include "ntrak/nspc/ItImport.hpp"
#include "ntrak/nspc/BrrCodec.hpp"

#include "NspcTestHelpers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ntrak::nspc {
namespace {

using test_helpers::buildProjectWithTwoSongsTwoAssets;

void writeU16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
    if (offset + 1 >= bytes.size()) {
        bytes.resize(offset + 2, 0);
    }
    bytes[offset] = static_cast<uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
}

void writeU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    if (offset + 3 >= bytes.size()) {
        bytes.resize(offset + 4, 0);
    }
    bytes[offset] = static_cast<uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
    bytes[offset + 2] = static_cast<uint8_t>((value >> 16u) & 0xFFu);
    bytes[offset + 3] = static_cast<uint8_t>((value >> 24u) & 0xFFu);
}

uint16_t readU16(const std::vector<uint8_t>& bytes, size_t offset) {
    if (offset + 1 >= bytes.size()) {
        return 0;
    }
    return static_cast<uint16_t>(bytes[offset] | (static_cast<uint16_t>(bytes[offset + 1]) << 8u));
}

uint32_t readU32(const std::vector<uint8_t>& bytes, size_t offset) {
    if (offset + 3 >= bytes.size()) {
        return 0;
    }
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8u) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16u) | (static_cast<uint32_t>(bytes[offset + 3]) << 24u);
}

void writeString(std::vector<uint8_t>& bytes, size_t offset, std::string_view text, size_t maxLen) {
    if (offset + maxLen > bytes.size()) {
        bytes.resize(offset + maxLen, 0);
    }
    const size_t len = std::min(maxLen, text.size());
    for (size_t i = 0; i < len; ++i) {
        bytes[offset + i] = static_cast<uint8_t>(text[i]);
    }
}

struct ItFixtureOptions {
    bool includeArpeggio = false;
    bool includeHighChannel = false;
    uint8_t arpeggioValue = 0x37;
    std::optional<std::pair<uint8_t, uint8_t>> row0Effect = std::nullopt;
    int rows = 4;
    int patternCount = 1;
    std::vector<uint8_t> orders = {0x00, 0xFF};
    uint8_t sampleFlags = 0x02;
    uint8_t sampleConversion = 1;
    uint8_t sampleGlobalVolume = 64;
    uint8_t sampleDefaultVolume = 64;
    uint32_t sampleLoopBegin = 0;
    std::optional<uint32_t> sampleLoopEnd = std::nullopt;
    std::vector<int16_t> samplePcm16 = {0, 2500, -2500, 1200, -1200, 500, -500, 0, 0, 1200, -1200, 600, -600, 0, 0, 0};
    std::vector<uint8_t> samplePcm8 = {};
    uint8_t initialChannelVolume = 64;
    bool includePortamentoDownBeforeSecondNote = false;
    bool includePortamentoUpBeforeSecondNote = false;
    bool includeNotePortamentoBeforeSecondNote = false;
    bool applyPreNotePortamentoOnSecondNoteRow = false;
    uint8_t preNotePortamentoValue = 0x20;
    uint8_t secondNote = 62;
    std::vector<uint8_t> patternPackedOverride = {};
    std::optional<int> patternRowsOverride = std::nullopt;
    bool instrumentUseEnvelope = false;
    bool instrumentSustainLoop = false;
    uint16_t instrumentFadeOut = 0;
    uint8_t instrumentGlobalVolume = 64;
    std::vector<std::pair<uint8_t, uint16_t>> instrumentEnvelopeNodes = {};
};

std::vector<uint8_t> buildMinimalItFile(const ItFixtureOptions& options = {}) {
    constexpr size_t kHeaderSize = 0xC0;
    std::vector<uint8_t> out(kHeaderSize, 0);

    writeString(out, 0x00, "IMPM", 4);
    writeString(out, 0x04, "UnitTest IT", 26);

    const uint16_t orderCount = static_cast<uint16_t>(options.orders.size());
    const uint16_t instrumentCount = 1;
    const uint16_t sampleCount = 1;
    const uint16_t patternCount = static_cast<uint16_t>(std::clamp(options.patternCount, 1, 0xFFFF));

    writeU16(out, 0x20, orderCount);
    writeU16(out, 0x22, instrumentCount);
    writeU16(out, 0x24, sampleCount);
    writeU16(out, 0x26, patternCount);
    out[0x30] = 128;  // global volume
    out[0x32] = 6;    // speed
    out[0x33] = 125;  // tempo

    // Default pan/vol for channels.
    for (int i = 0; i < 64; ++i) {
        out[0x40 + i] = 32;
        out[0x80 + i] = options.initialChannelVolume;
    }

    size_t cursor = kHeaderSize;
    out.resize(cursor + options.orders.size(), 0);
    for (size_t i = 0; i < options.orders.size(); ++i) {
        out[cursor + i] = options.orders[i];
    }
    cursor += options.orders.size();

    const size_t instrumentOffsetTable = cursor;
    out.resize(cursor + instrumentCount * 4u, 0);
    cursor += instrumentCount * 4u;

    const size_t sampleOffsetTable = cursor;
    out.resize(cursor + sampleCount * 4u, 0);
    cursor += sampleCount * 4u;

    const size_t patternOffsetTable = cursor;
    out.resize(cursor + patternCount * 4u, 0);
    cursor += patternCount * 4u;

    // Instrument block.
    const uint32_t instrumentOffset = static_cast<uint32_t>(cursor);
    out.resize(cursor + 0x200u, 0);
    writeString(out, cursor + 0x00u, "IMPI", 4);
    writeString(out, cursor + 0x20u, "Inst 1", 26);
    writeU16(out, cursor + 0x14u, options.instrumentFadeOut);
    out[cursor + 0x18u] = options.instrumentGlobalVolume;
    out[cursor + 0x40u + 121u] = 1;        // sample map
    uint8_t envFlags = 0;
    if (options.instrumentUseEnvelope) {
        envFlags |= 0x01u;
    }
    if (options.instrumentSustainLoop) {
        envFlags |= 0x04u;
    }
    out[cursor + 0x130u] = envFlags;
    const size_t nodeCount = std::min<size_t>(25, options.instrumentEnvelopeNodes.size());
    out[cursor + 0x131u] = static_cast<uint8_t>(nodeCount);
    for (size_t i = 0; i < nodeCount; ++i) {
        const size_t nodeOffset = cursor + 0x136u + (i * 3u);
        out[nodeOffset] = options.instrumentEnvelopeNodes[i].first;
        writeU16(out, nodeOffset + 1u, options.instrumentEnvelopeNodes[i].second);
    }
    cursor += 0x200u;

    // Sample block + PCM.
    const uint32_t sampleOffset = static_cast<uint32_t>(cursor);
    const bool sixteenBitSample = (options.sampleFlags & 0x02u) != 0u;
    const std::vector<int16_t>& pcm16 = options.samplePcm16;
    const std::vector<uint8_t>& pcm8 = options.samplePcm8;
    const uint32_t pcmSampleCount =
        sixteenBitSample ? static_cast<uint32_t>(pcm16.size()) : static_cast<uint32_t>(pcm8.size());
    const uint32_t pcmOffset = static_cast<uint32_t>(cursor + 0x50u);
    out.resize(cursor + 0x50u + (sixteenBitSample ? (pcm16.size() * 2u) : pcm8.size()), 0);
    writeString(out, cursor + 0x00u, "IMPS", 4);
    writeString(out, cursor + 0x14u, "Sample 1", 26);
    out[cursor + 0x11u] = options.sampleGlobalVolume;
    out[cursor + 0x12u] = options.sampleFlags;
    out[cursor + 0x13u] = options.sampleDefaultVolume;
    out[cursor + 0x2Eu] = options.sampleConversion;
    writeU32(out, cursor + 0x30u, pcmSampleCount);
    writeU32(out, cursor + 0x34u, options.sampleLoopBegin);
    writeU32(out, cursor + 0x38u, options.sampleLoopEnd.value_or(pcmSampleCount));
    writeU32(out, cursor + 0x3Cu, 8363);
    writeU32(out, cursor + 0x48u, pcmOffset);
    size_t sampleDataCursor = static_cast<size_t>(pcmOffset);
    if (sixteenBitSample) {
        for (const int16_t sample : pcm16) {
            writeU16(out, sampleDataCursor, static_cast<uint16_t>(sample));
            sampleDataCursor += 2;
        }
    } else {
        for (const uint8_t sample : pcm8) {
            out[sampleDataCursor] = sample;
            ++sampleDataCursor;
        }
    }
    cursor += 0x50u + (sixteenBitSample ? (pcm16.size() * 2u) : pcm8.size());

    // Pattern block.
    std::vector<uint8_t> packed;
    int patternRows = options.patternRowsOverride.value_or(options.rows);
    if (!options.patternPackedOverride.empty()) {
        packed = options.patternPackedOverride;
    } else {
        packed.reserve(64);

        // Row 0, channel 0: note/instrument/volume/(optional effect).
        const bool hasRow0Effect = options.row0Effect.has_value() || options.includeArpeggio;
        const uint8_t row0EffectCommand = options.row0Effect.has_value() ? options.row0Effect->first : 10u;  // J
        const uint8_t row0EffectValue = options.row0Effect.has_value() ? options.row0Effect->second : options.arpeggioValue;
        packed.push_back(0x81);  // channel 0 + new mask
        packed.push_back(hasRow0Effect ? 0x0F : 0x07);
        packed.push_back(60);   // note
        packed.push_back(1);    // instrument
        packed.push_back(64);   // volume
        if (hasRow0Effect) {
            packed.push_back(row0EffectCommand);
            packed.push_back(row0EffectValue);
        }

        if (options.includeHighChannel) {
            packed.push_back(0x89);  // channel 8 + new mask
            packed.push_back(0x07);  // note/instrument/volume
            packed.push_back(64);
            packed.push_back(1);
            packed.push_back(64);
        }

        packed.push_back(0);  // end row 0

        int row = 1;
        const bool includePreNotePortamento =
            options.includePortamentoDownBeforeSecondNote || options.includePortamentoUpBeforeSecondNote ||
            options.includeNotePortamentoBeforeSecondNote;
        uint8_t preNoteCommand = 5; // E
        if (options.includeNotePortamentoBeforeSecondNote) {
            preNoteCommand = 7; // G
        } else if (options.includePortamentoUpBeforeSecondNote) {
            preNoteCommand = 6; // F
        }

        if (includePreNotePortamento && row < patternRows) {
            if (options.applyPreNotePortamentoOnSecondNoteRow) {
                packed.push_back(0x81);  // channel 0 + new mask
                packed.push_back(0x09);  // note + effect
                packed.push_back(options.secondNote);
                packed.push_back(preNoteCommand);
                packed.push_back(options.preNotePortamentoValue);
                packed.push_back(0);  // end row
                ++row;
            } else {
                packed.push_back(0x81);  // channel 0 + new mask
                packed.push_back(0x08);  // effect only
                packed.push_back(preNoteCommand);
                packed.push_back(options.preNotePortamentoValue);
                packed.push_back(0);  // end row
                ++row;

                if (row < patternRows) {
                    packed.push_back(0x81);  // channel 0 + new mask
                    packed.push_back(0x01);  // note only
                    packed.push_back(options.secondNote);
                    packed.push_back(0);  // end row
                    ++row;
                }
            }
        }

        for (; row < patternRows; ++row) {
            packed.push_back(0);
        }
    }

    std::vector<uint32_t> patternOffsets;
    patternOffsets.reserve(patternCount);
    for (uint16_t i = 0; i < patternCount; ++i) {
        const uint32_t patternOffset = static_cast<uint32_t>(cursor);
        patternOffsets.push_back(patternOffset);
        out.resize(cursor + 8u + packed.size(), 0);
        writeU16(out, cursor + 0x00u, static_cast<uint16_t>(packed.size()));
        writeU16(out, cursor + 0x02u, static_cast<uint16_t>(patternRows));
        std::copy(packed.begin(), packed.end(), out.begin() + static_cast<std::ptrdiff_t>(cursor + 8u));
        cursor += 8u + packed.size();
    }

    writeU32(out, instrumentOffsetTable, instrumentOffset);
    writeU32(out, sampleOffsetTable, sampleOffset);
    for (uint16_t i = 0; i < patternCount; ++i) {
        writeU32(out, patternOffsetTable + static_cast<size_t>(i) * 4u, patternOffsets[static_cast<size_t>(i)]);
    }

    return out;
}

std::filesystem::path uniqueTempPath(std::string_view stem, std::string_view ext) {
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    return tempDir / std::format("{}-{}.{}", stem, tick, ext);
}

std::filesystem::path writeItFixture(const std::vector<uint8_t>& bytes, std::string_view stem) {
    const auto path = uniqueTempPath(stem, "it");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

NspcEngineConfig baseConfig() {
    NspcEngineConfig config{};
    config.name = "IT import test";
    config.entryPoint = 0x1234;
    config.sampleHeaders = 0x0200;
    config.instrumentHeaders = 0x0300;
    config.songIndexPointers = 0x0400;
    config.instrumentEntryBytes = 6;
    return config;
}

bool songContainsExtensionVcmd(const NspcSong& song, uint8_t id) {
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (!vcmd || !std::holds_alternative<VcmdExtension>(vcmd->vcmd)) {
                continue;
            }
            if (std::get<VcmdExtension>(vcmd->vcmd).id == id) {
                return true;
            }
        }
    }
    return false;
}

bool songHasAnyExtensionVcmd(const NspcSong& song) {
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (!vcmd) {
                continue;
            }
            if (std::holds_alternative<VcmdExtension>(vcmd->vcmd)) {
                return true;
            }
        }
    }
    return false;
}

std::vector<uint8_t> collectExtensionParam0ById(const NspcSong& song, uint8_t id) {
    std::vector<uint8_t> params;
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd == nullptr || !std::holds_alternative<VcmdExtension>(vcmd->vcmd)) {
                continue;
            }
            const auto& extension = std::get<VcmdExtension>(vcmd->vcmd);
            if (extension.id != id || extension.paramCount < 1) {
                continue;
            }
            params.push_back(extension.params[0]);
        }
    }
    return params;
}

bool songContainsPitchSlideToNote(const NspcSong& song) {
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd == nullptr) {
                continue;
            }
            if (std::holds_alternative<VcmdPitchSlideToNote>(vcmd->vcmd)) {
                return true;
            }
        }
    }
    return false;
}

std::optional<VcmdPitchSlideToNote> findFirstPitchSlideToNote(const NspcSong& song) {
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd == nullptr) {
                continue;
            }
            if (!std::holds_alternative<VcmdPitchSlideToNote>(vcmd->vcmd)) {
                continue;
            }
            return std::get<VcmdPitchSlideToNote>(vcmd->vcmd);
        }
    }
    return std::nullopt;
}

std::vector<VcmdPitchSlideToNote> collectPitchSlideToNoteCommands(const NspcSong& song) {
    std::vector<VcmdPitchSlideToNote> slides;
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd == nullptr || !std::holds_alternative<VcmdPitchSlideToNote>(vcmd->vcmd)) {
                continue;
            }
            slides.push_back(std::get<VcmdPitchSlideToNote>(vcmd->vcmd));
        }
    }
    return slides;
}

std::vector<uint8_t> collectTempoCommands(const NspcSong& song) {
    std::vector<uint8_t> tempos;
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd == nullptr || !std::holds_alternative<VcmdTempo>(vcmd->vcmd)) {
                continue;
            }
            tempos.push_back(std::get<VcmdTempo>(vcmd->vcmd).tempo);
        }
    }
    return tempos;
}

std::vector<uint8_t> collectGlobalVolumeCommands(const NspcSong& song) {
    std::vector<uint8_t> globalVolumes;
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd == nullptr || !std::holds_alternative<VcmdGlobalVolume>(vcmd->vcmd)) {
                continue;
            }
            globalVolumes.push_back(std::get<VcmdGlobalVolume>(vcmd->vcmd).volume);
        }
    }
    return globalVolumes;
}

std::vector<VcmdVibratoOn> collectVibratoOnCommands(const NspcSong& song) {
    std::vector<VcmdVibratoOn> vibratos;
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd == nullptr || !std::holds_alternative<VcmdVibratoOn>(vcmd->vcmd)) {
                continue;
            }
            vibratos.push_back(std::get<VcmdVibratoOn>(vcmd->vcmd));
        }
    }
    return vibratos;
}

std::optional<VcmdVolumeFade> findFirstVolumeFade(const NspcSong& song) {
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd == nullptr || !std::holds_alternative<VcmdVolumeFade>(vcmd->vcmd)) {
                continue;
            }
            return std::get<VcmdVolumeFade>(vcmd->vcmd);
        }
    }
    return std::nullopt;
}

int countVolumeFades(const NspcSong& song) {
    int count = 0;
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd != nullptr && std::holds_alternative<VcmdVolumeFade>(vcmd->vcmd)) {
                ++count;
            }
        }
    }
    return count;
}

int countVolumeCommands(const NspcSong& song) {
    int count = 0;
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd != nullptr && std::holds_alternative<VcmdVolume>(vcmd->vcmd)) {
                ++count;
            }
        }
    }
    return count;
}

std::optional<uint8_t> findVolumeTargetBeforeNthNote(const NspcSong& song, int noteOrdinal) {
    for (const auto& track : song.tracks()) {
        int notesSeen = 0;
        for (size_t i = 0; i < track.events.size(); ++i) {
            if (!std::holds_alternative<Note>(track.events[i].event)) {
                continue;
            }
            ++notesSeen;
            if (notesSeen != noteOrdinal) {
                continue;
            }

            size_t cursor = i;
            while (cursor > 0) {
                --cursor;
                const auto& event = track.events[cursor].event;
                if (std::holds_alternative<Note>(event) || std::holds_alternative<Tie>(event) ||
                    std::holds_alternative<Rest>(event) || std::holds_alternative<Percussion>(event) ||
                    std::holds_alternative<End>(event)) {
                    break;
                }
                const auto* vcmd = std::get_if<Vcmd>(&event);
                if (vcmd == nullptr) {
                    continue;
                }
                if (std::holds_alternative<VcmdVolume>(vcmd->vcmd)) {
                    return std::get<VcmdVolume>(vcmd->vcmd).volume;
                }
                if (std::holds_alternative<VcmdVolumeFade>(vcmd->vcmd)) {
                    return std::get<VcmdVolumeFade>(vcmd->vcmd).target;
                }
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::pair<bool, bool> findVolumeAndFadeBeforeNthNote(const NspcSong& song, int noteOrdinal) {
    for (const auto& track : song.tracks()) {
        int notesSeen = 0;
        for (size_t i = 0; i < track.events.size(); ++i) {
            if (!std::holds_alternative<Note>(track.events[i].event)) {
                continue;
            }
            ++notesSeen;
            if (notesSeen != noteOrdinal) {
                continue;
            }

            bool hasVolume = false;
            bool hasFade = false;
            size_t cursor = i;
            while (cursor > 0) {
                --cursor;
                const auto& event = track.events[cursor].event;
                if (std::holds_alternative<Note>(event) || std::holds_alternative<Tie>(event) ||
                    std::holds_alternative<Rest>(event) || std::holds_alternative<Percussion>(event) ||
                    std::holds_alternative<End>(event)) {
                    break;
                }
                const auto* vcmd = std::get_if<Vcmd>(&event);
                if (vcmd == nullptr) {
                    continue;
                }
                hasVolume = hasVolume || std::holds_alternative<VcmdVolume>(vcmd->vcmd);
                hasFade = hasFade || std::holds_alternative<VcmdVolumeFade>(vcmd->vcmd);
            }
            return {hasVolume, hasFade};
        }
    }
    return {false, false};
}

bool songContainsVolumeFadeWithTime(const NspcSong& song, uint8_t time) {
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd == nullptr || !std::holds_alternative<VcmdVolumeFade>(vcmd->vcmd)) {
                continue;
            }
            if (std::get<VcmdVolumeFade>(vcmd->vcmd).time == time) {
                return true;
            }
        }
    }
    return false;
}

bool songHasClusteredVolumeVcmds(const NspcSong& song) {
    for (const auto& track : song.tracks()) {
        enum class VolumeCmdKind { Volume, Fade };
        std::vector<VolumeCmdKind> cluster;
        cluster.reserve(4);

        auto clusterIsProblematic = [&]() {
            if (cluster.size() <= 1) {
                return false;
            }
            // Keep Volume->VolumeFade as a valid pair: fade start level must be explicit.
            if (cluster.size() == 2 && cluster[0] == VolumeCmdKind::Volume && cluster[1] == VolumeCmdKind::Fade) {
                return false;
            }
            return true;
        };

        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd == nullptr) {
                if (clusterIsProblematic()) {
                    return true;
                }
                cluster.clear();
                continue;
            }
            if (std::holds_alternative<VcmdVolume>(vcmd->vcmd)) {
                cluster.push_back(VolumeCmdKind::Volume);
            } else if (std::holds_alternative<VcmdVolumeFade>(vcmd->vcmd)) {
                cluster.push_back(VolumeCmdKind::Fade);
            } else {
                if (clusterIsProblematic()) {
                    return true;
                }
                cluster.clear();
            }
        }
        if (clusterIsProblematic()) {
            return true;
        }
    }
    return false;
}

int countVibratoOn(const NspcSong& song) {
    int count = 0;
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd != nullptr && std::holds_alternative<VcmdVibratoOn>(vcmd->vcmd)) {
                ++count;
            }
        }
    }
    return count;
}

int countTremoloOn(const NspcSong& song) {
    int count = 0;
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd != nullptr && std::holds_alternative<VcmdTremoloOn>(vcmd->vcmd)) {
                ++count;
            }
        }
    }
    return count;
}

bool songContainsVibratoOff(const NspcSong& song) {
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd != nullptr && std::holds_alternative<VcmdVibratoOff>(vcmd->vcmd)) {
                return true;
            }
        }
    }
    return false;
}

bool songContainsTremoloOff(const NspcSong& song) {
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd != nullptr && std::holds_alternative<VcmdTremoloOff>(vcmd->vcmd)) {
                return true;
            }
        }
    }
    return false;
}

bool songContainsPitchEnvelope(const NspcSong& song) {
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd == nullptr) {
                continue;
            }
            if (std::holds_alternative<VcmdPitchEnvelopeFrom>(vcmd->vcmd) ||
                std::holds_alternative<VcmdPitchEnvelopeTo>(vcmd->vcmd)) {
                return true;
            }
        }
    }
    return false;
}

bool songContainsPitchEnvelopeOff(const NspcSong& song) {
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd == nullptr) {
                continue;
            }
            if (std::holds_alternative<VcmdPitchEnvelopeOff>(vcmd->vcmd)) {
                return true;
            }
        }
    }
    return false;
}

int songCountNotes(const NspcSong& song) {
    int count = 0;
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            if (std::holds_alternative<Note>(entry.event)) {
                ++count;
            }
        }
    }
    return count;
}

int songCountRests(const NspcSong& song) {
    int count = 0;
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            if (std::holds_alternative<Rest>(entry.event)) {
                ++count;
            }
        }
    }
    return count;
}

int songCountTies(const NspcSong& song) {
    int count = 0;
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            if (std::holds_alternative<Tie>(entry.event)) {
                ++count;
            }
        }
    }
    return count;
}

std::vector<int> collectDurations(const NspcSong& song) {
    std::vector<int> durations;
    for (const auto& track : song.tracks()) {
        for (const auto& entry : track.events) {
            if (const auto* duration = std::get_if<Duration>(&entry.event); duration != nullptr) {
                durations.push_back(duration->ticks);
            }
        }
    }
    return durations;
}

uint8_t expectedMappedVolumeFromItFormula(int noteVolume, int sampleVolume, int instrumentVolume, int channelVolume) {
    int v = ((std::clamp(noteVolume, 0, 64) * std::clamp(sampleVolume, 0, 64) * std::clamp(instrumentVolume, 0, 128) *
              std::clamp(channelVolume, 0, 64)) /
             131072) -
            1;
    if (v != 0xFF) {
        ++v;
    }
    const int mapped = static_cast<int>(std::lround(std::sqrt(256.0 * static_cast<double>(std::max(v, 0)))));
    return static_cast<uint8_t>(std::clamp(mapped, 0, 0xFF));
}

uint8_t expectedMappedGlobalVolume(int itGlobalVolume) {
    const int clamped = std::clamp(itGlobalVolume, 0, 128);
    const int scaled = clamped * 2;
    if (scaled >= 0x100) {
        return 0xFF;
    }
    const int mapped = static_cast<int>(std::lround(std::sqrt(256.0 * static_cast<double>(std::max(scaled, 0)))));
    return static_cast<uint8_t>(std::clamp(mapped, 0, 0xFF));
}

std::optional<int> firstTrackDuration(const NspcSong& song, int trackId) {
    const auto trackIt = std::find_if(song.tracks().begin(), song.tracks().end(), [&](const NspcTrack& track) {
        return track.id == trackId;
    });
    if (trackIt == song.tracks().end()) {
        return std::nullopt;
    }

    for (const auto& entry : trackIt->events) {
        if (const auto* duration = std::get_if<Duration>(&entry.event); duration != nullptr) {
            return duration->ticks;
        }
    }
    return std::nullopt;
}

}  // namespace

TEST(ItImportTest, ImportMinimalItOverwritesSelectedSongSlot) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    ASSERT_EQ(base.songs().size(), 2u);

    const auto path = writeItFixture(buildMinimalItFile(), "it-import-minimal");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& [project, report] = *imported;
    EXPECT_EQ(report.targetSongIndex, 1);
    ASSERT_EQ(project.songs().size(), base.songs().size());
    EXPECT_EQ(project.songs()[1].songId(), 1);
    EXPECT_TRUE(project.songs()[1].isUserProvided());
}

TEST(ItImportTest, ImportInjectsInitialStateAndUsesLoudVolumeMapping) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(), "it-import-initial-state");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    bool hasGlobalVolume = false;
    bool hasTempo = false;
    bool hasSeedQv = false;
    uint8_t firstTempo = 0;
    uint8_t maxVoiceVolume = 0;

    for (const auto& track : song.tracks()) {
        bool checkedFirstDuration = false;
        for (const auto& entry : track.events) {
            if (!checkedFirstDuration) {
                if (const auto* duration = std::get_if<Duration>(&entry.event); duration != nullptr) {
                    if (duration->quantization.has_value() && duration->velocity.has_value() &&
                        *duration->quantization == 0x07 && *duration->velocity == 0x0F) {
                        hasSeedQv = true;
                    }
                    checkedFirstDuration = true;
                }
            }
            const auto* vcmd = std::get_if<Vcmd>(&entry.event);
            if (vcmd == nullptr) {
                continue;
            }
            std::visit(
                [&](const auto& command) {
                    using T = std::decay_t<decltype(command)>;
                    if constexpr (std::is_same_v<T, VcmdGlobalVolume>) {
                        hasGlobalVolume = true;
                    } else if constexpr (std::is_same_v<T, VcmdTempo>) {
                        hasTempo = true;
                        if (firstTempo == 0) {
                            firstTempo = command.tempo;
                        }
                    } else if constexpr (std::is_same_v<T, VcmdVolume>) {
                        maxVoiceVolume = std::max(maxVoiceVolume, command.volume);
                    } else if constexpr (std::is_same_v<T, VcmdVolumeFade>) {
                        maxVoiceVolume = std::max(maxVoiceVolume, command.target);
                    }
                },
                vcmd->vcmd);
        }
    }

    EXPECT_TRUE(hasGlobalVolume);
    EXPECT_TRUE(hasTempo);
    EXPECT_TRUE(hasSeedQv);
    EXPECT_LE(firstTempo, 40);
    EXPECT_GE(maxVoiceVolume, 0xB0);
}

TEST(ItImportTest, ImportTempoSlideT0xUsesPerTickDelta) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .row0Effect = std::pair<uint8_t, uint8_t>{static_cast<uint8_t>('T' - 64), 0x05},
                                      }),
                                     "it-import-tempo-slide");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto tempos = collectTempoCommands(imported->first.songs()[1]);
    ASSERT_FALSE(tempos.empty());
    const bool hasExpectedTempo = std::any_of(tempos.begin(), tempos.end(), [](uint8_t tempo) { return tempo == 21; });
    EXPECT_TRUE(hasExpectedTempo) << "Expected T05 to decrease tempo to 100 BPM (mapped to 21)";
}

TEST(ItImportTest, ImportGlobalVolumeVxxSetsMappedGlobalVolume) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .row0Effect = std::pair<uint8_t, uint8_t>{static_cast<uint8_t>('V' - 64), 0x40},
                                      }),
                                     "it-import-global-volume-set");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto globals = collectGlobalVolumeCommands(imported->first.songs()[1]);
    const bool hasExpected =
        std::any_of(globals.begin(), globals.end(), [](uint8_t value) { return value == expectedMappedGlobalVolume(0x40); });
    EXPECT_TRUE(hasExpected) << "Expected V40 to map to NSPC global volume using sqrt-domain scaling";
}

TEST(ItImportTest, ImportGlobalVolumeVxxLowValueUsesNonLinearSqrtMapping) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .row0Effect = std::pair<uint8_t, uint8_t>{static_cast<uint8_t>('V' - 64), 0x01},
                                      }),
                                     "it-import-global-volume-low-value");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto globals = collectGlobalVolumeCommands(imported->first.songs()[1]);
    ASSERT_FALSE(globals.empty());
    EXPECT_TRUE(std::any_of(globals.begin(), globals.end(), [](uint8_t value) { return value == expectedMappedGlobalVolume(1); }));
    EXPECT_FALSE(std::any_of(globals.begin(), globals.end(), [](uint8_t value) { return value == 1; }));
}

TEST(ItImportTest, ImportGlobalVolumeSlideWxyUsesPerTickDeltaAndMemory) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x0F, 60, 1, 64, static_cast<uint8_t>('W' - 64), 0x05, 0x00,  // row 0: W05
        0x81, 0x08, static_cast<uint8_t>('W' - 64), 0x00, 0x00,              // row 1: W00 (reuse W05)
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 2,
                                      }),
                                     "it-import-global-volume-slide");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto globals = collectGlobalVolumeCommands(imported->first.songs()[1]);
    const bool hasAfterFirstRow =
        std::any_of(globals.begin(), globals.end(), [](uint8_t value) { return value == expectedMappedGlobalVolume(103); });
    const bool hasAfterSecondRow =
        std::any_of(globals.begin(), globals.end(), [](uint8_t value) { return value == expectedMappedGlobalVolume(78); });
    EXPECT_TRUE(hasAfterFirstRow) << "Expected W05 at speed 6 to slide 128 -> 103";
    EXPECT_TRUE(hasAfterSecondRow) << "Expected W00 to reuse W05 and slide 103 -> 78";
}

TEST(ItImportTest, ImportGlobalVolumeVxxIgnoresOutOfRangeValuesAbove80h) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .row0Effect = std::pair<uint8_t, uint8_t>{static_cast<uint8_t>('V' - 64), 0x81},
                                      }),
                                     "it-import-global-volume-out-of-range");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto globals = collectGlobalVolumeCommands(imported->first.songs()[1]);
    EXPECT_EQ(globals.size(), 1u);
    EXPECT_EQ(globals.front(), expectedMappedGlobalVolume(128));
}

TEST(ItImportTest, ImportVolumeColumnFineSlideDownMapsToDfy) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 60, 1, 76, 0x00,  // row 0: b01 -> DF1
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 1,
                                          .instrumentGlobalVolume = 128,
                                      }),
                                     "it-import-volume-column-b01");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto firstNoteVolume = findVolumeTargetBeforeNthNote(imported->first.songs()[1], 1);
    ASSERT_TRUE(firstNoteVolume.has_value());
    EXPECT_EQ(*firstNoteVolume, 0xFE);
}

TEST(ItImportTest, ImportVolumeColumnSlideDownMapsToD0y) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 60, 1, 96, 0x00,  // row 0: d01 -> D01
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 1,
                                          .instrumentGlobalVolume = 128,
                                      }),
                                     "it-import-volume-column-d01");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto firstNoteVolume = findVolumeTargetBeforeNthNote(imported->first.songs()[1], 1);
    ASSERT_TRUE(firstNoteVolume.has_value());
    EXPECT_EQ(*firstNoteVolume, 0xFA);
}

TEST(ItImportTest, ImportVolumeColumnPortamentoDownMapsToExx) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 67, 1, 106, 0x00,  // row 0: e01 -> E04
        0x00,
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 2,
                                      }),
                                     "it-import-volume-column-e01");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto slide = findFirstPitchSlideToNote(imported->first.songs()[1]);
    ASSERT_TRUE(slide.has_value());
    EXPECT_EQ(slide->delay, 0);
    EXPECT_EQ(slide->length, 5);
    EXPECT_EQ(slide->note, static_cast<uint8_t>(67 - 24 - 1));
}

TEST(ItImportTest, ImportVolumeColumnPortamentoUpMapsToFxx) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 67, 1, 116, 0x00,  // row 0: f01 -> F04
        0x00,
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 2,
                                      }),
                                     "it-import-volume-column-f01");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto slide = findFirstPitchSlideToNote(imported->first.songs()[1]);
    ASSERT_TRUE(slide.has_value());
    EXPECT_EQ(slide->delay, 0);
    EXPECT_EQ(slide->length, 5);
    EXPECT_EQ(slide->note, static_cast<uint8_t>(67 - 24 + 1));
}

TEST(ItImportTest, ImportVolumeColumnTonePortamentoUsesGTableMapping) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 60, 1, 64, 0x00,  // row 0: base note
        0x81, 0x05, 64, 196, 0x00,    // row 1: g03 -> G08
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 2,
                                      }),
                                     "it-import-volume-column-g03");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto slide = findFirstPitchSlideToNote(imported->first.songs()[1]);
    ASSERT_TRUE(slide.has_value());
    EXPECT_EQ(slide->delay, 1);
    EXPECT_EQ(slide->length, 8);
    EXPECT_EQ(slide->note, static_cast<uint8_t>(64 - 24));
}

TEST(ItImportTest, ImportVolumeColumnVibratoDepthUsesLastSpeed) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x0F, 60, 1, 64, static_cast<uint8_t>('H' - 64), 0x41, 0x00,  // row 0: H41
        0x81, 0x04, 206, 0x00,                                               // row 1: h03
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 2,
                                      }),
                                     "it-import-volume-column-h03");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto vibratos = collectVibratoOnCommands(imported->first.songs()[1]);
    const bool hasDepthWithMemSpeed = std::any_of(vibratos.begin(), vibratos.end(), [](const VcmdVibratoOn& vibrato) {
        return vibrato.rate == 16 && vibrato.depth == 48;
    });
    EXPECT_TRUE(hasDepthWithMemSpeed);
}

TEST(ItImportTest, ImportMergesChainedEfPitchSlidesIntoSingleCommand) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 67, 1, 64, 0x00,                                        // row 0: note
        0x81, 0x08, static_cast<uint8_t>('E' - 64), 0x20, 0x00,             // row 1: E20
        0x81, 0x08, static_cast<uint8_t>('E' - 64), 0x20, 0x00,             // row 2: E20 (chain)
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 3,
                                      }),
                                     "it-import-merge-ef-chain");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto slides = collectPitchSlideToNoteCommands(imported->first.songs()[1]);
    ASSERT_EQ(slides.size(), 1u);
    EXPECT_EQ(slides[0].delay, 1);
    EXPECT_EQ(slides[0].length, 10);
    EXPECT_EQ(slides[0].note, static_cast<uint8_t>(67 - 24 - 20));
}

TEST(ItImportTest, ImportDoesNotMergeEfPitchSlidesWhenDirectionChanges) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 67, 1, 64, 0x00,                                        // row 0: note
        0x81, 0x08, static_cast<uint8_t>('E' - 64), 0x20, 0x00,             // row 1: E20
        0x81, 0x08, static_cast<uint8_t>('F' - 64), 0x20, 0x00,             // row 2: F20
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 3,
                                      }),
                                     "it-import-no-merge-ef-dir");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto slides = collectPitchSlideToNoteCommands(imported->first.songs()[1]);
    EXPECT_EQ(slides.size(), 2u);
}

TEST(ItImportTest, ImportVolumeMappingAppliesSampleAndInstrumentGlobalVolume) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .sampleGlobalVolume = 32,
                                          .initialChannelVolume = 32,
                                          .instrumentGlobalVolume = 96,
                                      }),
                                     "it-import-volume-formula-global");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    const auto firstNoteVolume = findVolumeTargetBeforeNthNote(song, 1);
    ASSERT_TRUE(firstNoteVolume.has_value());
    EXPECT_EQ(*firstNoteVolume, expectedMappedVolumeFromItFormula(64, 32, 96, 32));
}

TEST(ItImportTest, ImportVolumeMappingUsesSampleDefaultVolumeWhenNoVolumeColumn) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x03, 60, 1, 0x00,  // row 0: note+instrument only
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .sampleGlobalVolume = 48,
                                          .sampleDefaultVolume = 40,
                                          .initialChannelVolume = 64,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 1,
                                          .instrumentGlobalVolume = 80,
                                      }),
                                     "it-import-volume-formula-default");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    const auto firstNoteVolume = findVolumeTargetBeforeNthNote(song, 1);
    ASSERT_TRUE(firstNoteVolume.has_value());
    EXPECT_EQ(*firstNoteVolume, expectedMappedVolumeFromItFormula(40, 48, 80, 64));
}

TEST(ItImportTest, ImportPlaceholderInstrumentKeepsNeutralInstrumentGlobalVolume) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    std::vector<uint8_t> itBytes = buildMinimalItFile(ItFixtureOptions{
        .includeArpeggio = false,
        .sampleGlobalVolume = 64,
        .initialChannelVolume = 64,
    });
    const size_t instrumentOffsetTable = 0xC0u + static_cast<size_t>(readU16(itBytes, 0x20u));
    writeU32(itBytes, instrumentOffsetTable, 0);
    const auto path = writeItFixture(std::move(itBytes), "it-import-volume-placeholder-inst");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    const auto firstNoteVolume = findVolumeTargetBeforeNthNote(song, 1);
    ASSERT_TRUE(firstNoteVolume.has_value());
    EXPECT_EQ(*firstNoteVolume, expectedMappedVolumeFromItFormula(64, 64, 128, 64));
}

TEST(ItImportTest, ImportCarriesAxxSpeedAcrossPatternBoundaries) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> firstPatternPacked = {
        0x81, 0x07, 60, 1, 64, 0x00,                                      // row 0: note at speed 6
        0x81, 0x08, static_cast<uint8_t>('A' - 64), 0x03, 0x00,           // row 1: A03 => speed 3
    };
    std::vector<uint8_t> itBytes = buildMinimalItFile(ItFixtureOptions{
        .includeArpeggio = false,
        .patternCount = 2,
        .orders = {0x00, 0x01, 0xFF},
        .patternPackedOverride = firstPatternPacked,
        .patternRowsOverride = 2,
    });

    const size_t orderCount = readU16(itBytes, 0x20);
    const size_t instrumentCount = readU16(itBytes, 0x22);
    const size_t sampleCount = readU16(itBytes, 0x24);
    const size_t patternCount = readU16(itBytes, 0x26);
    ASSERT_GE(patternCount, 2u);
    const size_t patternOffsetTable = 0xC0u + orderCount + (instrumentCount * 4u) + (sampleCount * 4u);
    const uint32_t secondPatternOffset = readU32(itBytes, patternOffsetTable + 4u);
    ASSERT_GT(secondPatternOffset, 0u);

    const std::vector<uint8_t> secondPatternPacked = {
        0x81, 0x07, 62, 1, 64, 0x00,                                      // row 0: note, no Axx
        0x00,                                                              // row 1: blank
    };
    const size_t secondPatternDataOffset = static_cast<size_t>(secondPatternOffset) + 8u;
    ASSERT_LE(secondPatternDataOffset + secondPatternPacked.size(), itBytes.size());
    writeU16(itBytes, static_cast<size_t>(secondPatternOffset), static_cast<uint16_t>(secondPatternPacked.size()));
    writeU16(itBytes, static_cast<size_t>(secondPatternOffset) + 0x02u, 2);
    std::copy(secondPatternPacked.begin(), secondPatternPacked.end(),
              itBytes.begin() + static_cast<std::ptrdiff_t>(secondPatternDataOffset));

    const auto path = writeItFixture(itBytes, "it-import-speed-carry");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    ASSERT_EQ(song.patterns().size(), 2u);
    ASSERT_TRUE(song.patterns()[0].channelTrackIds.has_value());
    ASSERT_TRUE(song.patterns()[1].channelTrackIds.has_value());
    const int firstPatternTrackId = (*song.patterns()[0].channelTrackIds)[0];
    const int secondPatternTrackId = (*song.patterns()[1].channelTrackIds)[0];
    ASSERT_GE(firstPatternTrackId, 0);
    ASSERT_GE(secondPatternTrackId, 0);

    const auto firstPatternFirstDuration = firstTrackDuration(song, firstPatternTrackId);
    const auto secondPatternFirstDuration = firstTrackDuration(song, secondPatternTrackId);
    ASSERT_TRUE(firstPatternFirstDuration.has_value());
    ASSERT_TRUE(secondPatternFirstDuration.has_value());
    EXPECT_EQ(*firstPatternFirstDuration, 9);
    EXPECT_EQ(*secondPatternFirstDuration, 6);
}

TEST(ItImportTest, ImportVolumeSlideDxyUsesRowTickRate) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .row0Effect = std::pair<uint8_t, uint8_t>{static_cast<uint8_t>('D' - 64), 0x01},
                                      }),
                                     "it-import-volume-slide-rate");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto volumeFade = findFirstVolumeFade(imported->first.songs()[1]);
    ASSERT_TRUE(volumeFade.has_value());
    EXPECT_EQ(volumeFade->time, 6);
}

TEST(ItImportTest, ImportMidNoteChannelVolumeChangeUsesOneTickFade) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 60, 1, 64, 0x00,  // row 0: note
        0x81, 0x08, static_cast<uint8_t>('M' - 64), 0x20, 0x00,  // row 1: channel volume change while tying
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 2,
                                      }),
                                     "it-import-midnote-channel-volume");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_TRUE(songContainsVolumeFadeWithTime(song, 1));
}

TEST(ItImportTest, ImportMergesClusteredVolumeFadesIntoSingleCommand) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 60, 1, 64, 0x00,                                                // row 0: note
        0x81, 0x0C, 32, static_cast<uint8_t>('M' - 64), 0x20, 0x00,                 // row 1: vol column + channel vol
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 2,
                                      }),
                                     "it-import-merge-volume-cluster");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_TRUE(songContainsVolumeFadeWithTime(song, 1));
    EXPECT_FALSE(songHasClusteredVolumeVcmds(song));
}

TEST(ItImportTest, ImportMergesChainedVolumeFadesAcrossTickSpacing) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 60, 1, 64, 0x00,                                    // row 0: note at default speed 6
        0x81, 0x08, static_cast<uint8_t>('D' - 64), 0x01, 0x00,         // row 1: fade (time 6)
        0x81, 0x08, static_cast<uint8_t>('D' - 64), 0x01, 0x00,         // row 2: fade (time 6), chained
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 3,
                                      }),
                                     "it-import-merge-volume-chain");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_TRUE(songContainsVolumeFadeWithTime(song, 12));
    EXPECT_EQ(countVolumeFades(song), 1);
}

TEST(ItImportTest, ImportInstrumentOnlyRowsResetVolumeBeforeEachDxyFade) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x0F, 60, 1, 64, static_cast<uint8_t>('D' - 64), 0x0F, 0x00, // row 0: note + D0F
        0x81, 0x0A, 1, static_cast<uint8_t>('D' - 64), 0x0F, 0x00,          // row 1: instrument only + D0F
        0x81, 0x0A, 1, static_cast<uint8_t>('D' - 64), 0x0F, 0x00,          // row 2: instrument only + D0F
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 3,
                                      }),
                                     "it-import-inst-only-dxy-reset");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_EQ(countVolumeFades(song), 5);
    EXPECT_TRUE(songContainsVolumeFadeWithTime(song, 1));
    EXPECT_TRUE(songContainsVolumeFadeWithTime(song, 5));
    EXPECT_TRUE(songContainsVolumeFadeWithTime(song, 6));
    EXPECT_FALSE(songHasClusteredVolumeVcmds(song));
}

TEST(ItImportTest, ImportPortamentoTargetWithVolumeUsesOneTickFade) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 60, 1, 64, 0x00,                                                  // row 0: note
        0x81, 0x0D, 62, 40, static_cast<uint8_t>('G' - 64), 0x20, 0x00,               // row 1: tone porta target + volume
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 2,
                                      }),
                                     "it-import-porta-target-volume-fade");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_TRUE(songContainsPitchSlideToNote(song));
    EXPECT_TRUE(songContainsVolumeFadeWithTime(song, 1));
}

TEST(ItImportTest, ImportPortamentoTargetWithInstrumentVolumeUsesOneTickFade) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 60, 1, 64, 0x00,                                    // row 0: note
        0x81, 0x0B, 62, 1, static_cast<uint8_t>('G' - 64), 0x20, 0x00,  // row 1: note+inst+tone porta (no vol column)
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 2,
                                      }),
                                     "it-import-porta-target-inst-volume-fade");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_TRUE(songContainsPitchSlideToNote(song));
    EXPECT_TRUE(songContainsVolumeFadeWithTime(song, 1));
}

TEST(ItImportTest, ImportNoteAfterFadeResetsToInstrumentDefaultBeforeRowFade) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 60, 1, 40, 0x00,                             // row 0: note with explicit volume 40
        0x81, 0x08, static_cast<uint8_t>('D' - 64), 0x01, 0x00, // row 1: D01 => remembered note volume 35
        0x81, 0x0B, 62, 1, static_cast<uint8_t>('D' - 64), 0x01, 0x00, // row 2: note+inst + D01
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 3,
                                      }),
                                     "it-import-note-after-fade-default-volume");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    const auto secondNoteVolume = findVolumeTargetBeforeNthNote(song, 2);
    ASSERT_TRUE(secondNoteVolume.has_value());
    const auto [hasVolumeBeforeSecondNote, hasFadeBeforeSecondNote] = findVolumeAndFadeBeforeNthNote(song, 2);
    EXPECT_TRUE(hasVolumeBeforeSecondNote);
    EXPECT_TRUE(hasFadeBeforeSecondNote);
    // Should reset to instrument/sample default note volume first, then apply D01 from that baseline.
    EXPECT_GE(*secondNoteVolume, static_cast<uint8_t>(0xA8));
}

TEST(ItImportTest, ImportNoteCutScxSplitsRowWithRest) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .row0Effect = std::pair<uint8_t, uint8_t>{static_cast<uint8_t>('S' - 64), 0xC3},
                                          .rows = 1,
                                      }),
                                     "it-import-note-cut");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    const auto durations = collectDurations(song);
    EXPECT_EQ(songCountNotes(song), 1);
    EXPECT_GE(songCountRests(song), 1);
    ASSERT_GE(durations.size(), 2u);
    EXPECT_EQ(durations[0], 3);
    EXPECT_EQ(durations[1], 3);
}

TEST(ItImportTest, ImportNoteOffKeepsPlayingForGainOnlyInstrument) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 60, 1, 64, 0x00, // row 0: note
        0x81, 0x01, 254, 0x00,       // row 1: note off (==)
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 2,
                                          .instrumentUseEnvelope = false,
                                      }),
                                     "it-import-note-off-gain-only");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_EQ(songCountRests(song), 0);
    const auto durations = collectDurations(song);
    ASSERT_FALSE(durations.empty());
    EXPECT_EQ(durations.front(), 12);
}

TEST(ItImportTest, ImportNoteOffKeysOffForAdsrInstrument) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 60, 1, 64, 0x00, // row 0: note
        0x81, 0x01, 254, 0x00,       // row 1: note off (==)
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 2,
                                          .instrumentUseEnvelope = true,
                                          .instrumentEnvelopeNodes = {
                                              {64, 0},
                                              {64, 1},
                                              {32, 8},
                                              {0, 16},
                                          },
                                      }),
                                     "it-import-note-off-adsr");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_GE(songCountRests(song), 1);
}

TEST(ItImportTest, ImportNoteDelaySdxDelaysNoteStart) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .row0Effect = std::pair<uint8_t, uint8_t>{static_cast<uint8_t>('S' - 64), 0xD2},
                                          .rows = 1,
                                      }),
                                     "it-import-note-delay");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    const auto durations = collectDurations(song);
    EXPECT_EQ(songCountNotes(song), 1);
    EXPECT_GE(songCountTies(song), 1);
    ASSERT_GE(durations.size(), 2u);
    EXPECT_EQ(durations[0], 2);
    EXPECT_EQ(durations[1], 4);
}

TEST(ItImportTest, ImportMergesEmptyRowContinuationIntoPreviousNoteDuration) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 60, 1, 64, 0x00, // row 0: note/instrument/volume
        0x00,                        // row 1: empty (continuation)
        0x81, 0x01, 62, 0x00,        // row 2: next note
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 3,
                                      }),
                                     "it-import-merge-empty-row-continuation");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    const auto durations = collectDurations(song);
    EXPECT_EQ(songCountNotes(song), 2);
    EXPECT_EQ(songCountTies(song), 0);
    ASSERT_GE(durations.size(), 2u);
    EXPECT_EQ(durations[0], 12);
    EXPECT_EQ(durations[1], 6);
}

TEST(ItImportTest, ImportKeepsContinuationTieWhenInstrumentRowChangesState) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x07, 60, 1, 64, 0x00, // row 0: note/instrument/volume
        0x81, 0x02, 1, 0x00,         // row 1: instrument only (state change on continuation row)
        0x81, 0x01, 62, 0x00,        // row 2: next note
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 3,
                                      }),
                                     "it-import-keep-tie-on-inst-row");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    const auto durations = collectDurations(song);
    EXPECT_EQ(songCountNotes(song), 2);
    EXPECT_GE(songCountTies(song), 1);
    ASSERT_GE(durations.size(), 3u);
    EXPECT_EQ(durations[0], 6);
}

TEST(ItImportTest, ImportMergesRepeatedVibratoAndTurnsItOffOnNextPlainNote) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x0F, 60, 1, 64, static_cast<uint8_t>('H' - 64), 0x44, 0x00,
        0x81, 0x08, static_cast<uint8_t>('H' - 64), 0x44, 0x00,
        0x81, 0x01, 62, 0x00,
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 3,
                                      }),
                                     "it-import-vibrato-merge");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_EQ(countVibratoOn(song), 1);
    EXPECT_TRUE(songContainsVibratoOff(song));
}

TEST(ItImportTest, ImportMergesRepeatedArpeggioAndTurnsItOffOnNextPlainNote) {
    NspcEngineConfig config = baseConfig();
    config.extensions.push_back(NspcEngineExtension{
        .name = "Arpeggio",
        .description = "arp",
        .enabledByDefault = false,
        .enabled = false,
        .patches = {},
        .vcmds = {NspcEngineExtensionVcmd{
            .id = 0xFC,
            .name = "Arpeggio",
            .description = "offsets",
            .paramCount = 1,
        }},
    });
    NspcProject base = buildProjectWithTwoSongsTwoAssets(std::move(config));

    const std::vector<uint8_t> packed = {
        0x81, 0x0F, 60, 1, 64, static_cast<uint8_t>('J' - 64), 0x37, 0x00,  // row 0
        0x81, 0x08, static_cast<uint8_t>('J' - 64), 0x37, 0x00,              // row 1 (same arp)
        0x81, 0x01, 62, 0x00,                                                 // row 2 plain note -> arp off
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 3,
                                      }),
                                     "it-import-arpeggio-off");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    const auto arpeggioParams = collectExtensionParam0ById(song, 0xFC);
    ASSERT_FALSE(arpeggioParams.empty());
    EXPECT_EQ(std::count(arpeggioParams.begin(), arpeggioParams.end(), static_cast<uint8_t>(0x37)), 1);
    EXPECT_GE(std::count(arpeggioParams.begin(), arpeggioParams.end(), static_cast<uint8_t>(0x00)), 1);
}

TEST(ItImportTest, ImportGuardsVibratoAtPatternBoundary) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x0F, 60, 1, 64, static_cast<uint8_t>('H' - 64), 0x44, 0x00,
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 1,
                                      }),
                                     "it-import-vibrato-boundary-guard");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_EQ(countVibratoOn(song), 1);
    EXPECT_TRUE(songContainsVibratoOff(song));
}

TEST(ItImportTest, ImportMergesRepeatedTremoloAndTurnsItOffOnNextPlainNote) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x0F, 60, 1, 64, static_cast<uint8_t>('R' - 64), 0x44, 0x00,
        0x81, 0x08, static_cast<uint8_t>('R' - 64), 0x44, 0x00,
        0x81, 0x01, 62, 0x00,
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 3,
                                      }),
                                     "it-import-tremolo-merge");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_EQ(countTremoloOn(song), 1);
    EXPECT_TRUE(songContainsTremoloOff(song));
}

TEST(ItImportTest, ImportCopiesAssetsAndDedupesSamplesViaPortSong) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    ASSERT_FALSE(base.samples().empty());

    const auto itBytes = buildMinimalItFile();
    const auto path = writeItFixture(itBytes, "it-import-dedupe");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    // Force the target to already contain the same BRR payload expected from the IT sample.
    const std::vector<int16_t> pcm = {0, 2500, -2500, 1200, -1200, 500, -500, 0, 0, 1200, -1200, 600, -600, 0, 0, 0};
    BrrEncodeOptions encodeOptions{};
    encodeOptions.enhanceTreble = true;
    auto encoded = encodePcm16ToBrr(pcm, encodeOptions);
    ASSERT_TRUE(encoded.has_value()) << encoded.error();
    base.samples()[0].data = encoded->bytes;

    const size_t initialSampleCount = base.samples().size();
    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& [project, report] = *imported;
    EXPECT_GE(report.importedSampleCount, 1);
    EXPECT_EQ(project.samples().size(), initialSampleCount) << "Expected BRR dedupe to reuse existing sample data";
}

TEST(ItImportTest, ImportDedupesIdenticalChannelTracksAcrossPatterns) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .includeHighChannel = false,
                                          .arpeggioValue = 0x37,
                                          .rows = 4,
                                          .patternCount = 3,
                                          .orders = {0x00, 0x01, 0x02, 0xFF},
                                      }),
                                     "it-import-track-dedupe");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& [project, report] = *imported;
    const auto& song = project.songs()[1];
    ASSERT_EQ(song.patterns().size(), 3u);
    ASSERT_EQ(song.tracks().size(), 2u);
    ASSERT_TRUE(song.patterns()[0].channelTrackIds.has_value());
    ASSERT_TRUE(song.patterns()[1].channelTrackIds.has_value());
    ASSERT_TRUE(song.patterns()[2].channelTrackIds.has_value());

    const int setupTrackId = (*song.patterns()[0].channelTrackIds)[0];
    const int dedupTrackIdA = (*song.patterns()[1].channelTrackIds)[0];
    const int dedupTrackIdB = (*song.patterns()[2].channelTrackIds)[0];
    EXPECT_GE(setupTrackId, 0);
    EXPECT_GE(dedupTrackIdA, 0);
    EXPECT_EQ(dedupTrackIdA, dedupTrackIdB);
    EXPECT_NE(setupTrackId, dedupTrackIdA);
    EXPECT_EQ(report.importedTrackCount, 2);
}

TEST(ItImportTest, ImportWarnsAndTruncatesChannelsAboveEight) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .includeHighChannel = true,
                                          .arpeggioValue = 0x37,
                                          .rows = 4,
                                          .orders = {0x00, 0xFF},
                                      }),
                                     "it-import-chan-limit");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& [project, report] = *imported;
    const bool hasWarn = std::any_of(report.warnings.begin(), report.warnings.end(), [](const std::string& warning) {
        return warning.find("channels above 8") != std::string::npos;
    });
    EXPECT_TRUE(hasWarn);

    const auto& song = project.songs()[1];
    for (const auto& pattern : song.patterns()) {
        ASSERT_TRUE(pattern.channelTrackIds.has_value());
        EXPECT_EQ(pattern.channelTrackIds->size(), 8u);
    }
}

TEST(ItImportTest, ImportSkipsOrderSeparatorWithoutWarning) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .includeHighChannel = false,
                                          .rows = 4,
                                          .orders = {0x00, 0xFE, 0x00, 0xFF},
                                      }),
                                     "it-import-order-separator");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& [project, report] = *imported;
    const bool hasSeparatorWarn =
        std::any_of(report.warnings.begin(), report.warnings.end(), [](const std::string& warning) {
            return warning.find("separator 0xFE") != std::string::npos;
        });
    EXPECT_FALSE(hasSeparatorWarn);
    EXPECT_EQ(project.songs()[1].sequence().size(), 3u); // two play-pattern entries + EndSequence
}

TEST(ItImportTest, ImportConvertsLoopedSamplesToDecodableBrr) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .includeHighChannel = false,
                                          .arpeggioValue = 0x37,
                                          .rows = 4,
                                          .orders = {0x00, 0xFF},
                                          .sampleFlags = static_cast<uint8_t>(0x10 | 0x02),
                                          .sampleConversion = static_cast<uint8_t>(0x01),
                                          .sampleLoopBegin = 4,
                                          .sampleLoopEnd = 16,
                                      }),
                                     "it-import-looped-sample");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    ASSERT_FALSE(imported->first.samples().empty());
    auto decoded = decodeBrrToPcm(imported->first.samples().front().data);
    ASSERT_TRUE(decoded.has_value()) << decoded.error();
    EXPECT_FALSE(decoded->empty());
}

TEST(ItImportTest, ImportConvertsSigned8BitSamplesToExpectedBrr) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> signed8 = {0x00, 0x20, 0x40, 0x60, 0x7F, 0x60, 0x40, 0x20,
                                          0x00, 0xE0, 0xC0, 0xA0, 0x80, 0xA0, 0xC0, 0xE0};

    std::vector<int16_t> expectedPcm;
    expectedPcm.reserve(signed8.size());
    for (const uint8_t value : signed8) {
        const int centered = static_cast<int>(static_cast<int8_t>(value));
        expectedPcm.push_back(static_cast<int16_t>(centered * 256));
    }
    BrrEncodeOptions encodeOptions{};
    encodeOptions.enhanceTreble = true;
    auto expectedBrr = encodePcm16ToBrr(expectedPcm, encodeOptions);
    ASSERT_TRUE(expectedBrr.has_value()) << expectedBrr.error();

    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .includeHighChannel = false,
                                          .arpeggioValue = 0x37,
                                          .rows = 4,
                                          .orders = {0x00, 0xFF},
                                          .sampleFlags = static_cast<uint8_t>(0x01),
                                          .sampleConversion = static_cast<uint8_t>(0x01),
                                          .sampleLoopBegin = 0,
                                          .sampleLoopEnd = std::nullopt,
                                          .samplePcm16 = {},
                                          .samplePcm8 = signed8,
                                      }),
                                     "it-import-signed-8bit");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto sampleMatches = [&]() {
        return std::any_of(imported->first.samples().begin(), imported->first.samples().end(),
                           [&](const BrrSample& sample) { return sample.data == expectedBrr->bytes; });
    };
    EXPECT_TRUE(sampleMatches());
}

TEST(ItImportTest, ImportConvertsStereo16BitSamplesByDownmixingToMono) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());

    const std::vector<int16_t> left = {3000, -3000, 2000, -2000, 1000, -1000, 500, -500};
    const std::vector<int16_t> right = {-3000, 3000, 1000, -1000, 500, -500, 250, -250};
    ASSERT_EQ(left.size(), right.size());

    std::vector<int16_t> interleavedChannels;
    interleavedChannels.reserve(left.size() + right.size());
    interleavedChannels.insert(interleavedChannels.end(), left.begin(), left.end());
    interleavedChannels.insert(interleavedChannels.end(), right.begin(), right.end());

    std::vector<int16_t> expectedPcm;
    expectedPcm.reserve(left.size());
    for (size_t i = 0; i < left.size(); ++i) {
        const int mixed = (static_cast<int>(left[i]) + static_cast<int>(right[i])) / 2;
        expectedPcm.push_back(static_cast<int16_t>(std::clamp(mixed, -32768, 32767)));
    }
    BrrEncodeOptions encodeOptions{};
    encodeOptions.enhanceTreble = true;
    auto expectedBrr = encodePcm16ToBrr(expectedPcm, encodeOptions);
    ASSERT_TRUE(expectedBrr.has_value()) << expectedBrr.error();

    auto stereoIt = buildMinimalItFile(ItFixtureOptions{
        .includeArpeggio = false,
        .includeHighChannel = false,
        .arpeggioValue = 0x37,
        .rows = 4,
        .orders = {0x00, 0xFF},
        .sampleFlags = static_cast<uint8_t>(0x02 | 0x04),  // 16-bit stereo
        .sampleConversion = static_cast<uint8_t>(0x01),     // signed PCM
        .sampleLoopBegin = 0,
        .sampleLoopEnd = std::nullopt,
        .samplePcm16 = interleavedChannels,
        .samplePcm8 = {},
    });

    const uint32_t sampleOffset = readU32(stereoIt, 0xC6);
    ASSERT_GT(sampleOffset, 0u);
    writeU32(stereoIt, static_cast<size_t>(sampleOffset) + 0x30u, static_cast<uint32_t>(left.size()));
    writeU32(stereoIt, static_cast<size_t>(sampleOffset) + 0x38u, static_cast<uint32_t>(left.size()));

    const auto path = writeItFixture(stereoIt, "it-import-stereo-16bit");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const bool hasDownmixWarn = std::any_of(imported->second.warnings.begin(), imported->second.warnings.end(),
                                            [](const std::string& warning) {
                                                return warning.find("downmixed to mono") != std::string::npos;
                                            });
    EXPECT_TRUE(hasDownmixWarn);

    const auto sampleMatches = [&]() {
        return std::any_of(imported->first.samples().begin(), imported->first.samples().end(),
                           [&](const BrrSample& sample) { return sample.data == expectedBrr->bytes; });
    };
    EXPECT_TRUE(sampleMatches());
}

TEST(ItImportTest, ImportToleratesTruncatedCompressedSampleData) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .includeHighChannel = false,
                                          .rows = 4,
                                          .orders = {0x00, 0xFF},
                                          .sampleFlags = static_cast<uint8_t>(0x02 | 0x08), // 16-bit compressed
                                          .sampleConversion = static_cast<uint8_t>(0x01),
                                      }),
                                     "it-import-compressed-truncated");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const bool hasTruncatedWarn =
        std::any_of(imported->second.warnings.begin(), imported->second.warnings.end(), [](const std::string& warning) {
            return warning.find("compressed data was truncated") != std::string::npos;
        });
    EXPECT_TRUE(hasTruncatedWarn);
    EXPECT_FALSE(imported->first.samples().empty());
}

TEST(ItImportTest, ImportConvertsPortamentoUpDownToPitchSlideToNote) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .includeHighChannel = false,
                                          .arpeggioValue = 0x37,
                                          .rows = 4,
                                          .orders = {0x00, 0xFF},
                                          .includePortamentoDownBeforeSecondNote = true,
                                          .includePortamentoUpBeforeSecondNote = false,
                                          .applyPreNotePortamentoOnSecondNoteRow = true,
                                          .preNotePortamentoValue = 0x30,
                                          .secondNote = 64,
                                      }),
                                     "it-import-portamento-slide");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_TRUE(songContainsPitchSlideToNote(song));
    EXPECT_FALSE(songContainsPitchEnvelope(song));
    EXPECT_FALSE(songContainsPitchEnvelopeOff(song));
}

TEST(ItImportTest, ImportPortamentoUpDownTargetNoteRetriggers) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .includeHighChannel = false,
                                          .arpeggioValue = 0x37,
                                          .rows = 4,
                                          .orders = {0x00, 0xFF},
                                          .includePortamentoDownBeforeSecondNote = true,
                                          .includePortamentoUpBeforeSecondNote = false,
                                          .applyPreNotePortamentoOnSecondNoteRow = true,
                                          .preNotePortamentoValue = 0x20,
                                          .secondNote = 67,
                                      }),
                                     "it-import-portamento-note-target");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_TRUE(songContainsPitchSlideToNote(song));
    EXPECT_FALSE(songContainsPitchEnvelope(song));
    EXPECT_FALSE(songContainsPitchEnvelopeOff(song));
    EXPECT_EQ(songCountNotes(song), 2);
}

TEST(ItImportTest, ImportNoteAfterEfSlideRetriggersEvenWhenGxxIsPresent) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const std::vector<uint8_t> packed = {
        0x81, 0x0F, 84, 1, 64, static_cast<uint8_t>('E' - 64), 0x20, 0x00,  // row 0: C-7 E20
        0x81, 0x09, 72, static_cast<uint8_t>('G' - 64), 0xF0, 0x00,          // row 1: C-6 GF0
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 2,
                                      }),
                                     "it-import-ef-slide-next-note-retrigger");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_EQ(songCountNotes(song), 2);
    const auto slides = collectPitchSlideToNoteCommands(song);
    EXPECT_EQ(slides.size(), 1u);
}

TEST(ItImportTest, ImportPortamentoUpDownUsesCurrentRowNoteAsSlideBase) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .includeHighChannel = false,
                                          .arpeggioValue = 0x37,
                                          .rows = 4,
                                          .orders = {0x00, 0xFF},
                                          .includePortamentoDownBeforeSecondNote = true,
                                          .includePortamentoUpBeforeSecondNote = false,
                                          .applyPreNotePortamentoOnSecondNoteRow = true,
                                          .preNotePortamentoValue = 0x20,
                                          .secondNote = 67,
                                      }),
                                     "it-import-efx-current-note-base");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    auto pitchSlide = findFirstPitchSlideToNote(song);
    ASSERT_TRUE(pitchSlide.has_value());

    // IT E20 on row with note 67 should slide that note down by 10 semitones over 5 ticks.
    EXPECT_EQ(pitchSlide->delay, 0);
    EXPECT_EQ(pitchSlide->length, 5);
    EXPECT_EQ(pitchSlide->note, static_cast<uint8_t>(67 - 24 - 10));
}

TEST(ItImportTest, ImportNotePortamentoTargetNoteDoesNotRetrigger) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .includeHighChannel = false,
                                          .arpeggioValue = 0x37,
                                          .rows = 4,
                                          .orders = {0x00, 0xFF},
                                          .includePortamentoDownBeforeSecondNote = false,
                                          .includePortamentoUpBeforeSecondNote = false,
                                          .includeNotePortamentoBeforeSecondNote = true,
                                          .applyPreNotePortamentoOnSecondNoteRow = true,
                                          .preNotePortamentoValue = 0x20,
                                          .secondNote = 67,
                                      }),
                                     "it-import-gxx-note-target");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_TRUE(songContainsPitchSlideToNote(song));
    EXPECT_EQ(songCountNotes(song), 1);
}

TEST(ItImportTest, ImportNotePortamentoUsesRateAsSpeedNotRawLength) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .includeHighChannel = false,
                                          .arpeggioValue = 0x37,
                                          .rows = 4,
                                          .orders = {0x00, 0xFF},
                                          .includePortamentoDownBeforeSecondNote = false,
                                          .includePortamentoUpBeforeSecondNote = false,
                                          .includeNotePortamentoBeforeSecondNote = true,
                                          .applyPreNotePortamentoOnSecondNoteRow = true,
                                          .preNotePortamentoValue = 0xFF,
                                          .secondNote = 67,
                                      }),
                                     "it-import-gxx-rate");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    auto pitchSlide = findFirstPitchSlideToNote(song);
    ASSERT_TRUE(pitchSlide.has_value());

    EXPECT_EQ(pitchSlide->delay, 1);
    EXPECT_EQ(pitchSlide->length, 1);
}

TEST(ItImportTest, ImportNotePortamentoWithoutTargetNoteDoesNotQueueToNextNote) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .includeHighChannel = false,
                                          .arpeggioValue = 0x37,
                                          .rows = 4,
                                          .orders = {0x00, 0xFF},
                                          .includePortamentoDownBeforeSecondNote = false,
                                          .includePortamentoUpBeforeSecondNote = false,
                                          .includeNotePortamentoBeforeSecondNote = true,
                                          .applyPreNotePortamentoOnSecondNoteRow = false,
                                          .preNotePortamentoValue = 0x20,
                                          .secondNote = 67,
                                      }),
                                     "it-import-gxx-no-queue");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    EXPECT_FALSE(songContainsPitchSlideToNote(song));
    EXPECT_EQ(songCountNotes(song), 2);
}

TEST(ItImportTest, ImportAutoEnablesLegatoAndArpeggioExtensionsWhenPresent) {
    NspcEngineConfig config = baseConfig();
    config.extensions.push_back(NspcEngineExtension{
        .name = "Legato Mode",
        .description = "legato",
        .enabledByDefault = false,
        .enabled = false,
        .patches = {},
        .vcmds = {NspcEngineExtensionVcmd{
            .id = 0xFB,
            .name = "Legato",
            .description = "state",
            .paramCount = 1,
        }},
    });
    config.extensions.push_back(NspcEngineExtension{
        .name = "Arpeggio",
        .description = "arp",
        .enabledByDefault = false,
        .enabled = false,
        .patches = {},
        .vcmds = {NspcEngineExtensionVcmd{
            .id = 0xFC,
            .name = "Arpeggio",
            .description = "offsets",
            .paramCount = 1,
        }},
    });
    NspcProject base = buildProjectWithTwoSongsTwoAssets(std::move(config));

    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = true,
                                          .includeHighChannel = false,
                                          .arpeggioValue = 0x37,
                                          .rows = 4,
                                          .orders = {0x00, 0xFF},
                                      }),
                                     "it-import-ext-on");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& [project, report] = *imported;
    EXPECT_TRUE(project.isEngineExtensionEnabled("Legato Mode"));
    EXPECT_TRUE(project.isEngineExtensionEnabled("Arpeggio"));

    EXPECT_TRUE(std::any_of(report.enabledExtensions.begin(), report.enabledExtensions.end(),
                            [](const std::string& name) { return name == "Legato Mode"; }));
    EXPECT_TRUE(std::any_of(report.enabledExtensions.begin(), report.enabledExtensions.end(),
                            [](const std::string& name) { return name == "Arpeggio"; }));

    const auto& song = project.songs()[1];
    EXPECT_TRUE(songContainsExtensionVcmd(song, 0xFB));
    EXPECT_TRUE(songContainsExtensionVcmd(song, 0xFC));
}

TEST(ItImportTest, ImportEnablesLegatoPerInitializedChannelTrack) {
    NspcEngineConfig config = baseConfig();
    config.extensions.push_back(NspcEngineExtension{
        .name = "Legato Mode",
        .description = "legato",
        .enabledByDefault = false,
        .enabled = false,
        .patches = {},
        .vcmds = {NspcEngineExtensionVcmd{
            .id = 0xFB,
            .name = "Legato",
            .description = "state",
            .paramCount = 1,
        }},
    });
    NspcProject base = buildProjectWithTwoSongsTwoAssets(std::move(config));

    const std::vector<uint8_t> packed = {
        0x81, 0x07, 60, 1, 64, // row 0 ch0 note
        0x82, 0x07, 64, 1, 64, // row 0 ch1 note
        0x00,
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 1,
                                      }),
                                     "it-import-legato-per-channel");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& song = imported->first.songs()[1];
    const auto legatoParams = collectExtensionParam0ById(song, 0xFB);
    EXPECT_EQ(legatoParams.size(), 2u);
    EXPECT_TRUE(std::all_of(legatoParams.begin(), legatoParams.end(), [](uint8_t param) { return param == 1; }));
}

TEST(ItImportTest, ImportEnablesNoPatternKoffPerFirstPatternChannelTrack) {
    NspcEngineConfig config = baseConfig();
    config.extensions.push_back(NspcEngineExtension{
        .name = "No Pattern KOFF",
        .description = "disable pattern-end key-off",
        .enabledByDefault = false,
        .enabled = false,
        .patches = {},
        .vcmds = {NspcEngineExtensionVcmd{
            .id = 0xFD,
            .name = "No Pattern KOFF",
            .description = "0=off,1=on",
            .paramCount = 1,
        }},
    });
    NspcProject base = buildProjectWithTwoSongsTwoAssets(std::move(config));

    const std::vector<uint8_t> packed = {
        0x81, 0x07, 60, 1, 64, // row 0 ch0 note
        0x82, 0x07, 64, 1, 64, // row 0 ch1 note
        0x00,
    };
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = false,
                                          .patternPackedOverride = packed,
                                          .patternRowsOverride = 1,
                                      }),
                                     "it-import-koff-per-channel");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& [project, report] = *imported;
    EXPECT_TRUE(project.isEngineExtensionEnabled("No Pattern KOFF"));
    EXPECT_TRUE(std::any_of(report.enabledExtensions.begin(), report.enabledExtensions.end(),
                            [](const std::string& name) { return name == "No Pattern KOFF"; }));

    const auto& song = project.songs()[1];
    const auto koffParams = collectExtensionParam0ById(song, 0xFD);
    EXPECT_EQ(koffParams.size(), 2u);
    EXPECT_TRUE(std::all_of(koffParams.begin(), koffParams.end(), [](uint8_t param) { return param == 1; }));
}

TEST(ItImportTest, ImportFallsBackWhenExtensionsMissing) {
    NspcProject base = buildProjectWithTwoSongsTwoAssets(baseConfig());
    const auto path = writeItFixture(buildMinimalItFile(ItFixtureOptions{
                                          .includeArpeggio = true,
                                          .includeHighChannel = false,
                                          .arpeggioValue = 0x45,
                                          .rows = 4,
                                          .orders = {0x00, 0xFF},
                                      }),
                                     "it-import-ext-missing");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_TRUE(imported.has_value()) << imported.error();

    const auto& [project, report] = *imported;
    const bool hasArpWarning = std::any_of(report.warnings.begin(), report.warnings.end(), [](const std::string& warning) {
        return warning.find("Arpeggio extension") != std::string::npos;
    });
    EXPECT_TRUE(hasArpWarning);
    EXPECT_TRUE(report.enabledExtensions.empty());
    EXPECT_FALSE(songHasAnyExtensionVcmd(project.songs()[1]));
}

TEST(ItImportTest, ImportIsAtomicOnFailure) {
    NspcEngineConfig config = baseConfig();
    config.reserved.push_back(NspcReservedRegion{
        .name = "No free ARAM",
        .from = 0x0001,
        .to = 0xFFFF,
    });
    NspcProject base = buildProjectWithTwoSongsTwoAssets(std::move(config));
    const size_t beforeSongCount = base.songs().size();
    const size_t beforeInstrumentCount = base.instruments().size();
    const size_t beforeSampleCount = base.samples().size();
    const uint32_t beforeFreeBytes = base.aramUsage().freeBytes;

    const auto path = writeItFixture(buildMinimalItFile(), "it-import-atomic");
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto imported = importItFileIntoSongSlot(base, path, 1);
    cleanup();
    ASSERT_FALSE(imported.has_value());
    EXPECT_NE(imported.error().find("Not enough free ARAM"), std::string::npos);

    // Base project must remain unchanged on failure.
    EXPECT_EQ(base.songs().size(), beforeSongCount);
    EXPECT_EQ(base.instruments().size(), beforeInstrumentCount);
    EXPECT_EQ(base.samples().size(), beforeSampleCount);
    EXPECT_EQ(base.aramUsage().freeBytes, beforeFreeBytes);
}

}  // namespace ntrak::nspc
