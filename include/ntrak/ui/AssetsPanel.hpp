#pragma once

#include "ntrak/app/AppState.hpp"
#include "ntrak/nspc/NspcData.hpp"
#include "ntrak/ui/Panel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ntrak::ui {

class AssetsPanel : public Panel {
public:
    explicit AssetsPanel(app::AppState& appState);
    ~AssetsPanel() override = default;

    void draw() override;
    const char* title() const override { return "Assets"; }

private:
    app::AppState& appState_;

    int selectedInstrumentId_ = -1;
    int selectedSampleId_ = -1;

    struct InstrumentDraft {
        int id = -1;
        std::string name;
        uint8_t sampleIndex = 0;
        uint8_t adsr1 = 0x8F;
        uint8_t adsr2 = 0xE0;
        uint8_t gain = 0x7F;
        uint8_t basePitchMult = 0x01;
        uint8_t fracPitchMult = 0x00;
    };

    struct SampleDraft {
        int id = -1;
        std::string name;
        std::vector<uint8_t> brrData;
        std::vector<int16_t> wavSourcePcm;
        std::string wavSourcePath;
        uint32_t wavDecodedSampleRate = 0;
        bool wavDecodedHighQuality = true;
        int wavTrimStartSample = 0;
        int wavTrimEndSample = 0;  // exclusive
        int wavLoopSample = 0;
        uint16_t originalAddr = 0;
        uint16_t loopAddr = 0;
        bool loopEnabled = false;
        int loopBlock = 0;
        uint32_t targetSampleRate = 32000;
        bool highQualityResampling = true;
        bool enhanceTrebleOnEncode = true;
    };

    struct TrackerPitchInput {
        int pitch = 0;
        int key = 0;
    };

    bool instrumentEditorOpen_ = false;
    bool instrumentEditorIsNew_ = false;
    InstrumentDraft instrumentDraft_;

    bool sampleEditorOpen_ = false;
    bool sampleEditorIsNew_ = false;
    SampleDraft sampleDraft_;
    std::vector<int16_t> sampleWavePreview_;

    std::string status_;
    std::optional<int> activeInstrumentPreviewKey_;
    bool instrumentKeyboardPreviewActive_ = false;

    void drawInstrumentsTab();
    void drawSamplesTab();
    void drawInstrumentEditor();
    void drawSampleEditor();
    bool saveInstrumentDraft();
    bool saveSampleDraft();

    void setStatus(std::string message);
    void syncSourceSpcRange(uint16_t aramAddress, size_t size);
    void syncProjectAramToPreviewPlayer();
    void previewInstrument(const InstrumentDraft& draft);
    void previewSample(const SampleDraft& draft);
    void stopPreview();
    std::optional<TrackerPitchInput> consumeTrackerPitchInput() const;
    void startInstrumentKeyboardPreview(int midiPitch, int key);
    void stopInstrumentKeyboardPreview();
    void handleInstrumentKeyboardPreview();

    std::optional<size_t> findInstrumentIndexById(int id) const;
    std::optional<size_t> findSampleIndexById(int id) const;

    bool writeInstrumentToAram(const nspc::NspcInstrument& instrument);
    void clearInstrumentEntryInAram(int instrumentId);
    bool writeSampleDirectoryEntry(int sampleId, uint16_t startAddr, uint16_t loopAddr);
    bool writeSampleDataToAram(const nspc::BrrSample& sample);
    std::optional<uint16_t> allocateSampleAddress(size_t size, std::optional<int> replaceSampleId = std::nullopt,
                                                  std::optional<uint16_t> preferred = std::nullopt);
    bool importNtiAsNewInstrument();
    bool exportSelectedInstrumentAsNti();
    bool importWavIntoSampleDraft(SampleDraft& draft);
    bool importBrrIntoSampleDraft(SampleDraft& draft);
    bool refreshWavSourcePcmForDraft(SampleDraft& draft);
    bool rebuildWavSampleDraftBrr(SampleDraft& draft);
    void clearWavSourceInSampleDraft(SampleDraft& draft);
    bool exportSelectedSampleAsBrr();
    void refreshSampleWavePreview();
    std::optional<double> detectSamplePitch(const std::vector<uint8_t>& brrData, uint32_t assumedSampleRate);
};

}  // namespace ntrak::ui
