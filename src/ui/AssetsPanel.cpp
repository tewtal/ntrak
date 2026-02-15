#include "ntrak/ui/AssetsPanel.hpp"

#include "ntrak/audio/SpcPlayer.hpp"
#include "ntrak/nspc/NspcAssetFile.hpp"
#include "ntrak/nspc/BrrCodec.hpp"

#include <imgui.h>
#include <miniaudio.h>
#include <nfd.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <ntrak/app/App.hpp>

namespace ntrak::ui {

namespace {

constexpr uint32_t kAramSize = 0x10000;
constexpr size_t kSpcHeaderSize = 0x100;
constexpr int kMaxInstruments = 64;
constexpr int kMaxSamples = 64;
constexpr int kDefaultPreviewMidiNote = 48;  // N-SPC note C-4 in tracker UI numbering
constexpr uint8_t kDspDirReg = 0x5D;
constexpr uint8_t kInstrumentKeyboardPreviewVoice = 2;
constexpr const char* kInstrumentEditorPopupId = "Instrument Editor##Assets";
constexpr const char* kSampleEditorPopupId = "Sample Editor##Assets";

const char* contentOriginTag(nspc::NspcContentOrigin origin) {
    return (origin == nspc::NspcContentOrigin::UserProvided) ? "U" : "E";
}

struct TrackerNoteKeyBinding {
    ImGuiKey key;
    int semitoneOffset;
};

constexpr std::array<TrackerNoteKeyBinding, 25> kTrackerNoteKeys = {{
    {ImGuiKey_Z, 0},  {ImGuiKey_S, 1},  {ImGuiKey_X, 2},  {ImGuiKey_D, 3},  {ImGuiKey_C, 4},
    {ImGuiKey_V, 5},  {ImGuiKey_G, 6},  {ImGuiKey_B, 7},  {ImGuiKey_H, 8},  {ImGuiKey_N, 9},
    {ImGuiKey_J, 10}, {ImGuiKey_M, 11}, {ImGuiKey_Q, 12}, {ImGuiKey_2, 13}, {ImGuiKey_W, 14},
    {ImGuiKey_3, 15}, {ImGuiKey_E, 16}, {ImGuiKey_R, 17}, {ImGuiKey_5, 18}, {ImGuiKey_T, 19},
    {ImGuiKey_6, 20}, {ImGuiKey_Y, 21}, {ImGuiKey_7, 22}, {ImGuiKey_U, 23}, {ImGuiKey_I, 24},
}};

uint16_t pitchMultiplierFromInstrument(const nspc::NspcInstrument& instrument) {
    uint16_t pitchMult = (static_cast<uint16_t>(instrument.basePitchMult) << 8u) | instrument.fracPitchMult;
    if (pitchMult == 0) {
        pitchMult = 0x0100;
    }
    return pitchMult;
}

struct AddressRange {
    uint32_t from = 0;  // inclusive
    uint32_t to = 0;    // exclusive
};

void addClampedRange(std::vector<AddressRange>& ranges, uint32_t from, uint32_t to) {
    from = std::min<uint32_t>(from, kAramSize);
    to = std::min<uint32_t>(to, kAramSize);
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
        freeRanges.push_back(AddressRange{cursor, kAramSize});
    }
    return freeRanges;
}

std::optional<uint16_t> allocateFromFreeRanges(std::vector<AddressRange>& freeRanges, uint32_t size) {
    if (size == 0 || size > kAramSize) {
        return std::nullopt;
    }

    for (size_t i = 0; i < freeRanges.size(); ++i) {
        auto& range = freeRanges[i];
        if (range.to - range.from < size) {
            continue;
        }

        const uint32_t start = range.from;
        const uint32_t end = start + size;
        if (end == range.to) {
            freeRanges.erase(freeRanges.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            range.from = end;
        }
        return static_cast<uint16_t>(start);
    }

    return std::nullopt;
}

std::optional<int> firstUnusedId(const auto& objects, int maxExclusive) {
    for (int id = 0; id < maxExclusive; ++id) {
        const bool used = std::any_of(objects.begin(), objects.end(), [id](const auto& value) { return value.id == id; });
        if (!used) {
            return id;
        }
    }
    return std::nullopt;
}

void sortById(auto& values) {
    std::sort(values.begin(), values.end(), [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
}

std::expected<std::vector<int16_t>, std::string> decodeWavToMonoPcm16(const std::string& path,
                                                                       uint32_t targetSampleRate,
                                                                       bool highQualityResampling) {
    constexpr ma_uint32 kDefaultResamplerLpfOrder = 4;
    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_s16, 1, targetSampleRate);
    decoderConfig.resampling.linear.lpfOrder = highQualityResampling ? MA_MAX_FILTER_ORDER
                                                                      : kDefaultResamplerLpfOrder;
    ma_decoder decoder{};

    const ma_result initResult = ma_decoder_init_file(path.c_str(), &decoderConfig, &decoder);
    if (initResult != MA_SUCCESS) {
        return std::unexpected(std::format("Failed to decode WAV file: {}", path));
    }

    std::vector<int16_t> pcm;
    pcm.reserve(32768);

    std::array<int16_t, 4096> chunk{};
    while (true) {
        ma_uint64 framesRead = 0;
        const ma_result readResult =
            ma_decoder_read_pcm_frames(&decoder, chunk.data(), static_cast<ma_uint64>(chunk.size()), &framesRead);
        if (readResult != MA_SUCCESS && readResult != MA_AT_END) {
            ma_decoder_uninit(&decoder);
            return std::unexpected(std::format("Error while decoding WAV data: {}", path));
        }

        if (framesRead == 0) {
            break;
        }

        pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(framesRead));
        if (readResult == MA_AT_END) {
            break;
        }
    }

    ma_decoder_uninit(&decoder);
    if (pcm.empty()) {
        return std::unexpected("Decoded WAV has no samples");
    }
    return pcm;
}

}  // namespace

AssetsPanel::AssetsPanel(app::AppState& appState) : appState_(appState) {}

void AssetsPanel::setStatus(std::string message) {
    status_ = std::move(message);
}

void AssetsPanel::syncSourceSpcRange(uint16_t aramAddress, size_t size) {
    if (!appState_.project.has_value() || size == 0) {
        return;
    }
    if (static_cast<uint32_t>(aramAddress) + static_cast<uint32_t>(size) > kAramSize) {
        return;
    }
    if (appState_.sourceSpcData.size() < kSpcHeaderSize + kAramSize) {
        return;
    }

    auto aram = appState_.project->aram();
    const auto src = aram.bytes(aramAddress, size);
    auto dstIt = appState_.sourceSpcData.begin() +
                 static_cast<std::ptrdiff_t>(kSpcHeaderSize + static_cast<size_t>(aramAddress));
    std::copy(src.begin(), src.end(), dstIt);
}

void AssetsPanel::syncProjectAramToPreviewPlayer() {
    if (!appState_.project.has_value() || !appState_.spcPlayer) {
        return;
    }

    const auto srcAram = appState_.project->aram();
    auto dstAram = appState_.spcPlayer->spcDsp().aram();
    const auto srcAll = srcAram.all();
    auto dstAll = dstAram.all();
    std::copy(srcAll.begin(), srcAll.end(), dstAll.begin());
}

void AssetsPanel::previewInstrument(const InstrumentDraft& draft) {
    if (!appState_.spcPlayer || !appState_.project.has_value()) {
        setStatus("Preview unavailable: SPC player is not ready");
        return;
    }

    const auto& engine = appState_.project->engineConfig();
    if (engine.sampleHeaders == 0) {
        setStatus("Preview unavailable: engine has no sample directory");
        return;
    }

    stopPreview();
    syncProjectAramToPreviewPlayer();
    appState_.spcPlayer->spcDsp().writeDspRegister(kDspDirReg, static_cast<uint8_t>(engine.sampleHeaders >> 8));

    // Use the same pitch-base mapping as tracker/song playback.
    const nspc::NspcInstrument draftInstrument{
        .id = draft.id,
        .sampleIndex = draft.sampleIndex,
        .adsr1 = draft.adsr1,
        .adsr2 = draft.adsr2,
        .gain = draft.gain,
        .basePitchMult = draft.basePitchMult,
        .fracPitchMult = draft.fracPitchMult,
        .name = draft.name,
        .originalAddr = 0,
        .contentOrigin = nspc::NspcContentOrigin::UserProvided,
    };

    audio::NotePreviewParams params{};
    params.sampleIndex = static_cast<uint8_t>(draft.sampleIndex & 0x7F);
    params.pitch =
        audio::NotePreviewParams::pitchFromNspcNote(kDefaultPreviewMidiNote, pitchMultiplierFromInstrument(draftInstrument));
    params.volumeL = 127;
    params.volumeR = 127;
    params.adsr1 = draft.adsr1;
    params.adsr2 = draft.adsr2;
    params.gain = draft.gain;
    params.voice = 0;

    appState_.spcPlayer->noteOn(params);
    setStatus(std::format("Previewing instrument {:02X}", std::max(draft.id, 0)));
}

void AssetsPanel::previewSample(const SampleDraft& draft) {
    if (!appState_.spcPlayer || !appState_.project.has_value()) {
        setStatus("Preview unavailable: SPC player is not ready");
        return;
    }
    if (draft.id < 0 || draft.id >= kMaxSamples) {
        setStatus("Preview unavailable: invalid sample id");
        return;
    }
    if (draft.brrData.empty()) {
        setStatus("Preview unavailable: sample has no BRR data");
        return;
    }
    if (draft.brrData.size() > kAramSize) {
        setStatus(std::format("Preview unavailable: sample is too large ({} bytes, max {} bytes)",
                             draft.brrData.size(), kAramSize));
        return;
    }

    const auto& engine = appState_.project->engineConfig();
    if (engine.sampleHeaders == 0) {
        setStatus("Preview unavailable: engine has no sample directory");
        return;
    }

    stopPreview();
    syncProjectAramToPreviewPlayer();
    appState_.spcPlayer->spcDsp().writeDspRegister(kDspDirReg, static_cast<uint8_t>(engine.sampleHeaders >> 8));

    auto playerAram = appState_.spcPlayer->spcDsp().aram();

    uint16_t previewAddr = draft.originalAddr;
    if (previewAddr == 0 || static_cast<uint32_t>(previewAddr) + static_cast<uint32_t>(draft.brrData.size()) > kAramSize) {
        // Sample doesn't have a valid address or doesn't fit - use tail of ARAM
        // We already validated that brrData.size() <= kAramSize above
        const uint32_t tailStart = static_cast<uint32_t>(kAramSize) - static_cast<uint32_t>(draft.brrData.size());
        previewAddr = static_cast<uint16_t>(tailStart);
    }

    auto dst = playerAram.bytes(previewAddr, draft.brrData.size());
    std::copy(draft.brrData.begin(), draft.brrData.end(), dst.begin());

    const uint32_t dirAddr32 = static_cast<uint32_t>(engine.sampleHeaders) + static_cast<uint32_t>(draft.id) * 4u;
    if (dirAddr32 + 4u > kAramSize) {
        setStatus("Preview unavailable: sample directory entry is out of ARAM range");
        return;
    }

    const uint16_t dirAddr = static_cast<uint16_t>(dirAddr32);
    const int blockCount = static_cast<int>(draft.brrData.size() / 9u);
    const int loopBlock = std::clamp(draft.loopBlock, 0, std::max(blockCount - 1, 0));
    const uint16_t loopAddr = draft.loopEnabled ? static_cast<uint16_t>(previewAddr + static_cast<uint16_t>(loopBlock * 9))
                                                : previewAddr;

    playerAram.write16(dirAddr, previewAddr);
    playerAram.write16(dirAddr + 2u, loopAddr);

    audio::NotePreviewParams params{};
    params.sampleIndex = static_cast<uint8_t>(draft.id);
    uint16_t samplePitchBase = 0x1000;
    if (!draft.wavSourcePcm.empty() && draft.targetSampleRate > 0) {
        const uint32_t scaled =
            (0x1000u * std::clamp<uint32_t>(draft.targetSampleRate, 1000u, 32000u) + 16000u) / 32000u;
        samplePitchBase = static_cast<uint16_t>(std::clamp<uint32_t>(scaled, 1u, 0x3FFFu));
    }
    params.pitch = audio::NotePreviewParams::pitchFromMidi(kDefaultPreviewMidiNote, samplePitchBase);
    params.volumeL = 127;
    params.volumeR = 127;
    params.adsr1 = 0x8F;
    params.adsr2 = 0xE0;
    params.gain = 0x7F;
    params.voice = 0;

    appState_.spcPlayer->noteOn(params);
    setStatus(std::format("Previewing sample {:02X}", draft.id));
}

void AssetsPanel::stopPreview() {
    if (appState_.spcPlayer) {
        appState_.spcPlayer->allNotesOff();
    }
    instrumentKeyboardPreviewActive_ = false;
    activeInstrumentPreviewKey_.reset();
}

std::optional<AssetsPanel::TrackerPitchInput> AssetsPanel::consumeTrackerPitchInput() const {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl || io.KeyAlt || io.KeySuper) {
        return std::nullopt;
    }

