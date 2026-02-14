#pragma once

#include "ntrak/app/AppState.hpp"
#include "ntrak/nspc/NspcConverter.hpp"
#include "ntrak/nspc/NspcProject.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ntrak::ui {

/// Modal dialog for porting a song from the current project to a target SPC's engine.
class SongPortDialog {
public:
    explicit SongPortDialog(app::AppState& appState);

    /// Open the dialog (schedules ImGui popup open on next draw).
    void open();

    /// Draw the dialog (call every frame from the parent panel/manager).
    void draw();

    /// Called after a successful port to install the modified target project into the app.
    std::function<void(nspc::NspcProject, std::vector<uint8_t>, std::optional<std::filesystem::path>)>
        onInstallProject;

private:
    void drawSourceSongSection(const nspc::NspcProject& sourceProject);
    void drawTargetEngineSection();
    void drawInstrumentMappingSection(const nspc::NspcProject& sourceProject);
    void drawTargetInstrumentsRemovalSection();
    void drawTargetSongSlotSection();
    void drawStatusSection();

    void rebuildMappings();
    bool loadTargetSpc(const std::filesystem::path& path);
    void executePort();

    app::AppState& appState_;

    bool pendingOpen_ = false;

    // Source selection
    int sourceSongIndex_ = 0;

    // Target state
    std::optional<std::filesystem::path> targetSpcPath_;
    std::optional<nspc::NspcProject> targetProject_;
    std::vector<uint8_t> targetSpcData_;
    std::string targetLoadError_;

    // Instrument mapping (rebuilt when source/target changes)
    std::vector<int> usedInstrumentIds_;
    std::vector<nspc::InstrumentMapping> instrumentMappings_;

    // Target instruments to delete before porting
    std::set<int> instrumentsToDelete_;

    // Target song placement (1 = append new, 0 = overwrite)
    int appendNewSong_ = 1;
    int targetSongOverwriteIndex_ = 0;

    // Status / result
    std::string portError_;
    std::string portStatus_;
};

}  // namespace ntrak::ui
