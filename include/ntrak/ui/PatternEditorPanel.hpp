#pragma once

#include "ntrak/app/AppState.hpp"
#include "ntrak/nspc/NspcFlatten.hpp"
#include "ntrak/nspc/NspcEditor.hpp"
#include "ntrak/ui/Panel.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ntrak::nspc {
struct NspcEngineExtensionVcmd;
}

namespace ntrak::ui {

class PatternEditorPanel final : public Panel {
public:
    explicit PatternEditorPanel(app::AppState& appState);
    void draw() override;

    const char* title() const override { return "Pattern Editor"; }

private:
    static constexpr int kChannels = 8;
    static constexpr int kEditItems = 5;
    static constexpr int kDefaultVisibleRows = 128;
    static constexpr int kMaxVisibleRows = 4096;
    static constexpr int kMinTicksPerRow = 1;
    static constexpr int kMaxTicksPerRow = 8;

    struct EffectChip {
        std::string label;
        std::string tooltip;
        uint8_t category = 0;  // 0=other, 1=vol/pan, 2=pitch, 3=mod, 4=echo, 5=tempo
        uint8_t id = 0;
        std::array<uint8_t, 4> params{};
        uint8_t paramCount = 0;
        std::optional<int> subroutineId;
    };

    struct PatternCell {
        std::string note = "...";
        std::string instrument = "..";
        std::string volume = "..";
        std::string qv = "..";
        std::vector<EffectChip> effects;
        int subroutineId = -1;
        bool hasSubroutineData = false;
        bool isSubroutineStart = false;
        bool isSubroutineEnd = false;
        bool hasEndMarker = false;
        bool instrumentDerived = false;
        bool volumeDerived = false;
        bool qvDerived = false;
    };

    struct SelectionCell {
        int row = -1;
        int channel = -1;
        int item = 0;
    };

    struct ClipboardCell {
        int rowOffset = 0;
        int flatColumnOffset = 0;
        std::optional<nspc::NspcRowEvent> rowEvent;
        std::optional<uint8_t> byteValue;
        std::vector<EffectChip> effects;
    };

    struct TrackerPitchInput {
        int pitch = 0;
        int key = 0;
    };

    enum class InstrumentRemapScope : uint8_t {
        Global,
        Channel,
    };

    struct SongInstrumentRemapEntry {
        uint8_t source = 0;
        uint8_t target = 0;
        int uses = 0;
    };

    using PatternRow = std::array<PatternCell, kChannels>;