    const int octave = std::clamp(appState_.trackerInputOctave, 0, 7);
    for (const auto& key : kTrackerNoteKeys) {
        if (ImGui::IsKeyPressed(key.key, false)) {
            const int pitch = std::clamp(octave * 12 + key.semitoneOffset, 0, 0x47);
            return TrackerPitchInput{.pitch = pitch, .key = static_cast<int>(key.key)};
        }
    }

    return std::nullopt;
}

void AssetsPanel::startInstrumentKeyboardPreview(int midiPitch, int key) {
    if (!appState_.project.has_value() || !appState_.spcPlayer) {
        return;
    }
    if (appState_.isPlaying && appState_.isPlaying()) {
        return;
    }

    auto& instruments = appState_.project->instruments();
    const auto it =
        std::find_if(instruments.begin(), instruments.end(),
                     [&](const nspc::NspcInstrument& instrument) { return instrument.id == selectedInstrumentId_; });
    if (it == instruments.end()) {
        return;
    }

    const auto& engine = appState_.project->engineConfig();
    if (engine.sampleHeaders == 0) {
        return;
    }

    syncProjectAramToPreviewPlayer();
    appState_.spcPlayer->spcDsp().writeDspRegister(kDspDirReg, static_cast<uint8_t>(engine.sampleHeaders >> 8));

    appState_.spcPlayer->noteOff(kInstrumentKeyboardPreviewVoice);

    audio::NotePreviewParams params{};
    params.sampleIndex = static_cast<uint8_t>(it->sampleIndex & 0x7F);
    params.pitch = audio::NotePreviewParams::pitchFromNspcNote(midiPitch, pitchMultiplierFromInstrument(*it));
    params.volumeL = 127;
    params.volumeR = 127;
    params.adsr1 = it->adsr1;
    params.adsr2 = it->adsr2;
    params.gain = it->gain;
    params.voice = kInstrumentKeyboardPreviewVoice;
    appState_.spcPlayer->noteOn(params);

    activeInstrumentPreviewKey_ = key;
    instrumentKeyboardPreviewActive_ = true;
}

void AssetsPanel::stopInstrumentKeyboardPreview() {
    if (instrumentKeyboardPreviewActive_ && appState_.spcPlayer) {
        appState_.spcPlayer->noteOff(kInstrumentKeyboardPreviewVoice);
    }
    instrumentKeyboardPreviewActive_ = false;
    activeInstrumentPreviewKey_.reset();
}

void AssetsPanel::handleInstrumentKeyboardPreview() {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        stopInstrumentKeyboardPreview();
        return;
    }

    if (instrumentEditorOpen_ || sampleEditorOpen_ || ImGui::IsPopupOpen(kInstrumentEditorPopupId) ||
        ImGui::IsPopupOpen(kSampleEditorPopupId)) {
        stopInstrumentKeyboardPreview();
        return;
    }

    if (instrumentKeyboardPreviewActive_ && activeInstrumentPreviewKey_.has_value() &&
        ImGui::IsKeyReleased(static_cast<ImGuiKey>(*activeInstrumentPreviewKey_))) {
        stopInstrumentKeyboardPreview();
    }

    if (ImGui::IsAnyItemActive()) {
        return;
    }

    if (const auto pitchInput = consumeTrackerPitchInput(); pitchInput.has_value()) {
        startInstrumentKeyboardPreview(pitchInput->pitch, pitchInput->key);
    }
}

std::optional<size_t> AssetsPanel::findInstrumentIndexById(int id) const {
    if (!appState_.project.has_value() || id < 0) {
        return std::nullopt;
    }
    const auto& instruments = appState_.project->instruments();
    const auto it =
        std::find_if(instruments.begin(), instruments.end(), [id](const auto& instrument) { return instrument.id == id; });
    if (it == instruments.end()) {
        return std::nullopt;
    }
    return static_cast<size_t>(std::distance(instruments.begin(), it));
}

std::optional<size_t> AssetsPanel::findSampleIndexById(int id) const {
    if (!appState_.project.has_value() || id < 0) {
        return std::nullopt;
    }
    const auto& samples = appState_.project->samples();
    const auto it = std::find_if(samples.begin(), samples.end(), [id](const auto& sample) { return sample.id == id; });
    if (it == samples.end()) {
        return std::nullopt;
    }
    return static_cast<size_t>(std::distance(samples.begin(), it));
}

bool AssetsPanel::writeInstrumentToAram(const nspc::NspcInstrument& instrument) {
    if (!appState_.project.has_value()) {
        return false;
    }

    auto& project = *appState_.project;
    const auto& config = project.engineConfig();
    const uint8_t entrySize = std::clamp<uint8_t>(config.instrumentEntryBytes, 5, 6);

    if (config.instrumentHeaders == 0) {
        setStatus("Engine config has no instrument table");
        return false;
    }
    if (instrument.id < 0 || instrument.id >= kMaxInstruments) {
        setStatus("Instrument id is out of range");
        return false;
    }

    const uint32_t address32 = static_cast<uint32_t>(config.instrumentHeaders) + static_cast<uint32_t>(instrument.id) * entrySize;
    if (address32 + entrySize > kAramSize) {
        setStatus("Instrument table write would exceed ARAM");
        return false;
    }

    const uint16_t address = static_cast<uint16_t>(address32);
    auto aram = project.aram();
    aram.write(address + 0, instrument.sampleIndex);
    aram.write(address + 1, instrument.adsr1);
    aram.write(address + 2, instrument.adsr2);
    aram.write(address + 3, instrument.gain);
    aram.write(address + 4, instrument.basePitchMult);
    if (entrySize >= 6) {
        aram.write(address + 5, instrument.fracPitchMult);
    }

    if (config.engineVersion == "0.0" && config.percussionHeaders != 0) {
        const auto commandMap = config.commandMap.value_or(nspc::NspcCommandMap{});
        const int percussionCount =
            static_cast<int>(commandMap.percussionEnd) - static_cast<int>(commandMap.percussionStart) + 1;
        if (instrument.id >= 0 && instrument.id < percussionCount) {
            const uint32_t percussionAddress32 =
                static_cast<uint32_t>(config.percussionHeaders) + static_cast<uint32_t>(instrument.id) * 6u;
            if (percussionAddress32 + 6u <= kAramSize) {
                const uint16_t percussionAddress = static_cast<uint16_t>(percussionAddress32);
                aram.write(percussionAddress + 0, instrument.sampleIndex);
                aram.write(percussionAddress + 1, instrument.adsr1);
                aram.write(percussionAddress + 2, instrument.adsr2);
                aram.write(percussionAddress + 3, instrument.gain);
                aram.write(percussionAddress + 4, instrument.basePitchMult);
                aram.write(percussionAddress + 5, instrument.percussionNote);
                syncSourceSpcRange(percussionAddress, 6);
            }
        }
    }

    syncSourceSpcRange(address, entrySize);
    return true;
}

void AssetsPanel::clearInstrumentEntryInAram(int instrumentId) {
    if (!appState_.project.has_value()) {
        return;
    }

    auto& project = *appState_.project;
    const auto& config = project.engineConfig();
    const uint8_t entrySize = std::clamp<uint8_t>(config.instrumentEntryBytes, 5, 6);

    if (config.instrumentHeaders == 0 || instrumentId < 0 || instrumentId >= kMaxInstruments) {
        return;
    }

    const uint32_t address32 = static_cast<uint32_t>(config.instrumentHeaders) + static_cast<uint32_t>(instrumentId) * entrySize;
    if (address32 + entrySize > kAramSize) {
        return;
    }

    const uint16_t address = static_cast<uint16_t>(address32);
    auto aram = project.aram();
    for (uint8_t i = 0; i < entrySize; ++i) {
        aram.write(address + i, 0);
    }

    syncSourceSpcRange(address, entrySize);
}

bool AssetsPanel::writeSampleDirectoryEntry(int sampleId, uint16_t startAddr, uint16_t loopAddr) {
    if (!appState_.project.has_value()) {
        return false;
    }

    auto& project = *appState_.project;
    const auto& config = project.engineConfig();
    if (config.sampleHeaders == 0) {
        setStatus("Engine config has no sample directory");
        return false;
    }
    if (sampleId < 0 || sampleId >= kMaxSamples) {
        setStatus("Sample id is out of range");
        return false;
    }

    const uint32_t address32 = static_cast<uint32_t>(config.sampleHeaders) + static_cast<uint32_t>(sampleId) * 4u;
    if (address32 + 4u > kAramSize) {
        setStatus("Sample directory write would exceed ARAM");
        return false;
    }

    const uint16_t address = static_cast<uint16_t>(address32);
    auto aram = project.aram();
    aram.write16(address, startAddr);
    aram.write16(address + 2u, loopAddr);
    syncSourceSpcRange(address, 4);
    return true;
}

bool AssetsPanel::writeSampleDataToAram(const nspc::BrrSample& sample) {
    if (!appState_.project.has_value()) {
        return false;
    }
    if (sample.data.empty()) {
        setStatus("Sample BRR data is empty");
        return false;
    }

    const uint32_t start = sample.originalAddr;
    const uint32_t size = static_cast<uint32_t>(sample.data.size());
    if (start == 0 || start + size > kAramSize) {
        setStatus("Sample data write would exceed ARAM");
        return false;
    }

    auto aram = appState_.project->aram();
    auto dst = aram.bytes(sample.originalAddr, sample.data.size());
    std::copy(sample.data.begin(), sample.data.end(), dst.begin());
    syncSourceSpcRange(sample.originalAddr, sample.data.size());
    return true;
}

std::optional<uint16_t> AssetsPanel::allocateSampleAddress(size_t size, std::optional<int> replaceSampleId,
                                                           std::optional<uint16_t> preferred) {
    if (!appState_.project.has_value() || size == 0 || size > kAramSize) {
        return std::nullopt;
    }

    auto& project = *appState_.project;
    project.refreshAramUsage();
    const auto& usage = project.aramUsage();

    std::vector<AddressRange> blocked;
    addClampedRange(blocked, 0, 1);

    for (const auto& region : usage.regions) {
        if (replaceSampleId.has_value() && region.kind == nspc::NspcAramRegionKind::SampleData &&
            region.objectId == *replaceSampleId) {
            continue;
        }
        addClampedRange(blocked, region.from, region.to);
    }

    normalizeRanges(blocked);
    auto freeRanges = invertRanges(blocked);

    if (preferred.has_value()) {
        for (size_t i = 0; i < freeRanges.size(); ++i) {
            auto& range = freeRanges[i];
            const uint32_t start = *preferred;
            const uint32_t end = start + static_cast<uint32_t>(size);
            if (start < range.from || end > range.to) {
                continue;
            }

            if (start == range.from && end == range.to) {
                freeRanges.erase(freeRanges.begin() + static_cast<std::ptrdiff_t>(i));
            } else if (start == range.from) {
                range.from = end;
            } else if (end == range.to) {
                range.to = start;
            } else {
                const AddressRange tail{end, range.to};
                range.to = start;
                freeRanges.insert(freeRanges.begin() + static_cast<std::ptrdiff_t>(i + 1), tail);
            }
            return static_cast<uint16_t>(start);
        }
    }

    return allocateFromFreeRanges(freeRanges, static_cast<uint32_t>(size));
}

