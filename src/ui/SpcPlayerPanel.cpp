#include "ntrak/ui/SpcPlayerPanel.hpp"

#include "ntrak/app/App.hpp"

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <nfd.hpp>

namespace fs = std::filesystem;

namespace ntrak::ui {

SpcPlayerPanel::SpcPlayerPanel(app::AppState& appState) : appState_(appState) {
    setVisible(false);
    try {
        currentDirectory_ = fs::current_path().string();
    } catch (...) {
        currentDirectory_ = ".";
    }
}

SpcPlayerPanel::~SpcPlayerPanel() = default;

void SpcPlayerPanel::loadSpcFile(const std::string& path) {
    if (!appState_.spcPlayer) {
        errorMessage_ = "Audio not initialized";
        return;
    }

    appState_.spcPlayer->stop();

    if (!appState_.spcPlayer->loadFile(path)) {
        errorMessage_ = "Failed to load SPC file: " + path;
        return;
    }

    loadedFilePath_ = path;
    errorMessage_.clear();
}

void SpcPlayerPanel::draw() {
    if (!appState_.spcPlayer) {
        ImGui::TextDisabled("Audio not available");
        return;
    }

    auto& player = *appState_.spcPlayer;

    ImGui::Separator();
    ImGui::Text("SPC Player Test");
    ImGui::Separator();

    // File browser button
    if (ImGui::Button("Browse...")) {
        showFileDialog_ = true;
    }

    ImGui::SameLine();

    if (player.isLoaded()) {
        ImGui::Text("Loaded: %s", fs::path(loadedFilePath_).filename().string().c_str());
    } else {
        ImGui::TextDisabled("No file loaded");
    }

    // Playback controls
    ImGui::BeginDisabled(!player.isLoaded());

    if (player.isPlaying()) {
        if (ImGui::Button("Stop")) {
            player.stop();
        }
    } else {
        if (ImGui::Button("Play")) {
            player.play();
        }
    }

    ImGui::EndDisabled();

    ImGui::SameLine();
    if (player.isPlaying()) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Playing");
    } else {
        ImGui::TextDisabled("Stopped");
    }

    // File info
    if (player.isLoaded()) {
        const auto* info = player.fileInfo();
        if (info && !info->songTitle.empty()) {
            ImGui::Text("Title: %s", info->songTitle.c_str());
        }
        if (info && !info->gameTitle.empty()) {
            ImGui::Text("Game: %s", info->gameTitle.c_str());
        }
        if (info && !info->artist.empty()) {
            ImGui::Text("Artist: %s", info->artist.c_str());
        }
    }

    // Error message
    if (!errorMessage_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", errorMessage_.c_str());
    }

    // File dialog popup
    if (showFileDialog_) {
        NFD::UniquePath outPath;
        nfdfilteritem_t filterItem[1] = {{"SPC Files", "spc"}};
        nfdresult_t result = NFD::OpenDialog(outPath, filterItem, 1);
        if (result == NFD_OKAY) {
            loadSpcFile(std::string(outPath.get()));
        }
        showFileDialog_ = false;
    }
}

}  // namespace ntrak::ui
