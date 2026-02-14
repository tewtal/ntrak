#pragma once

#include "ntrak/app/AppState.hpp"
#include "ntrak/ui/Panel.hpp"

#include <string>

namespace ntrak::ui {

class BuildPanel final : public Panel {
public:
    explicit BuildPanel(app::AppState& appState);
    void draw() override;

    const char* title() const override { return "Build"; }

private:
    app::AppState& appState_;
    std::string actionStatus_;
    bool actionStatusIsError_ = false;
};

}  // namespace ntrak::ui