bool AssetsPanel::importNtiAsNewInstrument() {
    if (!appState_.project.has_value()) {
        setStatus("No project loaded");
        return false;
    }

    NFD::UniquePath outPath;
    nfdfilteritem_t filterItem[1] = {{"ntrak Instrument", "nti"}};
    const nfdresult_t result = NFD::OpenDialog(outPath, filterItem, 1);
    if (result != NFD_OKAY) {
        return false;
    }

    const std::filesystem::path ntiPath = outPath.get();
    auto imported = nspc::loadNtiFile(ntiPath);
    if (!imported.has_value()) {
        setStatus(imported.error());
        return false;
    }

    auto& project = *appState_.project;
    auto& instruments = project.instruments();
    auto& samples = project.samples();

    const auto freeInstrumentId = firstUnusedId(instruments, kMaxInstruments);
    if (!freeInstrumentId.has_value()) {
        setStatus("No free instrument slots available");
        return false;
    }
    const auto freeSampleId = firstUnusedId(samples, kMaxSamples);
    if (!freeSampleId.has_value()) {
        setStatus("No free sample slots available");
        return false;
    }

    const auto allocatedAddr = allocateSampleAddress(imported->sample.data.size());
    if (!allocatedAddr.has_value()) {
        setStatus("No free ARAM range for imported sample");
        return false;
    }

    nspc::BrrSample sample = imported->sample;
    sample.id = *freeSampleId;
    if (sample.name.empty()) {
        sample.name = std::format("Sample {:02X}", sample.id);
    }
    sample.originalAddr = *allocatedAddr;
    sample.originalLoopAddr = sample.originalAddr;
    if (imported->loopEnabled) {
        const uint32_t loopAddr =
            static_cast<uint32_t>(sample.originalAddr) + static_cast<uint32_t>(imported->loopOffsetBytes);
        if (loopAddr >= kAramSize) {
            setStatus("Imported NTI loop offset exceeds ARAM range");
            return false;
        }
        sample.originalLoopAddr = static_cast<uint16_t>(loopAddr);
    }
    sample.contentOrigin = nspc::NspcContentOrigin::UserProvided;

    nspc::NspcInstrument instrument = imported->instrument;
    instrument.id = *freeInstrumentId;
    if (instrument.name.empty()) {
        instrument.name = std::format("Inst {:02X}", instrument.id);
    }
    instrument.sampleIndex = static_cast<uint8_t>(sample.id & 0x7F);
    instrument.contentOrigin = nspc::NspcContentOrigin::UserProvided;

    if (!writeInstrumentToAram(instrument)) {
        return false;
    }
    if (!writeSampleDirectoryEntry(sample.id, sample.originalAddr, sample.originalLoopAddr)) {
        clearInstrumentEntryInAram(instrument.id);
        return false;
    }
    if (!writeSampleDataToAram(sample)) {
        clearInstrumentEntryInAram(instrument.id);
        (void)writeSampleDirectoryEntry(sample.id, 0, 0);
        return false;
    }

    instruments.push_back(instrument);
    samples.push_back(sample);
    sortById(instruments);
    sortById(samples);

    project.refreshAramUsage();
    selectedInstrumentId_ = instrument.id;
    appState_.selectedInstrumentId = selectedInstrumentId_;
    selectedSampleId_ = sample.id;
    setStatus(std::format("Imported '{}' as instrument {:02X} + sample {:02X}", ntiPath.filename().string(),
                          instrument.id, sample.id));
    return true;
}

bool AssetsPanel::exportSelectedInstrumentAsNti() {
    if (!appState_.project.has_value()) {
        setStatus("No project loaded");
        return false;
    }
    if (selectedInstrumentId_ < 0) {
        setStatus("No instrument selected");
        return false;
    }

    auto& project = *appState_.project;
    const auto instrumentIndex = findInstrumentIndexById(selectedInstrumentId_);
    if (!instrumentIndex.has_value()) {
        setStatus("Selected instrument no longer exists");
        return false;
    }

    const auto& instrument = project.instruments()[*instrumentIndex];
    const int sampleId = static_cast<int>(instrument.sampleIndex & 0x7F);
    const auto sampleIndex = findSampleIndexById(sampleId);
    if (!sampleIndex.has_value()) {
        setStatus(std::format("Instrument {:02X} references missing sample {:02X}", instrument.id, sampleId));
        return false;
    }
    const auto& sample = project.samples()[*sampleIndex];

    std::string defaultName = instrument.name.empty() ? std::format("instrument_{:02X}.nti", instrument.id)
                                                       : std::format("{}.nti", instrument.name);

    NFD::UniquePath outPath;
    nfdfilteritem_t filterItem[1] = {{"ntrak Instrument", "nti"}};
    const nfdresult_t result = NFD::SaveDialog(outPath, filterItem, 1, nullptr, defaultName.c_str());
    if (result != NFD_OKAY) {
        return false;
    }

    const std::filesystem::path ntiPath = outPath.get();
    auto saveResult = nspc::saveNtiFile(ntiPath, instrument, sample);
    if (!saveResult.has_value()) {
        setStatus(saveResult.error());
        return false;
    }

    setStatus(std::format("Exported NTI '{}'", ntiPath.filename().string()));
    return true;
}

bool AssetsPanel::importWavIntoSampleDraft(SampleDraft& draft) {
    NFD::UniquePath outPath;
    nfdfilteritem_t filterItem[1] = {{"Wave files", "wav"}};
    const nfdresult_t result = NFD::OpenDialog(outPath, filterItem, 1);
    if (result != NFD_OKAY) {
        return false;
    }

    const std::string wavPath = outPath.get();
    draft.wavSourcePath = wavPath;
    draft.wavDecodedSampleRate = 0;
    if (!refreshWavSourcePcmForDraft(draft)) {
        return false;
    }
    if (draft.wavSourcePcm.empty()) {
        setStatus("Imported WAV has no samples");
        return false;
    }

    if (!rebuildWavSampleDraftBrr(draft)) {
        return false;
    }

    draft.name = std::filesystem::path(wavPath).filename().string();
    const int blockCount = static_cast<int>(draft.brrData.size() / 9u);
    const float durationSeconds =
        static_cast<float>(draft.wavSourcePcm.size()) / static_cast<float>(std::max<uint32_t>(draft.targetSampleRate, 1u));
    setStatus(std::format("Imported WAV {} ({} bytes BRR, {} blocks, {:.2f}s @ {}Hz). Use Start/Loop/End tools to trim.",
                         std::filesystem::path(wavPath).filename().string(),
                         draft.brrData.size(), blockCount, durationSeconds, draft.targetSampleRate));
    return true;
}

bool AssetsPanel::importBrrIntoSampleDraft(SampleDraft& draft) {
    NFD::UniquePath outPath;
    nfdfilteritem_t filterItem[1] = {{"BRR files", "brr"}};
    const nfdresult_t result = NFD::OpenDialog(outPath, filterItem, 1);
    if (result != NFD_OKAY) {
        return false;
    }

    const std::filesystem::path brrPath = outPath.get();
    auto loaded = nspc::loadBrrFile(brrPath);
    if (!loaded.has_value()) {
        setStatus(loaded.error());
        return false;
    }

    if (loaded->size() > kAramSize) {
        setStatus(std::format("BRR file is too large: {} bytes (max {} bytes)", loaded->size(), kAramSize));
        return false;
    }

    draft.brrData = std::move(*loaded);
    draft.name = brrPath.filename().string();
    clearWavSourceInSampleDraft(draft);

    const int blockCount = static_cast<int>(draft.brrData.size() / 9u);
    const bool hasLoopFlag = blockCount > 0 && ((draft.brrData[draft.brrData.size() - 9u] & 0x02u) != 0u);
    draft.loopEnabled = hasLoopFlag;
    draft.loopBlock = 0;

    refreshSampleWavePreview();
    setStatus(std::format("Imported BRR {} ({} bytes, {} blocks)", brrPath.filename().string(),
                         draft.brrData.size(), blockCount));
    return true;
}

bool AssetsPanel::rebuildWavSampleDraftBrr(SampleDraft& draft) {
    if (!refreshWavSourcePcmForDraft(draft)) {
        return false;
    }
    if (draft.wavSourcePcm.empty()) {
        setStatus("WAV tools unavailable: no WAV source loaded");
        return false;
    }

    const int sourceCount = static_cast<int>(draft.wavSourcePcm.size());
    draft.wavTrimStartSample = std::clamp(draft.wavTrimStartSample, 0, std::max(sourceCount - 1, 0));
    draft.wavTrimEndSample = std::clamp(draft.wavTrimEndSample, draft.wavTrimStartSample + 1, sourceCount);
    draft.wavLoopSample = std::clamp(draft.wavLoopSample, draft.wavTrimStartSample, draft.wavTrimEndSample - 1);

    const auto beginIt = draft.wavSourcePcm.begin() + static_cast<std::ptrdiff_t>(draft.wavTrimStartSample);
    const auto endIt = draft.wavSourcePcm.begin() + static_cast<std::ptrdiff_t>(draft.wavTrimEndSample);
    std::vector<int16_t> trimmed(beginIt, endIt);
    if (trimmed.empty()) {
        setStatus("Trim range is empty");
        return false;
    }

    nspc::BrrEncodeOptions encodeOptions;
    encodeOptions.enableLoop = draft.loopEnabled;
    encodeOptions.enhanceTreble = draft.enhanceTrebleOnEncode;
    if (draft.loopEnabled) {
        const int relativeLoop = std::clamp(draft.wavLoopSample - draft.wavTrimStartSample, 0, static_cast<int>(trimmed.size()) - 1);
        encodeOptions.loopStartSample = static_cast<size_t>(relativeLoop);
    }

    auto encoded = nspc::encodePcm16ToBrr(trimmed, encodeOptions);
    if (!encoded.has_value()) {
        setStatus(encoded.error());
        return false;
    }

    if (encoded->bytes.size() > kAramSize) {
        setStatus(std::format("Encoded BRR is too large: {} bytes (max {} bytes). "
                              "Try a shorter range or a lower sample rate.",
                              encoded->bytes.size(), kAramSize));
        return false;
    }

    draft.brrData = std::move(encoded->bytes);
    const int blockCount = static_cast<int>(draft.brrData.size() / 9u);
    if (draft.loopEnabled && blockCount > 0) {
        draft.loopBlock = std::clamp<int>(static_cast<int>(encoded->loopOffsetBytes / 9u), 0, blockCount - 1);
    } else {
        draft.loopBlock = 0;
    }

    refreshSampleWavePreview();
    return true;
}

bool AssetsPanel::refreshWavSourcePcmForDraft(SampleDraft& draft) {
    if (draft.wavSourcePath.empty()) {
        return true;
    }
    const bool matchesSettings = !draft.wavSourcePcm.empty() && draft.wavDecodedSampleRate == draft.targetSampleRate &&
                                 draft.wavDecodedHighQuality == draft.highQualityResampling;
    if (matchesSettings) {
        return true;
    }

    const int previousCount = static_cast<int>(draft.wavSourcePcm.size());
    const int previousStart = draft.wavTrimStartSample;
    const int previousEnd = draft.wavTrimEndSample;
    const int previousLoop = draft.wavLoopSample;

    auto decoded = decodeWavToMonoPcm16(draft.wavSourcePath, draft.targetSampleRate, draft.highQualityResampling);
    if (!decoded.has_value()) {
        setStatus(decoded.error());
        return false;
    }
    draft.wavSourcePcm = std::move(*decoded);
    draft.wavDecodedSampleRate = draft.targetSampleRate;
    draft.wavDecodedHighQuality = draft.highQualityResampling;

    const int newCount = static_cast<int>(draft.wavSourcePcm.size());
    if (newCount <= 0) {
        draft.wavTrimStartSample = 0;
        draft.wavTrimEndSample = 0;
        draft.wavLoopSample = 0;
        return true;
    }

    if (previousCount <= 0) {
        draft.wavTrimStartSample = 0;
        draft.wavTrimEndSample = newCount;
        draft.wavLoopSample = 0;
        return true;
    }

    auto scaleIndex = [&](int index) {
        const int maxPrev = std::max(previousCount - 1, 1);
        const int maxNew = std::max(newCount - 1, 1);
        const int clamped = std::clamp(index, 0, previousCount - 1);
        return static_cast<int>((static_cast<int64_t>(clamped) * maxNew + maxPrev / 2) / maxPrev);
    };

    auto scaleEndExclusive = [&](int endExclusive) {
        const int clamped = std::clamp(endExclusive, 1, previousCount);
        return static_cast<int>((static_cast<int64_t>(clamped) * newCount + previousCount / 2) / previousCount);
    };

    draft.wavTrimStartSample = std::clamp(scaleIndex(previousStart), 0, newCount - 1);
    draft.wavTrimEndSample = std::clamp(scaleEndExclusive(previousEnd), draft.wavTrimStartSample + 1, newCount);
    draft.wavLoopSample = std::clamp(scaleIndex(previousLoop), draft.wavTrimStartSample, draft.wavTrimEndSample - 1);
    return true;
}

