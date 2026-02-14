#pragma once

#include "ntrak/app/AppState.hpp"
#include "ntrak/ui/Panel.hpp"

namespace ntrak::ui {

/// Panel displaying SPC/DSP state information
/// Shows voice states, CPU registers, and memory when the shared player is active
class SpcInfoPanel : public Panel {
public:
    explicit SpcInfoPanel(app::AppState& appState);
    ~SpcInfoPanel() override = default;

    void draw() override;

    const char* title() const override { return "SPC Info"; }

private:
    app::AppState& appState_;
};

}  // namespace ntrak::ui
