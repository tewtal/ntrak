#include "ntrak/nspc/NspcSpcExport.hpp"

#include "ntrak/nspc/NspcCompile.hpp"
#include "ntrak/emulation/SpcDsp.hpp"

#include <algorithm>
#include <format>
#include <string_view>

namespace ntrak::nspc {
namespace {

constexpr size_t kSpcHeaderSize = 0x100;
constexpr size_t kSpcAramSize = 0x10000;
constexpr size_t kSpcDspRegOffset = kSpcHeaderSize + kSpcAramSize;
constexpr size_t kSpcDspRegSize = 128;
constexpr size_t kSpcMinimumSize = kSpcDspRegOffset + kSpcDspRegSize;
constexpr size_t kSpcExtraRamOffset = 0x101C0;
constexpr size_t kSpcExtraRamSize = 0x40;
constexpr size_t kSpcMinimumSizeWithExtraRam = kSpcExtraRamOffset + kSpcExtraRamSize;

constexpr size_t kSpcPcOffset = 0x25;
constexpr size_t kSpcAOffset = 0x27;
constexpr size_t kSpcXOffset = 0x28;
constexpr size_t kSpcYOffset = 0x29;
constexpr size_t kSpcPsOffset = 0x2A;
constexpr size_t kSpcSpOffset = 0x2B;
constexpr size_t kSpcSongTitleOffset = 0x2E;
constexpr size_t kSpcSongTitleSize = 0x20;
constexpr size_t kSpcArtistOffset = 0xB1;
constexpr size_t kSpcArtistSize = 0x20;

void setVoiceVolumesToZero(emulation::SpcDsp& dsp) {
    for (uint8_t voice = 0; voice < 8; ++voice) {
        dsp.writeDspRegister(static_cast<uint8_t>(voice * 0x10), 0x00);
        dsp.writeDspRegister(static_cast<uint8_t>((voice * 0x10) + 1), 0x00);
    }
}

void writeSpcTextField(std::vector<uint8_t>& spcData, size_t offset, size_t size, std::string_view value) {
    if (offset + size > spcData.size() || size == 0) {
        return;
    }
    std::fill_n(spcData.begin() + static_cast<ptrdiff_t>(offset), static_cast<ptrdiff_t>(size), 0);
    const size_t copyLen = std::min(value.size(), size - 1);
    std::copy_n(value.begin(), static_cast<ptrdiff_t>(copyLen), spcData.begin() + static_cast<ptrdiff_t>(offset));
}

void applySongId666Tags(std::vector<uint8_t>& spcData, const NspcSong& song) {
    writeSpcTextField(spcData, kSpcSongTitleOffset, kSpcSongTitleSize, song.songName());
    writeSpcTextField(spcData, kSpcArtistOffset, kSpcArtistSize, song.author());
}

bool hasAnyUserProvidedContent(const NspcProject& project) {
    const bool hasUserSongs = std::any_of(project.songs().begin(), project.songs().end(),
                                          [](const NspcSong& song) { return song.isUserProvided(); });
    if (hasUserSongs) {
        return true;
    }

    const bool hasUserInstruments = std::any_of(project.instruments().begin(), project.instruments().end(),
                                                [](const NspcInstrument& instrument) {
                                                    return instrument.contentOrigin == NspcContentOrigin::UserProvided;
                                                });
    if (hasUserInstruments) {
        return true;
    }

    return std::any_of(project.samples().begin(), project.samples().end(), [](const BrrSample& sample) {
        return sample.contentOrigin == NspcContentOrigin::UserProvided;
    });
}

}  // namespace

std::expected<std::vector<uint8_t>, std::string> buildAutoPlaySpc(
    NspcProject& project,
    std::span<const uint8_t> baseSpcImage,
    int songIndex,
    std::optional<uint8_t> triggerPortOverride,
    NspcBuildOptions buildOptions) {
    
    const auto& songs = project.songs();
    if (songIndex < 0 || songIndex >= static_cast<int>(songs.size())) {
        return std::unexpected(std::format("Song index {} is out of range (project has {} songs)", 
                                          songIndex, songs.size()));
    }
    const NspcSong& song = songs[static_cast<size_t>(songIndex)];

    std::vector<uint8_t> patchedImage(baseSpcImage.begin(), baseSpcImage.end());

    // Keep export compile behavior aligned with playback, but never mutate project content on export.
    NspcBuildOptions options = buildOptions;
    options.applyOptimizedSongToProject = false;
    options.includeEngineExtensions = true;

    const auto applyUpload = [&](const NspcUploadList& upload, std::string_view stage)
        -> std::expected<void, std::string> {
        auto patched = applyUploadToSpcImage(upload, patchedImage);
        if (!patched.has_value()) {
            return std::unexpected(std::format("Failed to patch SPC image ({}): {}", stage, patched.error()));
        }
        patchedImage = std::move(*patched);
        return {};
    };

    const bool hasUserContent = hasAnyUserProvidedContent(project);
    if (hasUserContent) {
        auto userUpload = buildUserContentUpload(project, options);
        if (!userUpload.has_value()) {
            return std::unexpected(std::format("Failed to compile user content: {}", userUpload.error()));
        }
        if (auto applied = applyUpload(*userUpload, "user content"); !applied.has_value()) {
            return std::unexpected(applied.error());
        }
    }

    NspcBuildOptions songOptions = options;
    if (hasUserContent) {
        // User-content upload already includes enabled extension patches.
        songOptions.includeEngineExtensions = false;
    }

    auto compileResult = buildSongScopedUpload(project, songIndex, songOptions);
    if (!compileResult.has_value()) {
        return std::unexpected(std::format("Failed to compile song: {}", compileResult.error()));
    }

    if (auto applied = applyUpload(compileResult->upload, "song"); !applied.has_value()) {
        return std::unexpected(applied.error());
    }

    if (patchedImage.size() < kSpcMinimumSize) {
        return std::unexpected("Patched SPC image is too small to initialize playback state");
    }

    // Initialize the emulator with the patched SPC
    emulation::SpcDsp dsp;
    dsp.reset();
    if (!dsp.loadSpcFile(patchedImage.data(), static_cast<uint32_t>(patchedImage.size()))) {
        return std::unexpected("Failed to load patched SPC into emulator");
    }

    // Mirror ControlPanel::playSpcImage startup path exactly.
    const auto& engine = project.engineConfig();
    dsp.setPC(engine.entryPoint);
    dsp.clearSampleBuffer();
    setVoiceVolumesToZero(dsp);
    constexpr uint64_t kEngineWarmupCycles = 140000;
    dsp.runCycles(kEngineWarmupCycles);

    // Trigger the song
    const uint8_t configuredTriggerPort = static_cast<uint8_t>(engine.songTriggerPort & 0x03u);
    const uint8_t triggerPort = triggerPortOverride.value_or(configuredTriggerPort);
    const uint8_t triggerValue =
        static_cast<uint8_t>((static_cast<uint32_t>(songIndex) + engine.songTriggerOffset) & 0xFFu);
    dsp.writePort(triggerPort, triggerValue);

    // Build output SPC with updated state
    std::vector<uint8_t> output(patchedImage.begin(), patchedImage.end());
    if (output.size() < kSpcMinimumSizeWithExtraRam) {
        output.resize(kSpcMinimumSizeWithExtraRam, 0);
    }

    // Update ARAM
    const auto aramView = dsp.aram();
    const auto aramBytes = aramView.all();
    std::copy(aramBytes.begin(), aramBytes.end(), output.begin() + static_cast<ptrdiff_t>(kSpcHeaderSize));
    // Persist the trigger write into SPC I/O mirror bytes. ares port latches are not guaranteed
    // to be mirrored back into ARAM $F4-$F7 immediately, but external SPC players commonly
    // derive startup port state from these bytes.
    output[kSpcHeaderSize + 0xF4 + triggerPort] = triggerValue;

    // Update DSP registers
    for (size_t i = 0; i < kSpcDspRegSize; ++i) {
        output[kSpcDspRegOffset + i] = dsp.readDspRegister(static_cast<uint8_t>(i));
    }

    // Update extra RAM (high 64 bytes)
    std::copy_n(aramBytes.begin() + static_cast<ptrdiff_t>(0xFFC0), static_cast<ptrdiff_t>(kSpcExtraRamSize),
                output.begin() + static_cast<ptrdiff_t>(kSpcExtraRamOffset));

    // Update CPU registers
    const uint16_t pc = dsp.pc();
    output[kSpcPcOffset] = static_cast<uint8_t>(pc & 0xFFu);
    output[kSpcPcOffset + 1] = static_cast<uint8_t>((pc >> 8u) & 0xFFu);
    output[kSpcAOffset] = dsp.a();
    output[kSpcXOffset] = dsp.x();
    output[kSpcYOffset] = dsp.y();
    output[kSpcPsOffset] = dsp.ps();
    output[kSpcSpOffset] = dsp.sp();
    applySongId666Tags(output, song);

    // Do not rewrite $F0-$FF from ioState(): ARAM already contains authoritative values.
    // SpcDsp::ioState() is partially synthetic and can clobber valid timer/output state.

    return output;
}

}  // namespace ntrak::nspc