void AssetsPanel::clearWavSourceInSampleDraft(SampleDraft& draft) {
    draft.wavSourcePcm.clear();
    draft.wavSourcePath.clear();
    draft.wavDecodedSampleRate = 0;
    draft.wavDecodedHighQuality = true;
    draft.wavTrimStartSample = 0;
    draft.wavTrimEndSample = 0;
    draft.wavLoopSample = 0;
}

bool AssetsPanel::exportSelectedSampleAsBrr() {
    if (!appState_.project.has_value()) {
        setStatus("No project loaded");
        return false;
    }
    if (selectedSampleId_ < 0) {
        setStatus("No sample selected");
        return false;
    }

    const auto sampleIndex = findSampleIndexById(selectedSampleId_);
    if (!sampleIndex.has_value()) {
        setStatus("Selected sample no longer exists");
        return false;
    }

    const auto& sample = appState_.project->samples()[*sampleIndex];
    std::string defaultName =
        sample.name.empty() ? std::format("sample_{:02X}.brr", sample.id) : std::format("{}.brr", sample.name);

    NFD::UniquePath outPath;
    nfdfilteritem_t filterItem[1] = {{"BRR files", "brr"}};
    const nfdresult_t result = NFD::SaveDialog(outPath, filterItem, 1, nullptr, defaultName.c_str());
    if (result != NFD_OKAY) {
        return false;
    }

    const std::filesystem::path brrPath = outPath.get();
    auto saveResult = nspc::saveBrrFile(brrPath, sample.data);
    if (!saveResult.has_value()) {
        setStatus(saveResult.error());
        return false;
    }

    setStatus(std::format("Exported BRR '{}'", brrPath.filename().string()));
    return true;
}

void AssetsPanel::refreshSampleWavePreview() {
    sampleWavePreview_.clear();
    if (sampleDraft_.brrData.empty()) {
        return;
    }

    auto decoded = nspc::decodeBrrToPcm(sampleDraft_.brrData);
    if (!decoded.has_value()) {
        setStatus(decoded.error());
        return;
    }

    sampleWavePreview_ = std::move(*decoded);
}

