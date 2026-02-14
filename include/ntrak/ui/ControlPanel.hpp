#pragma once
#include "ntrak/app/AppState.hpp"
#include "ntrak/nspc/NspcData.hpp"
#include "ntrak/nspc/NspcEngine.hpp"
#include "ntrak/ui/Panel.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ntrak::ui {

class ControlPanel final : public Panel {
public:
    explicit ControlPanel(app::AppState& appState);
    void draw() override;

    const char* title() const override { return "Control"; }

    // Playback actions callable from other panels via AppState callbacks
    bool doPlaySong();
    bool doPlayFromPattern();
    void doStop();
    bool doIsPlaying() const;

private:
    bool playSpcImage(const std::vector<uint8_t>& spcImage, uint16_t entryPoint,
                      const nspc::NspcEngineConfig& engineConfig, int songIndex,
                      std::string statusText,
                      std::optional<std::vector<nspc::NspcSequenceOp>> trackingSequence = std::nullopt,
                      int trackingStartRow = 0);

    app::AppState& appState_;
    std::string status_;
    std::vector<std::string> warnings_;
    std::string roundtripStatus_;
    std::vector<std::string> roundtripLines_;
};

}  // namespace ntrak::ui