    std::optional<int> resolveSelectedPatternId(const nspc::NspcSong& song);
    void rebuildPatternRows(const nspc::NspcSong& song, int patternId);
    bool handleKeyboardEditing(nspc::NspcSong& song, int patternId);
    void requestFxEditorOpen(int row, int channel, int effectIndex = -1);
    void openFxEditorForCell(size_t row, int channel, int effectIndex = -1);
    bool applyFxEditorChanges(nspc::NspcSong& song, int patternId);
    void prepareFxEditorPopupRequest();
    bool beginFxEditorPopupModal();
    bool hasSelectedFxEditorEffect() const;
    void normalizeFxEditorSelection();
    bool rebuildFxEditorChipFromRaw(EffectChip& chip);
    std::optional<EffectChip> createDefaultFxEditorChipForId(uint8_t effectId) const;
    std::string fxEditorEffectName(const EffectChip& chip) const;
    std::string fxEditorEffectSummary(const EffectChip& chip) const;
    void drawFxEditorEffectList();
    void drawFxEditorSelectedEffectSection();
    void drawFxEditorAddSection();
    void drawFxEditorEditActions();
    bool applyFxEditorPopupChanges(nspc::NspcSong& song, int patternId, bool closeAfterApply);
    void drawFxEditorPopup(nspc::NspcSong& song, int patternId);
    EffectChip makeEffectChipFromVcmd(const nspc::Vcmd& cmd) const;
    std::optional<nspc::Vcmd> reconstructVcmdFromEffectChip(const EffectChip& chip) const;
    const nspc::NspcEngineExtensionVcmd* extensionVcmdInfoForCurrentEngine(uint8_t id) const;
    std::optional<uint8_t> extensionParamCountForCurrentEngine(uint8_t id) const;
    std::optional<uint8_t> fxParamCountForCurrentEngine(uint8_t id) const;
    std::optional<std::pair<uint8_t, size_t>> decodeTypedFxLeadForCurrentEngine(std::string_view hexDigits) const;
    bool drawFxTypePickerCombo(const char* label, uint8_t& selectedId) const;
    bool isEditableFxIdForCurrentEngine(uint8_t id) const;
    std::optional<nspc::Vcmd> buildVcmdFromRawForCurrentEngine(
        uint8_t id, const std::array<uint8_t, 4>& params,
        std::optional<uint8_t> explicitParamCount = std::nullopt) const;
    std::optional<int> resolveSubroutineIdForAddress(uint16_t address) const;
    std::optional<uint16_t> resolveSubroutineAddressForId(int subroutineId) const;
    bool rebuildSubroutineChip(EffectChip& chip) const;
    std::optional<int> selectedSubroutineIdForActions(const nspc::NspcSong& song) const;
    std::optional<std::pair<uint32_t, uint32_t>> selectedRowRangeForChannel(int channel) const;
    void clampSelectionToRows();
    bool appendTypedHexNibble();
    std::optional<TrackerPitchInput> consumeTrackerPitchInput() const;
    std::optional<uint8_t> selectedInstrumentForEntry();
    std::optional<uint8_t> effectiveInstrumentAtRow(int channel, uint32_t row) const;
    bool cycleSelectedInstrument(int direction);
    void advanceEditingCursor(int step, int maxRow);
    bool handleNoteColumnEditing(nspc::NspcSong& song, const nspc::NspcEditorLocation& location, int step, int maxRow);
    bool clearCurrentValueColumn(nspc::NspcSong& song, const nspc::NspcEditorLocation& location);
    bool handleValueColumnHexEditing(nspc::NspcSong& song, const nspc::NspcEditorLocation& location, int step,
                                     int maxRow);
    bool handleFxHexEditing(nspc::NspcSong& song, const nspc::NspcEditorLocation& location);
    std::optional<bool> handlePreNavigationShortcuts(nspc::NspcSong& song, int patternId, bool commandModifier);
    void handleNavigationKeys(bool commandModifier, int step, int maxRow, const SelectionCell& cursorBeforeMove);
    std::optional<bool> handlePostNavigationShortcuts(nspc::NspcSong& song, int patternId, bool commandModifier,
                                                       int step);
    bool handleDeleteSelectionShortcut(nspc::NspcSong& song, int patternId);
    void syncProjectAramToPreviewPlayer();
    void startTrackerPreview(int midiPitch, int key);
    void stopTrackerPreview();
    size_t selectionIndex(int row, int channel, int item) const;
    void ensureSelectionStorage();
    void clearCellSelection();
    bool hasCellSelection() const;
    bool isCellSelected(int row, int channel, int item) const;
    void setCellSelected(int row, int channel, int item, bool selected);
    void selectSingleCell(int row, int channel, int item, bool resetAnchor);
    void selectRange(const SelectionCell& anchor, const SelectionCell& focus, bool additive);
    void handleCellSelectionInput(int row, int channel, int item, bool clicked, bool hovered);
    void updateSelectionFromCursor(bool extending);
    std::optional<nspc::NspcRowEvent> parseRowEventFromCell(const PatternCell& cell) const;
    std::optional<uint8_t> parseHexByte(std::string_view text) const;
    bool copyCellSelectionToClipboard();
    bool pasteClipboardAtCursor(nspc::NspcSong& song, int patternId);
    bool clearSelectedCells(nspc::NspcSong& song, int patternId);