void AssetsPanel::drawInstrumentsTab() {
    if (!appState_.project.has_value()) {
        return;
    }

    auto& project = *appState_.project;
    auto& instruments = project.instruments();
    const int userInstrumentCount = static_cast<int>(std::count_if(
        instruments.begin(), instruments.end(),
        [](const nspc::NspcInstrument& instrument) { return instrument.contentOrigin == nspc::NspcContentOrigin::UserProvided; }));
    const int engineInstrumentCount = static_cast<int>(instruments.size()) - userInstrumentCount;

    if (findInstrumentIndexById(appState_.selectedInstrumentId).has_value()) {
        selectedInstrumentId_ = appState_.selectedInstrumentId;
    }
    if (!findInstrumentIndexById(selectedInstrumentId_).has_value()) {
        selectedInstrumentId_ = -1;
    }
    if (selectedInstrumentId_ < 0 && !instruments.empty()) {
        selectedInstrumentId_ = instruments.front().id;
    }
    appState_.selectedInstrumentId = selectedInstrumentId_;
    const auto selectedInstrumentIndex = findInstrumentIndexById(selectedInstrumentId_);
    const bool selectedInstrumentLocked =
        selectedInstrumentIndex.has_value() && appState_.lockEngineContent &&
        instruments[*selectedInstrumentIndex].contentOrigin == nspc::NspcContentOrigin::EngineProvided;

    handleInstrumentKeyboardPreview();

    ImGui::PushFont(ntrak::app::App::fonts().mono, 14.0f);

    if (ImGui::Button("Add")) {
        const auto freeId = firstUnusedId(instruments, kMaxInstruments);
        if (!freeId.has_value()) {
            setStatus("No free instrument slots available");
        } else {
            instrumentEditorIsNew_ = true;
            instrumentDraft_ = InstrumentDraft{};
            instrumentDraft_.id = *freeId;
            instrumentDraft_.name = std::format("Inst {:02X}", *freeId);
            if (!project.samples().empty()) {
                instrumentDraft_.sampleIndex = static_cast<uint8_t>(project.samples().front().id & 0x7F);
            }
            instrumentEditorOpen_ = true;
        }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(selectedInstrumentId_ < 0 || selectedInstrumentLocked);
    if (ImGui::Button("Edit")) {
        if (const auto index = findInstrumentIndexById(selectedInstrumentId_); index.has_value()) {
            const auto& instrument = instruments[*index];
            instrumentEditorIsNew_ = false;
            instrumentDraft_.id = instrument.id;
            instrumentDraft_.name = instrument.name;
            instrumentDraft_.sampleIndex = instrument.sampleIndex;
            instrumentDraft_.adsr1 = instrument.adsr1;
            instrumentDraft_.adsr2 = instrument.adsr2;
            instrumentDraft_.gain = instrument.gain;
            instrumentDraft_.basePitchMult = instrument.basePitchMult;
            instrumentDraft_.fracPitchMult = instrument.fracPitchMult;
            instrumentEditorOpen_ = true;
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(selectedInstrumentId_ < 0 || selectedInstrumentLocked);
    if (ImGui::Button("Remove")) {
        if (const auto index = findInstrumentIndexById(selectedInstrumentId_); index.has_value()) {
            clearInstrumentEntryInAram(selectedInstrumentId_);
            instruments.erase(instruments.begin() + static_cast<std::ptrdiff_t>(*index));
            project.refreshAramUsage();
            setStatus(std::format("Removed instrument {:02X}", selectedInstrumentId_));
            selectedInstrumentId_ = -1;
            appState_.selectedInstrumentId = -1;
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Import")) {
        (void)importNtiAsNewInstrument();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(selectedInstrumentId_ < 0);
    if (ImGui::Button("Export")) {
        (void)exportSelectedInstrumentAsNti();
    }
    ImGui::EndDisabled();

    if (const auto index = findInstrumentIndexById(selectedInstrumentId_); index.has_value()) {
        ImGui::SameLine();
        ImGui::BeginDisabled(instruments[*index].contentOrigin == nspc::NspcContentOrigin::UserProvided ||
                             (appState_.lockEngineContent &&
                              instruments[*index].contentOrigin == nspc::NspcContentOrigin::EngineProvided));
        if (ImGui::Button("User")) {
            (void)project.setInstrumentContentOrigin(selectedInstrumentId_, nspc::NspcContentOrigin::UserProvided);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(instruments[*index].contentOrigin == nspc::NspcContentOrigin::EngineProvided);
        if (ImGui::Button("Engine")) {
            (void)project.setInstrumentContentOrigin(selectedInstrumentId_, nspc::NspcContentOrigin::EngineProvided);
        }
        ImGui::EndDisabled();
    }

    if (selectedInstrumentLocked) {
        ImGui::SameLine();
        ImGui::TextDisabled("Locked");
    }

    ImGui::SameLine();
    if (ImGui::Button("Silence")) {
        stopPreview();
    }

    ImGui::PopFont();
    ImGui::PushFont(ntrak::app::App::fonts().mono, 16.0f);

    const float statusReserve =
        status_.empty() ? 0.0f : (ImGui::GetTextLineHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y + 8.0f);
    const float tableHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y - statusReserve);
    if (ImGui::BeginTable("InstrumentsTable", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Resizable,
                          ImVec2(0.0f, tableHeight))) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Src", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Sample", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        for (const auto& instrument : instruments) {
            ImGui::PushID(instrument.id);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            const bool selected = (selectedInstrumentId_ == instrument.id);
            if (ImGui::Selectable(std::format("{:02X}", instrument.id).c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                selectedInstrumentId_ = instrument.id;
                appState_.selectedInstrumentId = selectedInstrumentId_;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                !(appState_.lockEngineContent &&
                  instrument.contentOrigin == nspc::NspcContentOrigin::EngineProvided)) {
                instrumentEditorIsNew_ = false;
                instrumentDraft_.id = instrument.id;
                instrumentDraft_.name = instrument.name;
                instrumentDraft_.sampleIndex = instrument.sampleIndex;
                instrumentDraft_.adsr1 = instrument.adsr1;
                instrumentDraft_.adsr2 = instrument.adsr2;
                instrumentDraft_.gain = instrument.gain;
                instrumentDraft_.basePitchMult = instrument.basePitchMult;
                instrumentDraft_.fracPitchMult = instrument.fracPitchMult;
                instrumentEditorOpen_ = true;
            }

            ImGui::TableNextColumn();
            const std::string_view name = instrument.name.empty() ? std::string_view{"(unnamed)"} : instrument.name;
            ImGui::TextUnformatted(name.data(), name.data() + name.size());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(contentOriginTag(instrument.contentOrigin));

            ImGui::TableNextColumn();
            ImGui::Text("%02X", instrument.sampleIndex);

            ImGui::PopID();
        }

        ImGui::EndTable();
        ImGui::PopFont();
    }
}

void AssetsPanel::drawSamplesTab() {
    if (!appState_.project.has_value()) {
        return;
    }

    auto& project = *appState_.project;
    auto& samples = project.samples();
    const int userSampleCount = static_cast<int>(std::count_if(
        samples.begin(), samples.end(),
        [](const nspc::BrrSample& sample) { return sample.contentOrigin == nspc::NspcContentOrigin::UserProvided; }));
    const int engineSampleCount = static_cast<int>(samples.size()) - userSampleCount;

    if (!findSampleIndexById(selectedSampleId_).has_value()) {
        selectedSampleId_ = -1;
    }
    const auto selectedSampleIndex = findSampleIndexById(selectedSampleId_);
    const bool selectedSampleLocked =
        selectedSampleIndex.has_value() && appState_.lockEngineContent &&
        samples[*selectedSampleIndex].contentOrigin == nspc::NspcContentOrigin::EngineProvided;

    ImGui::PushFont(ntrak::app::App::fonts().mono, 14.0f);

    if (ImGui::Button("Add")) {
        const auto freeId = firstUnusedId(samples, kMaxSamples);
        if (!freeId.has_value()) {
            setStatus("No free sample slots available");
        } else {
            sampleEditorIsNew_ = true;
            sampleDraft_ = SampleDraft{};
            sampleDraft_.id = *freeId;
            sampleDraft_.name = std::format("Sample {:02X}", *freeId);
            clearWavSourceInSampleDraft(sampleDraft_);
            sampleEditorOpen_ = true;
            sampleWavePreview_.clear();
        }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(selectedSampleId_ < 0 || selectedSampleLocked);
    if (ImGui::Button("Edit")) {
        if (const auto index = findSampleIndexById(selectedSampleId_); index.has_value()) {
            const auto& sample = samples[*index];
            sampleEditorIsNew_ = false;
            sampleDraft_.id = sample.id;
            sampleDraft_.name = sample.name;
            sampleDraft_.brrData = sample.data;
            sampleDraft_.originalAddr = sample.originalAddr;
            sampleDraft_.loopAddr = sample.originalLoopAddr;
            clearWavSourceInSampleDraft(sampleDraft_);
            sampleDraft_.loopEnabled = (sample.originalLoopAddr >= sample.originalAddr) &&
                                       (sample.originalLoopAddr - sample.originalAddr) < sample.data.size() &&
                                       (sample.data.size() % 9u == 0u);
            const int blockCount = static_cast<int>(sample.data.size() / 9u);
            if (sampleDraft_.loopEnabled && blockCount > 0) {
                sampleDraft_.loopBlock =
                    std::clamp<int>((sample.originalLoopAddr - sample.originalAddr) / 9, 0, blockCount - 1);
            } else {
                sampleDraft_.loopBlock = 0;
            }
            refreshSampleWavePreview();
            sampleEditorOpen_ = true;
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(selectedSampleId_ < 0 || selectedSampleLocked);
    if (ImGui::Button("Remove")) {
        if (const auto index = findSampleIndexById(selectedSampleId_); index.has_value()) {
            const auto removed = samples[*index];
            (void)writeSampleDirectoryEntry(removed.id, 0, 0);

            if (removed.originalAddr != 0 && !removed.data.empty() &&
                static_cast<uint32_t>(removed.originalAddr) + removed.data.size() <= kAramSize) {
                auto aram = project.aram();
                auto bytes = aram.bytes(removed.originalAddr, removed.data.size());
                std::fill(bytes.begin(), bytes.end(), 0);
                syncSourceSpcRange(removed.originalAddr, removed.data.size());
            }

            samples.erase(samples.begin() + static_cast<std::ptrdiff_t>(*index));
            project.refreshAramUsage();
            setStatus(std::format("Removed sample {:02X}", selectedSampleId_));
            selectedSampleId_ = -1;
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(selectedSampleId_ < 0);
    if (ImGui::Button("Export")) {
        (void)exportSelectedSampleAsBrr();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    if (const auto index = findSampleIndexById(selectedSampleId_); index.has_value()) {
        ImGui::BeginDisabled(samples[*index].contentOrigin == nspc::NspcContentOrigin::UserProvided ||
                             (appState_.lockEngineContent &&
                              samples[*index].contentOrigin == nspc::NspcContentOrigin::EngineProvided));
        if (ImGui::Button("User")) {
            (void)project.setSampleContentOrigin(selectedSampleId_, nspc::NspcContentOrigin::UserProvided);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(samples[*index].contentOrigin == nspc::NspcContentOrigin::EngineProvided);
        if (ImGui::Button("Engine")) {
            (void)project.setSampleContentOrigin(selectedSampleId_, nspc::NspcContentOrigin::EngineProvided);
        }
        ImGui::EndDisabled();
    }

    if (selectedSampleLocked) {
        ImGui::SameLine();
        ImGui::TextDisabled("Locked");
    }

    ImGui::SameLine();
    if (ImGui::Button("Silence")) {
        stopPreview();
    }

    ImGui::PopFont();
    ImGui::PushFont(ntrak::app::App::fonts().mono, 16.0f);

    const float statusReserve =
        status_.empty() ? 0.0f : (ImGui::GetTextLineHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y + 8.0f);
    const float tableHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y - statusReserve);
    if (ImGui::BeginTable("SamplesTable", 6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Resizable,
                          ImVec2(0.0f, tableHeight))) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Src", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, 76.0f);
        ImGui::TableSetupColumn("Loop", ImGuiTableColumnFlags_WidthFixed, 76.0f);
        ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableHeadersRow();

        for (const auto& sample : samples) {
            ImGui::PushID(sample.id);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            const bool selected = (selectedSampleId_ == sample.id);
            if (ImGui::Selectable(std::format("{:02X}", sample.id).c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                selectedSampleId_ = sample.id;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                !(appState_.lockEngineContent && sample.contentOrigin == nspc::NspcContentOrigin::EngineProvided)) {
                sampleEditorIsNew_ = false;
                sampleDraft_.id = sample.id;
                sampleDraft_.name = sample.name;
                sampleDraft_.brrData = sample.data;
                sampleDraft_.originalAddr = sample.originalAddr;
                sampleDraft_.loopAddr = sample.originalLoopAddr;
                clearWavSourceInSampleDraft(sampleDraft_);
                sampleDraft_.loopEnabled = (sample.originalLoopAddr >= sample.originalAddr) &&
                                           (sample.originalLoopAddr - sample.originalAddr) < sample.data.size() &&
                                           (sample.data.size() % 9u == 0u);
                const int blockCount = static_cast<int>(sample.data.size() / 9u);
                if (sampleDraft_.loopEnabled && blockCount > 0) {
                    sampleDraft_.loopBlock =
                        std::clamp<int>((sample.originalLoopAddr - sample.originalAddr) / 9, 0, blockCount - 1);
                } else {
                    sampleDraft_.loopBlock = 0;
                }
                refreshSampleWavePreview();
                sampleEditorOpen_ = true;
            }

            ImGui::TableNextColumn();
            const std::string_view name = sample.name.empty() ? std::string_view{"(unnamed)"} : sample.name;
            ImGui::TextUnformatted(name.data(), name.data() + name.size());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(contentOriginTag(sample.contentOrigin));

            ImGui::TableNextColumn();
            ImGui::Text("$%04X", sample.originalAddr);

            ImGui::TableNextColumn();
            ImGui::Text("$%04X", sample.originalLoopAddr);

            ImGui::TableNextColumn();
            ImGui::Text("%zu", sample.data.size());

            ImGui::PopID();
        }

        ImGui::EndTable();
        ImGui::PopFont();
    }
}

void AssetsPanel::drawInstrumentEditor() {
    if (instrumentEditorOpen_ && !ImGui::IsPopupOpen(kInstrumentEditorPopupId)) {
        ImGui::OpenPopup(kInstrumentEditorPopupId);
    }

    if (!ImGui::BeginPopupModal(kInstrumentEditorPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (instrumentEditorOpen_ && !ImGui::IsPopupOpen(kInstrumentEditorPopupId)) {
            instrumentEditorOpen_ = false;
        }
        return;
    }

    ImGui::Text("Instrument %02X", std::max(instrumentDraft_.id, 0));
    const bool instrumentDraftLocked =
        !instrumentEditorIsNew_ && appState_.project.has_value() &&
        [&]() {
            if (const auto index = findInstrumentIndexById(instrumentDraft_.id); index.has_value()) {
                return appState_.lockEngineContent &&
                       appState_.project->instruments()[*index].contentOrigin == nspc::NspcContentOrigin::EngineProvided;
            }
            return false;
        }();

    if (instrumentDraftLocked) {
        ImGui::TextDisabled("Engine instrument is locked from edits.");
    }
    ImGui::BeginDisabled(instrumentDraftLocked);

    char nameBuffer[128]{};
    std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", instrumentDraft_.name.c_str());
    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
        instrumentDraft_.name = nameBuffer;
    }

    auto byteInput = [&](const char* label, uint8_t& value, int minValue, int maxValue) {
        int v = static_cast<int>(value);
        if (ImGui::InputInt(label, &v, 1, 4)) {
            value = static_cast<uint8_t>(std::clamp(v, minValue, maxValue));
        }
    };

    if (appState_.project.has_value()) {
        const auto& samples = appState_.project->samples();
        const int currentSampleId = static_cast<int>(instrumentDraft_.sampleIndex & 0x7F);
        std::string selectedSampleLabel = std::format("{:02X} (missing)", currentSampleId);
        if (const auto it =
                std::find_if(samples.begin(), samples.end(),
                             [currentSampleId](const nspc::BrrSample& sample) { return sample.id == currentSampleId; });
            it != samples.end()) {
            const std::string_view sampleName = it->name.empty() ? std::string_view{"(unnamed)"} : it->name;
            selectedSampleLabel = std::format("{:02X} - {}", it->id, sampleName);
        }

        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::BeginCombo("Sample", selectedSampleLabel.c_str())) {
            for (const auto& sample : samples) {
                const bool selected = (sample.id == currentSampleId);
                const std::string_view sampleName = sample.name.empty() ? std::string_view{"(unnamed)"} : sample.name;
                const std::string label = std::format("{:02X} - {}", sample.id, sampleName);
                if (ImGui::Selectable(label.c_str(), selected)) {
                    instrumentDraft_.sampleIndex = static_cast<uint8_t>(sample.id & 0x7F);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (samples.empty()) {
            ImGui::TextDisabled("No samples available. Add a sample in Assets -> Samples.");
        }
    }

    // ADSR Envelope Editor with decoded parameters
    ImGui::Separator();
    ImGui::Text("Envelope (ADSR)");

    // Decode ADSR1: [7:ADSR Enable] [6-4:Decay 0-7] [3-0:Attack 0-15]
    bool adsrEnable = (instrumentDraft_.adsr1 & 0x80) != 0;
    int attack = instrumentDraft_.adsr1 & 0x0F;         // bits 0-3 (4-bit attack rate)
    int decay = (instrumentDraft_.adsr1 >> 4) & 0x07;   // bits 4-6 (3-bit decay rate)

    // Decode ADSR2: [7-5:Sustain Level 0-7] [4-0:Sustain Rate 0-31]
    int sustainLevel = (instrumentDraft_.adsr2 >> 5) & 0x07;  // bits 5-7 (3-bit sustain level)
    int sustainRate = instrumentDraft_.adsr2 & 0x1F;          // bits 0-4 (5-bit sustain rate)

    // Decode GAIN for both editing and visualization
    bool customGain = (instrumentDraft_.gain & 0x80) != 0;
    int directVolume = instrumentDraft_.gain & 0x7F;
    int gainRate = instrumentDraft_.gain & 0x1F;
    int gainMode = (instrumentDraft_.gain >> 5) & 0x03;

    if (ImGui::Checkbox("ADSR Enable", &adsrEnable)) {
        instrumentDraft_.adsr1 = (instrumentDraft_.adsr1 & 0x7F) | (adsrEnable ? 0x80 : 0x00);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable ADSR envelope. If disabled, GAIN is used instead.");
    }

    if (adsrEnable) {
        ImGui::BeginDisabled(false);

        if (ImGui::SliderInt("Attack", &attack, 0, 15)) {
            // Clear bits 0-3 and set new attack value (bits 0-3)
            instrumentDraft_.adsr1 = (instrumentDraft_.adsr1 & 0xF0) | (attack & 0x0F);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Attack rate (0=slow, 15=fast). How quickly the sound reaches full volume.\n"
                             "Rate = N*2+1, Step = +32 (or +1024 when Rate=31)");
        }

        if (ImGui::SliderInt("Decay", &decay, 0, 7)) {
            // Clear bits 4-6 and set new decay value (bits 4-6)
            instrumentDraft_.adsr1 = (instrumentDraft_.adsr1 & 0x8F) | ((decay & 0x07) << 4);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Decay rate (0=slow, 7=fast). How quickly volume falls to sustain level after attack.\n"
                             "Rate = N*2+16");
        }

        if (ImGui::SliderInt("Sustain Level", &sustainLevel, 0, 7)) {
            // Clear bits 5-7 and set new sustain level (bits 5-7)
            instrumentDraft_.adsr2 = (instrumentDraft_.adsr2 & 0x1F) | ((sustainLevel & 0x07) << 5);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Sustain level (0=silent, 7=loud). Volume level held while note is playing.\n"
                             "Boundary = (N+1)*256");
        }

        if (ImGui::SliderInt("Sustain Rate", &sustainRate, 0, 31)) {
            // Clear bits 0-4 and set new sustain rate (bits 0-4)
            instrumentDraft_.adsr2 = (instrumentDraft_.adsr2 & 0xE0) | (sustainRate & 0x1F);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Sustain rate (0=slowest, 31=fastest). How sustain level changes over time.\n"
                             "Rate = N");
        }

        ImGui::EndDisabled();

        ImGui::TextDisabled("ADSR1=$%02X  ADSR2=$%02X", instrumentDraft_.adsr1, instrumentDraft_.adsr2);
    } else {
        // GAIN Mode editing
        ImGui::BeginDisabled(false);

        if (ImGui::Checkbox("Custom GAIN", &customGain)) {
            instrumentDraft_.gain = (instrumentDraft_.gain & 0x7F) | (customGain ? 0x80 : 0x00);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0=Direct (fixed volume), 1=Custom (dynamic envelope)");
        }

        if (!customGain) {
            // Direct Gain: bits 0-6 = volume
            if (ImGui::SliderInt("Volume", &directVolume, 0, 127)) {
                instrumentDraft_.gain = directVolume & 0x7F;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Fixed volume level. Envelope Level = N*16");
            }
        } else {
            // Custom Gain: bits 5-6 = mode, bits 0-4 = rate
            const char* gainModes[] = {"Linear Decrease", "Exp Decrease", "Linear Increase", "Bent Increase"};
            if (ImGui::Combo("Mode", &gainMode, gainModes, 4)) {
                instrumentDraft_.gain = 0x80 | ((gainMode & 0x03) << 5) | (gainRate & 0x1F);
            }
            if (ImGui::IsItemHovered()) {
                const char* tooltips[] = {
                    "Linear Decrease: Rate=N, Step=-32",
                    "Exponential Decrease: Rate=N, Step=-(((Level-1)>>8)+1)",
                    "Linear Increase: Rate=N, Step=+32",
                    "Bent Increase: Rate=N, Step=+32 if Level<0x600, else +8"};
                ImGui::SetTooltip("%s", tooltips[gainMode]);
            }

            if (ImGui::SliderInt("Rate", &gainRate, 0, 31)) {
                instrumentDraft_.gain = 0x80 | ((gainMode & 0x03) << 5) | (gainRate & 0x1F);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Envelope rate (0=slowest, 31=fastest)");
            }
        }

        ImGui::EndDisabled();
        ImGui::TextDisabled("GAIN=$%02X", instrumentDraft_.gain);
    }

    // Envelope Visualization
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::Text("Envelope Preview");
    {
        // Re-decode values for visualization (they may have changed from sliders)
        const bool vizAdsrEnable = (instrumentDraft_.adsr1 & 0x80) != 0;
        const int vizAttack = instrumentDraft_.adsr1 & 0x0F;
        const int vizDecay = (instrumentDraft_.adsr1 >> 4) & 0x07;
        const int vizSustainLevel = (instrumentDraft_.adsr2 >> 5) & 0x07;
        const int vizSustainRate = instrumentDraft_.adsr2 & 0x1F;
        const bool vizCustomGain = (instrumentDraft_.gain & 0x80) != 0;
        const int vizDirectVolume = instrumentDraft_.gain & 0x7F;
        const int vizGainMode = (instrumentDraft_.gain >> 5) & 0x03;

        const ImVec2 canvasSize(400, 120);
        const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Background
        drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                                IM_COL32(30, 30, 30, 255));

        // Grid lines
        for (int i = 0; i <= 4; ++i) {
            const float y = canvasPos.y + (canvasSize.y * i / 4.0F);
            drawList->AddLine(ImVec2(canvasPos.x, y), ImVec2(canvasPos.x + canvasSize.x, y),
                              IM_COL32(60, 60, 60, 255), 1.0F);
        }

        if (vizAdsrEnable) {
            // Draw ADSR envelope curve with proper time scaling
            const float maxLevel = 0x7FF;
            const float sustainLevelValue = (vizSustainLevel + 1) * 0x100;

            // Calculate phase durations based on rates (higher rate = faster = shorter)
            // Using approximate relative timing (inverse of rate for simplicity)
            const int attackRate = vizAttack * 2 + 1;  // Attack: N*2+1
            const int decayRate = vizDecay * 2 + 16;   // Decay: N*2+16
            const int sustainRateValue = vizSustainRate;  // Sustain: N
            const int releaseRate = 31;  // Release: fixed at 31

            // Convert rates to relative durations (inverse relationship: slower rate = longer time)
            // Add +1 to avoid division by zero
            const float attackDuration = 100.0F / (attackRate + 1);
            const float decayDuration = 100.0F / (decayRate + 1);
            // Sustain duration also depends on rate: higher rate = faster decay = shorter duration
            const float sustainDuration = sustainRateValue > 0 ? 120.0F / (sustainRateValue + 1) : 80.0F;
            const float releaseDuration = 100.0F / (releaseRate + 1);

            // Calculate total available width and scale factors
            const float totalDuration = attackDuration + decayDuration + sustainDuration + releaseDuration;
            const float scale = (canvasSize.x - 20) / totalDuration;

            // Calculate widths
            const float attackWidth = attackDuration * scale;
            const float decayWidth = decayDuration * scale;
            const float sustainWidth = sustainDuration * scale;
            const float releaseWidth = releaseDuration * scale;

            // Y coordinates: top = max level, bottom = zero level
            const float zeroY = canvasPos.y + canvasSize.y;
            const float maxY = canvasPos.y;

            // Draw Attack phase (starts at zero, rises to max)
            const float attackEndX = canvasPos.x + attackWidth;
            drawList->AddLine(ImVec2(canvasPos.x, zeroY), ImVec2(attackEndX, maxY),
                              IM_COL32(100, 200, 255, 255), 2.0F);

            // Draw Decay phase (max to sustain level)
            const float sustainY = canvasPos.y + canvasSize.y * (1.0F - sustainLevelValue / maxLevel);
            const float decayEndX = attackEndX + decayWidth;
            drawList->AddLine(ImVec2(attackEndX, maxY), ImVec2(decayEndX, sustainY),
                              IM_COL32(100, 200, 255, 255), 2.0F);

            // Draw Sustain phase (decays toward zero at sustain rate)
            const float sustainEndX = decayEndX + sustainWidth;
            // Sustain rate determines how much it decays: higher rate = more decay toward zero
            // Rate 0 = flat, higher rates decay more steeply toward zero
            const float sustainDecayAmount = sustainRateValue > 0 ? (sustainLevelValue / maxLevel) * canvasSize.y * 0.6F : 0.0F;
            const float sustainEndY = std::min(sustainY + sustainDecayAmount, zeroY);
            drawList->AddLine(ImVec2(decayEndX, sustainY), ImVec2(sustainEndX, sustainEndY),
                              IM_COL32(100, 200, 255, 255), 2.0F);

            // Draw Release phase (from current level to zero)
            const float releaseEndX = sustainEndX + releaseWidth;
            drawList->AddLine(ImVec2(sustainEndX, sustainEndY), ImVec2(releaseEndX, zeroY),
                              IM_COL32(150, 150, 150, 255), 2.0F);

            // Labels at phase boundaries
            drawList->AddText(ImVec2(canvasPos.x + attackWidth * 0.5F - 5, zeroY - 15),
                              IM_COL32(200, 200, 200, 255), "A");
            drawList->AddText(ImVec2(attackEndX + decayWidth * 0.5F - 5, zeroY - 15),
                              IM_COL32(200, 200, 200, 255), "D");
            drawList->AddText(ImVec2(decayEndX + sustainWidth * 0.5F - 5, zeroY - 15),
                              IM_COL32(200, 200, 200, 255), "S");
            drawList->AddText(ImVec2(sustainEndX + releaseWidth * 0.5F - 5, zeroY - 15),
                              IM_COL32(150, 150, 150, 255), "R");
        } else {
            // Draw GAIN envelope curve
            if (!vizCustomGain) {
                // Direct gain - flat line at fixed level
                const float level = (vizDirectVolume * 16.0F) / 0x800;
                const float y = canvasPos.y + canvasSize.y * (1.0F - level);
                drawList->AddLine(ImVec2(canvasPos.x, y), ImVec2(canvasPos.x + canvasSize.x, y),
                                  IM_COL32(255, 200, 100, 255), 2.0F);
                drawList->AddText(ImVec2(canvasPos.x + 5, y - 15), IM_COL32(255, 200, 100, 255), "Direct");
            } else {
                // Custom gain - draw appropriate curve
                const float startY = canvasPos.y + canvasSize.y * 0.5F;
                const float endX = canvasPos.x + canvasSize.x - 20.0F;

                if (vizGainMode == 0 || vizGainMode == 1) {
                    // Decreasing modes
                    const float endY = canvasPos.y + canvasSize.y;
                    if (vizGainMode == 0) {
                        // Linear decrease
                        drawList->AddLine(ImVec2(canvasPos.x, startY), ImVec2(endX, endY),
                                          IM_COL32(255, 150, 100, 255), 2.0F);
                        drawList->AddText(ImVec2(canvasPos.x + 5, canvasPos.y + canvasSize.y - 15),
                                          IM_COL32(255, 150, 100, 255), "Linear Dec");
                    } else {
                        // Exponential decrease
                        for (float x = 0; x < canvasSize.x - 20; x += 5.0F) {
                            const float t = x / (canvasSize.x - 20);
                            const float curve = 1.0F - (1.0F - std::exp(-3.0F * t));
                            const float y1 = startY + (endY - startY) * (x > 0 ? std::exp(-3.0F * (x - 5) / (canvasSize.x - 20)) : 0);
                            const float y2 = startY + (endY - startY) * curve;
                            drawList->AddLine(ImVec2(canvasPos.x + x - 5, y1), ImVec2(canvasPos.x + x, y2),
                                              IM_COL32(255, 150, 100, 255), 2.0F);
                        }
                        drawList->AddText(ImVec2(canvasPos.x + 5, canvasPos.y + canvasSize.y - 15),
                                          IM_COL32(255, 150, 100, 255), "Exp Dec");
                    }
                } else {
                    // Increasing modes
                    const float endY = canvasPos.y;
                    if (vizGainMode == 2) {
                        // Linear increase
                        drawList->AddLine(ImVec2(canvasPos.x, startY), ImVec2(endX, endY),
                                          IM_COL32(100, 255, 150, 255), 2.0F);
                        drawList->AddText(ImVec2(canvasPos.x + 5, canvasPos.y + canvasSize.y - 15),
                                          IM_COL32(100, 255, 150, 255), "Linear Inc");
                    } else {
                        // Bent increase (fast then slow)
                        const float bentX = canvasSize.x * 0.7F;
                        drawList->AddLine(ImVec2(canvasPos.x, startY), ImVec2(canvasPos.x + bentX, endY),
                                          IM_COL32(100, 255, 150, 255), 2.0F);
                        drawList->AddLine(ImVec2(canvasPos.x + bentX, endY), ImVec2(endX, endY),
                                          IM_COL32(100, 255, 150, 255), 2.0F);
                        drawList->AddText(ImVec2(canvasPos.x + 5, canvasPos.y + canvasSize.y - 15),
                                          IM_COL32(100, 255, 150, 255), "Bent Inc");
                    }
                }
            }
        }

        ImGui::Dummy(canvasSize);
    }

    ImGui::Separator();
    ImGui::Text("Pitch Tuning");

    // Display current pitch as combined value
    uint16_t currentPitch = (static_cast<uint16_t>(instrumentDraft_.basePitchMult) << 8) |
                            static_cast<uint16_t>(instrumentDraft_.fracPitchMult);
    ImGui::Text("Current Pitch: 0x%04X (%.3fx)", currentPitch, currentPitch / 256.0);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("0x0100 (256) = 1.0x = standard playback rate");
    }

    byteInput("Base Pitch", instrumentDraft_.basePitchMult, 0, 255);
    byteInput("Frac Pitch", instrumentDraft_.fracPitchMult, 0, 255);

    // Quick tuning buttons
    if (ImGui::Button("Reset to 1:1")) {
        instrumentDraft_.basePitchMult = 0x01;
        instrumentDraft_.fracPitchMult = 0x00;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Set to standard 1:1 playback rate (0x0100)");
    }

    ImGui::SameLine();
    if (ImGui::Button("Octave Up")) {
        uint16_t pitch = (static_cast<uint16_t>(instrumentDraft_.basePitchMult) << 8) |
                         static_cast<uint16_t>(instrumentDraft_.fracPitchMult);
        pitch = std::min(0xFFFFu, static_cast<unsigned int>(pitch * 2u));  // Double pitch, clamp to max
        instrumentDraft_.basePitchMult = static_cast<uint8_t>(pitch >> 8);
        instrumentDraft_.fracPitchMult = static_cast<uint8_t>(pitch & 0xFF);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Transpose up by one octave (2x pitch)");
    }

    ImGui::SameLine();
    if (ImGui::Button("Octave Down")) {
        uint16_t pitch = (static_cast<uint16_t>(instrumentDraft_.basePitchMult) << 8) |
                         static_cast<uint16_t>(instrumentDraft_.fracPitchMult);
        pitch = pitch / 2u;  // Half pitch
        instrumentDraft_.basePitchMult = static_cast<uint8_t>(pitch >> 8);
        instrumentDraft_.fracPitchMult = static_cast<uint8_t>(pitch & 0xFF);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Transpose down by one octave (0.5x pitch)");
    }

    // Semitone transpose
    ImGui::Text("Fine Tuning:");
    static int transposeAmount = 0;
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("##transpose", &transposeAmount, 1, 12)) {
        transposeAmount = std::clamp(transposeAmount, -48, 48);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Semitones to transpose (-48 to +48)");
    }

    ImGui::SameLine();
    ImGui::Text("semitones");
    ImGui::SameLine();
    if (ImGui::Button("Apply Transpose")) {
        if (transposeAmount != 0) {
            uint16_t pitch = (static_cast<uint16_t>(instrumentDraft_.basePitchMult) << 8) |
                             static_cast<uint16_t>(instrumentDraft_.fracPitchMult);
            double ratio = std::pow(2.0, transposeAmount / 12.0);
            pitch = static_cast<uint16_t>(std::clamp(pitch * ratio, 0.0, 65535.0));
            instrumentDraft_.basePitchMult = static_cast<uint8_t>(pitch >> 8);
            instrumentDraft_.fracPitchMult = static_cast<uint8_t>(pitch & 0xFF);
            transposeAmount = 0;  // Reset after applying
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Transpose by the specified number of semitones");
    }

    // TODO: Fix this properly later, we can't reliably tune since we don't know the sample rate of the original sample.
    // // Auto-tune button
    // ImGui::Spacing();
    // ImGui::Text("Automatic Tuning:");
    // if (ImGui::Button("Auto-Tune to C-4")) {
    //     if (!appState_.project.has_value()) {
    //         setStatus("No project loaded");
    //     } else {
    //         const auto& samples = appState_.project->samples();
    //         const auto sampleIt = std::find_if(samples.begin(), samples.end(),
    //                                           [&](const nspc::BrrSample& s) {
    //                                               return s.id == instrumentDraft_.sampleIndex;
    //                                           });

    //         if (sampleIt == samples.end()) {
    //             setStatus("Sample not found for auto-tune");
    //         } else {
    //             // Detect pitch (assuming 32000 Hz sample rate, common for SNES)
    //             constexpr uint32_t kAssumedSampleRate = 32000;
    //             auto detectedFreq = detectSamplePitch(sampleIt->data, kAssumedSampleRate);

    //             if (!detectedFreq.has_value()) {
    //                 setStatus("Could not detect pitch - sample may be too short or non-tonal");
    //             } else {
    //                 // Convert detected frequency to MIDI note
    //                 // MIDI note = 69 + 12 * log2(freq / 440)
    //                 const double midiNote = 69.0 + 12.0 * std::log2(*detectedFreq / 440.0);

    //                 // Calculate semitones from C-4 (MIDI note 60)
    //                 const double semitonesFromC4 = 60.0 - midiNote;

    //                 // Calculate required pitch multiplier
    //                 // Start from standard 0x0100 (1:1 rate) and transpose
    //                 const double ratio = std::pow(2.0, semitonesFromC4 / 12.0);
    //                 uint16_t newPitch = static_cast<uint16_t>(std::clamp(0x0100 * ratio, 0.0, 65535.0));

    //                 instrumentDraft_.basePitchMult = static_cast<uint8_t>(newPitch >> 8);
    //                 instrumentDraft_.fracPitchMult = static_cast<uint8_t>(newPitch & 0xFF);

    //                 setStatus(std::format("Auto-tuned: detected {:.1f} Hz ({:.1f} semitones from C-4), set pitch to 0x{:04X}",
    //                                     *detectedFreq, semitonesFromC4, newPitch));
    //             }
    //         }
    //     }
    // }
    // if (ImGui::IsItemHovered()) {
    //     ImGui::SetTooltip("Automatically detect sample pitch and calculate multiplier so C-4 plays as C-4.\n"
    //                      "Uses autocorrelation analysis on the sample data.");
    // }

    ImGui::Separator();

    if (ImGui::Button("Preview")) {
        previewInstrument(instrumentDraft_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        stopPreview();
    }

    ImGui::Separator();

    if (ImGui::Button("Cancel")) {
        instrumentEditorOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(instrumentDraftLocked);
    if (ImGui::Button("Save")) {
        if (saveInstrumentDraft()) {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();

    ImGui::EndPopup();
}

void AssetsPanel::drawSampleEditor() {
    if (sampleEditorOpen_ && !ImGui::IsPopupOpen(kSampleEditorPopupId)) {
        ImGui::OpenPopup(kSampleEditorPopupId);
    }

    if (!ImGui::BeginPopupModal(kSampleEditorPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (sampleEditorOpen_ && !ImGui::IsPopupOpen(kSampleEditorPopupId)) {
            sampleEditorOpen_ = false;
        }
        return;
    }

    ImGui::Text("Sample %02X", std::max(sampleDraft_.id, 0));
    const bool sampleDraftLocked =
        !sampleEditorIsNew_ && appState_.project.has_value() &&
        [&]() {
            if (const auto index = findSampleIndexById(sampleDraft_.id); index.has_value()) {
                return appState_.lockEngineContent &&
                       appState_.project->samples()[*index].contentOrigin == nspc::NspcContentOrigin::EngineProvided;
            }
            return false;
        }();

    if (sampleDraftLocked) {
        ImGui::TextDisabled("Engine sample is locked from edits.");
    }
    ImGui::BeginDisabled(sampleDraftLocked);

    char nameBuffer[128]{};
    std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", sampleDraft_.name.c_str());
    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
        sampleDraft_.name = nameBuffer;
    }

    const int blockCount = static_cast<int>(sampleDraft_.brrData.size() / 9u);
    ImGui::TextDisabled("BRR bytes: %zu (%d blocks)", sampleDraft_.brrData.size(), blockCount);

    ImGui::Checkbox("Loop Enabled", &sampleDraft_.loopEnabled);
    if (sampleDraft_.loopEnabled && blockCount > 0) {
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputInt("Loop Block", &sampleDraft_.loopBlock, 1, 8);
        sampleDraft_.loopBlock = std::clamp(sampleDraft_.loopBlock, 0, blockCount - 1);
    } else {
        sampleDraft_.loopBlock = 0;
    }

    ImGui::Separator();
    ImGui::Text("WAV Import Settings");
    ImGui::SetNextItemWidth(200.0f);
    const char* sampleRateOptions[] = {"32000 Hz (Full)", "16000 Hz (Half)", "8000 Hz (Quarter)"};
    int currentRateIndex = 0;
    if (sampleDraft_.targetSampleRate == 16000) {
        currentRateIndex = 1;
    } else if (sampleDraft_.targetSampleRate == 8000) {
        currentRateIndex = 2;
    }
    const bool sampleRateChanged = ImGui::Combo("Sample Rate", &currentRateIndex, sampleRateOptions, 3);
    if (sampleRateChanged) {
        switch (currentRateIndex) {
        case 0:
            sampleDraft_.targetSampleRate = 32000;
            break;
        case 1:
            sampleDraft_.targetSampleRate = 16000;
            break;
        case 2:
            sampleDraft_.targetSampleRate = 8000;
            break;
        }
    }
    ImGui::TextDisabled("Lower sample rates produce smaller BRR files");
    const bool hqChanged = ImGui::Checkbox("High Quality Resampling", &sampleDraft_.highQualityResampling);
    ImGui::Checkbox("Treble Compensation (Gaussian)", &sampleDraft_.enhanceTrebleOnEncode);

    if (!sampleDraft_.wavSourcePcm.empty()) {
        if (sampleRateChanged || hqChanged) {
            (void)refreshWavSourcePcmForDraft(sampleDraft_);
        }

        ImGui::Separator();
        ImGui::Text("WAV Sample Tools");
        const int sourceCount = static_cast<int>(sampleDraft_.wavSourcePcm.size());
        sampleDraft_.wavTrimStartSample = std::clamp(sampleDraft_.wavTrimStartSample, 0, std::max(sourceCount - 1, 0));
        sampleDraft_.wavTrimEndSample = std::clamp(sampleDraft_.wavTrimEndSample, sampleDraft_.wavTrimStartSample + 1, sourceCount);
        sampleDraft_.wavLoopSample = std::clamp(sampleDraft_.wavLoopSample, sampleDraft_.wavTrimStartSample, sampleDraft_.wavTrimEndSample - 1);

        ImGui::TextDisabled("Source samples: %d (%.2fs @ %uHz)", sourceCount,
                            static_cast<float>(sourceCount) / static_cast<float>(std::max<uint32_t>(sampleDraft_.targetSampleRate, 1u)),
                            sampleDraft_.targetSampleRate);

        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputInt("Start Sample", &sampleDraft_.wavTrimStartSample, 1, 64);
        sampleDraft_.wavTrimStartSample = std::clamp(sampleDraft_.wavTrimStartSample, 0, std::max(sourceCount - 1, 0));

        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputInt("End Sample", &sampleDraft_.wavTrimEndSample, 1, 64);
        sampleDraft_.wavTrimEndSample = std::clamp(sampleDraft_.wavTrimEndSample, sampleDraft_.wavTrimStartSample + 1, sourceCount);

        if (sampleDraft_.loopEnabled) {
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputInt("Loop Sample", &sampleDraft_.wavLoopSample, 1, 64);
            sampleDraft_.wavLoopSample =
                std::clamp(sampleDraft_.wavLoopSample, sampleDraft_.wavTrimStartSample, sampleDraft_.wavTrimEndSample - 1);
        } else {
            sampleDraft_.wavLoopSample = sampleDraft_.wavTrimStartSample;
        }

        const int trimmedCount = sampleDraft_.wavTrimEndSample - sampleDraft_.wavTrimStartSample;
        ImGui::TextDisabled("Trimmed region: %d samples (%.2fs)", trimmedCount,
                            static_cast<float>(trimmedCount) /
                                static_cast<float>(std::max<uint32_t>(sampleDraft_.targetSampleRate, 1u)));

        static int markerMode = 0;  // 0=start, 1=loop, 2=end
        ImGui::TextDisabled("Waveform click target:");
        ImGui::SameLine();
        ImGui::RadioButton("Start", &markerMode, 0);
        ImGui::SameLine();
        ImGui::BeginDisabled(!sampleDraft_.loopEnabled);
        ImGui::RadioButton("Loop", &markerMode, 1);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::RadioButton("End", &markerMode, 2);

        std::vector<float> wavPlot;
        const size_t wavMaxPoints = 2048;
        const size_t wavSourceSize = sampleDraft_.wavSourcePcm.size();
        const size_t wavPointCount = std::min(wavSourceSize, wavMaxPoints);
        wavPlot.resize(wavPointCount);
        for (size_t i = 0; i < wavPointCount; ++i) {
            const size_t sourceIndex = (wavSourceSize <= 1)
                                           ? 0
                                           : (i * (wavSourceSize - 1)) / std::max<size_t>(wavPointCount - 1, 1);
            wavPlot[i] = static_cast<float>(sampleDraft_.wavSourcePcm[sourceIndex]) / 32768.0f;
        }

        ImGui::PlotLines("##WavSourceWave", wavPlot.data(), static_cast<int>(wavPlot.size()), 0, nullptr, -1.0f, 1.0f,
                         ImVec2(520.0f, 120.0f));
        const ImVec2 wavPlotMin = ImGui::GetItemRectMin();
        const ImVec2 wavPlotMax = ImGui::GetItemRectMax();
        const float wavPlotWidth = std::max(1.0f, wavPlotMax.x - wavPlotMin.x);

        const auto sampleToX = [&](int sample) {
            if (sourceCount <= 1) {
                return wavPlotMin.x;
            }
            const float t = static_cast<float>(std::clamp(sample, 0, sourceCount - 1)) /
                            static_cast<float>(sourceCount - 1);
            return wavPlotMin.x + t * wavPlotWidth;
        };

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float startX = sampleToX(sampleDraft_.wavTrimStartSample);
        const float endX = sampleToX(std::max(sampleDraft_.wavTrimEndSample - 1, sampleDraft_.wavTrimStartSample));
        drawList->AddLine(ImVec2(startX, wavPlotMin.y), ImVec2(startX, wavPlotMax.y), IM_COL32(100, 220, 255, 255), 2.0f);
        drawList->AddLine(ImVec2(endX, wavPlotMin.y), ImVec2(endX, wavPlotMax.y), IM_COL32(255, 110, 110, 255), 2.0f);
        if (sampleDraft_.loopEnabled) {
            const float loopX = sampleToX(sampleDraft_.wavLoopSample);
            drawList->AddLine(ImVec2(loopX, wavPlotMin.y), ImVec2(loopX, wavPlotMax.y), IM_COL32(255, 210, 110, 255), 2.0f);
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && sourceCount > 1) {
            const float mouseX = ImGui::GetIO().MousePos.x;
            const float t = std::clamp((mouseX - wavPlotMin.x) / wavPlotWidth, 0.0f, 1.0f);
            const int clickedSample = static_cast<int>(std::lround(t * static_cast<float>(sourceCount - 1)));
            if (markerMode == 0) {
                sampleDraft_.wavTrimStartSample = std::min(clickedSample, sampleDraft_.wavTrimEndSample - 1);
                sampleDraft_.wavLoopSample =
                    std::clamp(sampleDraft_.wavLoopSample, sampleDraft_.wavTrimStartSample, sampleDraft_.wavTrimEndSample - 1);
            } else if (markerMode == 1 && sampleDraft_.loopEnabled) {
                sampleDraft_.wavLoopSample = std::clamp(clickedSample, sampleDraft_.wavTrimStartSample, sampleDraft_.wavTrimEndSample - 1);
            } else {
                sampleDraft_.wavTrimEndSample = std::max(clickedSample + 1, sampleDraft_.wavTrimStartSample + 1);
                sampleDraft_.wavLoopSample =
                    std::clamp(sampleDraft_.wavLoopSample, sampleDraft_.wavTrimStartSample, sampleDraft_.wavTrimEndSample - 1);
            }
        }

        ImGui::TextDisabled("Re-encode is automatic on Preview and Save.");
        if (ImGui::Button("Reset Range")) {
            sampleDraft_.wavTrimStartSample = 0;
            sampleDraft_.wavTrimEndSample = sourceCount;
            sampleDraft_.wavLoopSample = 0;
            if (rebuildWavSampleDraftBrr(sampleDraft_)) {
                setStatus("Reset WAV trim range and re-encoded BRR");
            }
        }
    }

    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::Button("Import WAV...")) {
        (void)importWavIntoSampleDraft(sampleDraft_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Import BRR...")) {
        (void)importBrrIntoSampleDraft(sampleDraft_);
    }
    ImGui::SameLine();
    const bool canPreview = !sampleDraft_.brrData.empty() || !sampleDraft_.wavSourcePcm.empty();
    ImGui::BeginDisabled(!canPreview);
    if (ImGui::Button("Preview")) {
        bool previewReady = true;
        if (!sampleDraft_.wavSourcePcm.empty()) {
            previewReady = rebuildWavSampleDraftBrr(sampleDraft_);
        }
        if (previewReady) {
            previewSample(sampleDraft_);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        stopPreview();
    }

    if (!sampleWavePreview_.empty()) {
        std::vector<float> plot;
        const size_t maxPoints = 2048;
        const size_t sourceSize = sampleWavePreview_.size();
        const size_t pointCount = std::min(sourceSize, maxPoints);
        plot.resize(pointCount);

        for (size_t i = 0; i < pointCount; ++i) {
            const size_t sourceIndex = (sourceSize <= 1)
                                           ? 0
                                           : (i * (sourceSize - 1)) / std::max<size_t>(pointCount - 1, 1);
            plot[i] = static_cast<float>(sampleWavePreview_[sourceIndex]) / 32768.0f;
        }

        ImGui::PlotLines("##SampleWave", plot.data(), static_cast<int>(plot.size()), 0, nullptr, -1.0f, 1.0f,
                         ImVec2(520.0f, 140.0f));

        if (sampleDraft_.loopEnabled && !sampleWavePreview_.empty() && blockCount > 0) {
            const int loopSample = std::clamp(sampleDraft_.loopBlock * 16, 0, static_cast<int>(sampleWavePreview_.size()) - 1);
            const float t = static_cast<float>(loopSample) / static_cast<float>(sampleWavePreview_.size() - 1);
            const ImVec2 plotMin = ImGui::GetItemRectMin();
            const ImVec2 plotMax = ImGui::GetItemRectMax();
            const float loopX = plotMin.x + t * (plotMax.x - plotMin.x);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(loopX, plotMin.y), ImVec2(loopX, plotMax.y),
                                                IM_COL32(255, 170, 90, 255), 2.0f);
            ImGui::TextDisabled("Loop marker at block %d", sampleDraft_.loopBlock);
        }
    } else {
        ImGui::TextDisabled("No waveform preview yet (import WAV or edit an existing BRR sample)");
    }

    ImGui::Separator();

    if (ImGui::Button("Cancel")) {
        sampleEditorOpen_ = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    const bool canSave = !sampleDraft_.brrData.empty() || !sampleDraft_.wavSourcePcm.empty();
    ImGui::BeginDisabled(!canSave || sampleDraftLocked);
    if (ImGui::Button("Save")) {
        if (saveSampleDraft()) {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();

    ImGui::EndPopup();
}

bool AssetsPanel::saveInstrumentDraft() {
    if (!appState_.project.has_value()) {
        setStatus("No project loaded");
        return false;
    }

    auto& project = *appState_.project;
    auto& instruments = project.instruments();
    if (!instrumentEditorIsNew_) {
        if (const auto index = findInstrumentIndexById(instrumentDraft_.id); index.has_value() &&
            appState_.lockEngineContent &&
            instruments[*index].contentOrigin == nspc::NspcContentOrigin::EngineProvided) {
            setStatus(std::format("Instrument {:02X} is engine-owned and locked", instrumentDraft_.id));
            return false;
        }
    }

    nspc::NspcInstrument instrument{};
    instrument.id = instrumentDraft_.id;
    instrument.name = instrumentDraft_.name;
    instrument.sampleIndex = static_cast<uint8_t>(instrumentDraft_.sampleIndex & 0x7F);
    instrument.adsr1 = instrumentDraft_.adsr1;
    instrument.adsr2 = instrumentDraft_.adsr2;
    instrument.gain = instrumentDraft_.gain;
    instrument.basePitchMult = instrumentDraft_.basePitchMult;
    instrument.fracPitchMult = instrumentDraft_.fracPitchMult;
    instrument.percussionNote = 0;
    instrument.contentOrigin = nspc::NspcContentOrigin::UserProvided;

    if (!instrumentEditorIsNew_) {
        if (const auto existingIndex = findInstrumentIndexById(instrument.id); existingIndex.has_value()) {
            instrument.percussionNote = instruments[*existingIndex].percussionNote;
        }
    }

    const auto& config = project.engineConfig();
    const uint8_t entrySize = std::clamp<uint8_t>(config.instrumentEntryBytes, 5, 6);
    if (config.instrumentHeaders != 0) {
        const uint32_t address32 =
            static_cast<uint32_t>(config.instrumentHeaders) + static_cast<uint32_t>(instrument.id) * entrySize;
        if (address32 + entrySize <= kAramSize) {
            instrument.originalAddr = static_cast<uint16_t>(address32);
        }
    }

    if (instrumentEditorIsNew_) {
        if (findInstrumentIndexById(instrument.id).has_value()) {
            setStatus(std::format("Instrument {:02X} already exists", instrument.id));
            return false;
        }
        instruments.push_back(instrument);
    } else {
        const auto index = findInstrumentIndexById(instrument.id);
        if (!index.has_value()) {
            setStatus("Selected instrument no longer exists");
            return false;
        }
        instruments[*index] = instrument;
    }

    sortById(instruments);
    if (!writeInstrumentToAram(instrument)) {
        return false;
    }

    project.refreshAramUsage();
    selectedInstrumentId_ = instrument.id;
    appState_.selectedInstrumentId = selectedInstrumentId_;
    instrumentEditorOpen_ = false;
    setStatus(std::format("Saved instrument {:02X}", instrument.id));
    return true;
}

bool AssetsPanel::saveSampleDraft() {
    if (!appState_.project.has_value()) {
        setStatus("No project loaded");
        return false;
    }

    auto& project = *appState_.project;
    auto& samples = project.samples();
    if (!sampleEditorIsNew_) {
        if (const auto index = findSampleIndexById(sampleDraft_.id); index.has_value() &&
            appState_.lockEngineContent &&
            samples[*index].contentOrigin == nspc::NspcContentOrigin::EngineProvided) {
            setStatus(std::format("Sample {:02X} is engine-owned and locked", sampleDraft_.id));
            return false;
        }
    }

    if (!sampleDraft_.wavSourcePcm.empty()) {
        if (!rebuildWavSampleDraftBrr(sampleDraft_)) {
            return false;
        }
    }

    std::optional<int> replaceId = sampleEditorIsNew_ ? std::nullopt : std::optional<int>(sampleDraft_.id);
    std::optional<uint16_t> preferredAddr = std::nullopt;
    if (replaceId.has_value()) {
        if (const auto index = findSampleIndexById(*replaceId); index.has_value()) {
            preferredAddr = samples[*index].originalAddr;
        }
    }

    const auto allocatedAddr = allocateSampleAddress(sampleDraft_.brrData.size(), replaceId, preferredAddr);
    if (!allocatedAddr.has_value()) {
        setStatus("No free ARAM range for sample data");
        return false;
    }

    nspc::BrrSample sample{};
    sample.id = sampleDraft_.id;
    sample.name = sampleDraft_.name;
    sample.data = sampleDraft_.brrData;
    sample.originalAddr = *allocatedAddr;
    sample.contentOrigin = nspc::NspcContentOrigin::UserProvided;

    const int finalBlockCount = static_cast<int>(sample.data.size() / 9u);
    const int finalLoopBlock = sampleDraft_.loopEnabled
                                   ? std::clamp(sampleDraft_.loopBlock, 0, std::max(finalBlockCount - 1, 0))
                                   : 0;
    sample.originalLoopAddr = sampleDraft_.loopEnabled
                                  ? static_cast<uint16_t>(sample.originalAddr + static_cast<uint16_t>(finalLoopBlock * 9))
                                  : sample.originalAddr;

    if (!writeSampleDirectoryEntry(sample.id, sample.originalAddr, sample.originalLoopAddr)) {
        return false;
    }
    if (!writeSampleDataToAram(sample)) {
        return false;
    }

    if (sampleEditorIsNew_) {
        if (findSampleIndexById(sample.id).has_value()) {
            setStatus(std::format("Sample {:02X} already exists", sample.id));
            return false;
        }
        samples.push_back(std::move(sample));
    } else {
        const auto index = findSampleIndexById(sample.id);
        if (!index.has_value()) {
            setStatus("Selected sample no longer exists");
            return false;
        }
        samples[*index] = std::move(sample);
    }

    sortById(samples);
    project.refreshAramUsage();
    selectedSampleId_ = sampleDraft_.id;
    sampleEditorOpen_ = false;
    setStatus(std::format("Saved sample {:02X}", sampleDraft_.id));
    return true;
}

void AssetsPanel::draw() {
    if (!appState_.project.has_value()) {
        stopInstrumentKeyboardPreview();
        ImGui::TextDisabled("No project loaded");
        ImGui::TextDisabled("Import an SPC to edit instruments and samples");
        return;
    }

    bool instrumentsTabActive = false;
    if (ImGui::BeginTabBar("AssetsTabs")) {
        if (ImGui::BeginTabItem("Instruments")) {
            instrumentsTabActive = true;
            drawInstrumentsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Samples")) {
            drawSamplesTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (!instrumentsTabActive) {
        stopInstrumentKeyboardPreview();
    }

    drawInstrumentEditor();
    drawSampleEditor();

    if (!status_.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", status_.c_str());
    }
}

std::optional<double> AssetsPanel::detectSamplePitch(const std::vector<uint8_t>& brrData, uint32_t assumedSampleRate) {
    // Decode BRR to PCM first
    auto pcmResult = nspc::decodeBrrToPcm(brrData);
    if (!pcmResult.has_value() || pcmResult->empty()) {
        return std::nullopt;
    }

    const auto& pcm = *pcmResult;
    const size_t numSamples = pcm.size();

    // Use only first portion for analysis (up to 8192 samples for speed)
    const size_t analysisSamples = std::min(numSamples, size_t(8192));

    // Autocorrelation-based pitch detection
    // Find the lag with the highest correlation (excluding lag 0)
    const size_t minLag = assumedSampleRate / 2000;  // Max 2000 Hz
    const size_t maxLag = assumedSampleRate / 50;    // Min 50 Hz

    if (maxLag >= analysisSamples) {
        return std::nullopt;  // Sample too short for reliable detection
    }

    double maxCorrelation = -1.0;
    size_t bestLag = 0;

    for (size_t lag = minLag; lag < std::min(maxLag, analysisSamples / 2); ++lag) {
        double correlation = 0.0;
        double sumSq1 = 0.0;
        double sumSq2 = 0.0;

        for (size_t i = 0; i < analysisSamples - lag; ++i) {
            const double s1 = static_cast<double>(pcm[i]);
            const double s2 = static_cast<double>(pcm[i + lag]);
            correlation += s1 * s2;
            sumSq1 += s1 * s1;
            sumSq2 += s2 * s2;
        }

        // Normalized correlation coefficient
        if (sumSq1 > 0.0 && sumSq2 > 0.0) {
            correlation /= std::sqrt(sumSq1 * sumSq2);

            if (correlation > maxCorrelation) {
                maxCorrelation = correlation;
                bestLag = lag;
            }
        }
    }

    // Require reasonable correlation threshold
    if (maxCorrelation < 0.5 || bestLag == 0) {
        return std::nullopt;  // No clear pitch detected
    }

    // Calculate frequency from the period (lag)
    const double frequency = static_cast<double>(assumedSampleRate) / static_cast<double>(bestLag);
    return frequency;
}

}  // namespace ntrak::ui
