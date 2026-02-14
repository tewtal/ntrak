#pragma once

#include "ntrak/app/AppState.hpp"
#include "ntrak/ui/Panel.hpp"

namespace ntrak::ui {

class AramUsagePanel final : public Panel {
public:
    explicit AramUsagePanel(app::AppState& appState);
    void draw() override;

    const char* title() const override { return "ARAM Usage"; }

private:
    app::AppState& appState_;
};

}  // namespace ntrak::ui
