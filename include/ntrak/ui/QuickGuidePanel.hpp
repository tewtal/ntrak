#pragma once

#include "ntrak/app/AppState.hpp"
#include "ntrak/ui/Panel.hpp"

namespace ntrak::ui {

class QuickGuidePanel final : public Panel {
public:
    explicit QuickGuidePanel(app::AppState& appState);
    void draw() override;

    const char* title() const override { return "Quick Guide"; }

private:
    void drawOverview();
    void drawEffectsReference();
    void drawKeyboardShortcuts();

    app::AppState& appState_;
};

}  // namespace ntrak::ui
