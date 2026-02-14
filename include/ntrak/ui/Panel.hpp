#pragma once

#include <string>

namespace ntrak::ui {

/// Base class for all dockable UI panels
class Panel {
public:
    virtual ~Panel() = default;

    /// Draw the panel content (called inside a window)
    virtual void draw() = 0;

    /// Get the window title for this panel
    virtual const char* title() const = 0;

    /// Check if this panel window is visible
    bool isVisible() const { return visible_; }

    /// Set panel window visibility
    void setVisible(bool visible) { visible_ = visible; }

    /// Toggle visibility
    void toggleVisible() { visible_ = !visible_; }

    /// Get pointer to visibility flag (for ImGui::MenuItem)
    bool* visiblePtr() { return &visible_; }

protected:
    bool visible_ = true;
};

}  // namespace ntrak::ui
