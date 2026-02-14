#include "ntrak/ui/SpcInfoPanel.hpp"

#include "ntrak/app/App.hpp"

#include <imgui.h>

#include <cstdint>

namespace ntrak::ui {

SpcInfoPanel::SpcInfoPanel(app::AppState& appState) : appState_(appState) {
    setVisible(false);
}

void SpcInfoPanel::draw() {
    if (!appState_.spcPlayer) {
        ImGui::TextDisabled("Audio not available");
        return;
    }

    auto& player = *appState_.spcPlayer;

    if (!player.isLoaded()) {
        ImGui::TextDisabled("No SPC loaded");
        return;
    }

    // Status
    if (player.isPlaying()) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Playing");
    } else {
        ImGui::TextDisabled("Stopped");
    }

    ImGui::Separator();

    // DSP Voice States
    ImGui::Text("DSP Voice States:");
    emulation::SpcDsp& dsp = player.spcDsp();

    if (ImGui::BeginTable("VoiceTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Voice", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Vol L", ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableSetupColumn("Vol R", ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableSetupColumn("Pitch", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Src", ImGuiTableColumnFlags_WidthFixed, 35);
        ImGui::TableSetupColumn("Env", ImGuiTableColumnFlags_WidthFixed, 35);
        ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int v = 0; v < emulation::SpcDsp::VoiceCount; ++v) {
            auto state = dsp.voiceState(v);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("%d", v);

            ImGui::TableNextColumn();
            ImGui::Text("%d", state.volume[0]);

            ImGui::TableNextColumn();
            ImGui::Text("%d", state.volume[1]);

            ImGui::TableNextColumn();
            ImGui::Text("%04X", state.pitch);

            ImGui::TableNextColumn();
            ImGui::Text("%02X", state.sourceNumber);

            ImGui::TableNextColumn();
            ImGui::Text("%d", state.envelopeLevel);

            ImGui::TableNextColumn();
            std::string flags;
            if (state.keyOn) {
                flags += "KON ";
            }
            if (state.keyOff) {
                flags += "KOFF ";
            }
            if (state.echoEnabled) {
                flags += "ECH ";
            }
            if (state.noiseEnabled) {
                flags += "NSE ";
            }
            if (state.pitchModEnabled) {
                flags += "PMOD";
            }
            ImGui::TextDisabled("%s", flags.c_str());
        }
        ImGui::EndTable();
    }

    // SPC CPU Status
    ImGui::Separator();
    ImGui::Text("SPC CPU:");
    ImGui::Text("A=%02X X=%02X Y=%02X SP=%02X PS=%02X PC=%04X", dsp.a(), dsp.x(), dsp.y(), dsp.sp(), dsp.ps(),
                dsp.pc());
    ImGui::Text("Cycles: %lu", dsp.cycleCount());

    // Zero Page (collapsible)
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Zero Page (0x00-0xFF)")) {
        auto zeroPage = dsp.aram().bytes(0x00, 0x100);

        ImGui::PushFont(ntrak::app::App::fonts().vt323, 16.0f);
        for (size_t i = 0; i < 16; ++i) {
            ImGui::Text("%02X:", static_cast<uint8_t>(i * 16));
            ImGui::SameLine();
            for (size_t j = 0; j < 16; ++j) {
                ImGui::Text("%02X", zeroPage[i * 16 + j]);
                if (j < 15) {
                    ImGui::SameLine();
                }
            }
        }
        ImGui::PopFont();
    }
}

}  // namespace ntrak::ui
