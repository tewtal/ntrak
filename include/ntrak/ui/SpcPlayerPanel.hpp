#pragma once

#include "ntrak/app/AppState.hpp"
#include "ntrak/ui/Panel.hpp"

#include <string>
#include <vector>

namespace ntrak::ui {

/// Utility panel for loading and playing arbitrary SPC files
/// Uses the shared SpcPlayer from AppState (temporarily hijacks it)
class SpcPlayerPanel : public Panel {
public:
    explicit SpcPlayerPanel(app::AppState& appState);
    ~SpcPlayerPanel() override;

    void draw() override;

    const char* title() const override { return "SPC Player"; }

private:
    void loadSpcFile(const std::string& path);

    app::AppState& appState_;

    std::string loadedFilePath_;
    std::string errorMessage_;

    std::string currentDirectory_;
    std::vector<std::string> directoryEntries_;
    int selectedEntry_ = -1;
    bool showFileDialog_ = false;
};

}  // namespace ntrak::ui
