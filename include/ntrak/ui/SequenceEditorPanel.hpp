#pragma once

#include "ntrak/app/AppState.hpp"
#include "ntrak/nspc/NspcData.hpp"
#include "ntrak/ui/Panel.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ntrak::ui {

class SequenceEditorPanel final : public Panel {
public:
    explicit SequenceEditorPanel(app::AppState& appState);
    void draw() override;

    const char* title() const override { return "Sequence Editor"; }

private:
    static constexpr int kChannels = 8;
    enum class InsertOpType : uint8_t {
        PlayPattern,
        JumpTimes,
        AlwaysJump,
        FastForwardOn,
        FastForwardOff,
        EndSequence,
    };
    enum class GridEditField : uint8_t {
        None,
        Pattern,
        Track,
        JumpCount,
        JumpTarget,
        AlwaysOpcode,
        AlwaysTarget,
    };

    nspc::NspcPattern* findPattern(nspc::NspcSong& song, int patternId);
    const nspc::NspcPattern* findPattern(const nspc::NspcSong& song, int patternId) const;
    std::vector<int> collectPatternIds(const nspc::NspcSong& song) const;
    int allocatePatternId(const nspc::NspcSong& song) const;
    int allocateTrackId(const nspc::NspcSong& song) const;
    nspc::NspcPattern& ensurePatternExists(nspc::NspcSong& song, int patternId);
    int duplicatePattern(nspc::NspcSong& song, int sourcePatternId);
    int createNewPattern(nspc::NspcSong& song);
    void insertPlayPatternOp(nspc::NspcSong& song, int patternId);
    std::string describeSequenceOp(const nspc::NspcSequenceOp& op) const;
    int defaultInsertIndex(const nspc::NspcSong& song) const;

    void drawHeader(nspc::NspcSong& song, bool songLocked);
    void drawSequenceTable(nspc::NspcSong& song, bool songLocked);
    void handleInlineHexEditing(nspc::NspcSong& song, bool songLocked);
    bool appendTypedHexNibble();
    void applyHexEdit(nspc::NspcSong& song, int value);
    const char* insertOpTypeLabel(InsertOpType type) const;
    nspc::NspcSequenceOp buildInsertOperation(nspc::NspcSong& song);
    std::optional<nspc::PlayPattern> selectedPlayPattern(const nspc::NspcSong& song) const;

    int& selectedRow() { return appState_.selectedSequenceRow; }

    int& selectedChannel() { return appState_.selectedSequenceChannel; }

    const int& selectedRow() const { return appState_.selectedSequenceRow; }

    const int& selectedChannel() const { return appState_.selectedSequenceChannel; }

    void syncSelectedPatternFromRow(const nspc::NspcSong& song);

    app::AppState& appState_;
    InsertOpType insertType_ = InsertOpType::PlayPattern;
    int insertPatternId_ = -1;
    int insertJumpCount_ = 1;
    int insertJumpTarget_ = 0;
    int insertAlwaysOpcode_ = 0x82;
    GridEditField gridEditField_ = GridEditField::None;
    std::string hexInput_;
    int lastPlaybackScrollRow_ = -1;
};

}  // namespace ntrak::ui