    // Selection operations
    bool transposeSelectedCells(nspc::NspcSong& song, int patternId, int semitones);
    bool setInstrumentOnSelection(nspc::NspcSong& song, int patternId, uint8_t instrument);
    bool setVolumeOnSelection(nspc::NspcSong& song, int patternId, uint8_t volume);
    bool interpolateSelectedCells(nspc::NspcSong& song, int patternId);
    void drawBulkValuePopup(bool& openFlag, const char* popupId, const char* inputId, const char* prompt,
                            bool instrumentMode, nspc::NspcSong& song, int patternId);
    void drawSetInstrumentPopup(nspc::NspcSong& song, int patternId);
    void drawSetVolumePopup(nspc::NspcSong& song, int patternId);
    void drawPatternLengthPopup(nspc::NspcSong& song, int patternId);
    void rebuildSongInstrumentRemapEntries(const nspc::NspcSong& song);
    void prepareSongInstrumentRemapPopup(const nspc::NspcSong& song);
    bool beginSongInstrumentRemapPopup();
    void drawSongInstrumentRemapScopeControls(const nspc::NspcSong& song);
    void drawSongInstrumentRemapEntriesTable();
    int countPendingSongInstrumentRemaps() const;
    void drawSongInstrumentRemapFooter(nspc::NspcSong& song, int patternId);
    bool applySongInstrumentRemap(nspc::NspcSong& song, int patternId);
    void drawSongInstrumentRemapPopup(nspc::NspcSong& song, int patternId);
    void drawContextMenu(nspc::NspcSong& song, int patternId);
    void applyPlaybackChannelMaskToPlayer(bool forceWhileStopped = false);
    void handleChannelHeaderClick(int channel, bool soloModifier);

    app::AppState& appState_;
    nspc::NspcEditor editor_{};
    std::vector<PatternRow> rows_;
    std::optional<nspc::NspcFlatPattern> flatPattern_;
    nspc::NspcFlattenOptions flattenOptions_{};

    bool rowsTruncated_ = false;
    int ticksPerRow_ = 1;
    int selectedRow_ = -1;
    int selectedChannel_ = -1;
    int selectedItem_ = 0;
    int editStep_ = 1;
    std::string hexInput_;
    std::vector<uint8_t> selectedCells_;
    bool selectionAnchorValid_ = false;
    SelectionCell selectionAnchor_{};
    bool mouseSelecting_ = false;
    bool mouseSelectionAdditive_ = false;
    SelectionCell mouseSelectionAnchor_{};
    std::vector<ClipboardCell> clipboardCells_;
    bool clipboardHasData_ = false;
    bool fxEditorOpenRequested_ = false;
    int fxEditorRequestRow_ = -1;
    int fxEditorRequestChannel_ = -1;
    int fxEditorRequestEffectIndex_ = -1;
    int fxEditorRow_ = -1;
    int fxEditorChannel_ = -1;
    int fxEditorSelectedIndex_ = -1;
    std::vector<EffectChip> fxEditorEffects_;
    int fxEditorAddEffectId_ = 0xE1;
    std::string fxEditorStatus_;

    // Bulk set popups
    bool setInstrumentPopupOpen_ = false;
    bool setVolumePopupOpen_ = false;
    bool patternLengthPopupOpen_ = false;
    int patternLengthInputTicks_ = 0;
    std::string patternLengthStatus_;
    std::array<char, 4> bulkValueInput_{};
    bool songInstrumentRemapPopupOpen_ = false;
    InstrumentRemapScope songInstrumentRemapScope_ = InstrumentRemapScope::Global;
    int songInstrumentRemapChannel_ = 0;
    std::vector<SongInstrumentRemapEntry> songInstrumentRemapEntries_;
    std::string songInstrumentRemapStatus_;

    std::optional<int> activeTrackerPreviewKey_;
    bool trackerPreviewActive_ = false;
    int lastViewedSongIndex_ = -1;
    std::optional<int> lastViewedPatternId_;
    bool pendingScrollToSelection_ = false;
};

}  // namespace ntrak::ui
