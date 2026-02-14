#pragma once

#include "ntrak/app/AppState.hpp"
#include "ntrak/ui/Panel.hpp"

namespace ntrak::ui {

class ProjectPanel : public Panel {
public:
    explicit ProjectPanel(app::AppState& appState);
    ~ProjectPanel() override = default;

    void draw() override;

    const char* title() const override { return "Project"; }

private:
    app::AppState& appState_;
};

}  // namespace ntrak::ui
