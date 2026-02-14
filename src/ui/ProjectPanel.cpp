#include "ntrak/ui/ProjectPanel.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <format>

namespace ntrak::ui {

namespace {

constexpr size_t kMaxSongs = 256;
constexpr const char* kRemoveSongPopupId = "Remove Song##ProjectPanel";

const char* contentOriginTag(nspc::NspcContentOrigin origin) {
    return (origin == nspc::NspcContentOrigin::UserProvided) ? "U" : "E";
}

std::optional<int> firstPlayablePatternId(const nspc::NspcSong& song, int& sequenceRowOut) {
    const auto& sequence = song.sequence();
    for (size_t row = 0; row < sequence.size(); ++row) {
        if (const auto* play = std::get_if<nspc::PlayPattern>(&sequence[row])) {
            sequenceRowOut = static_cast<int>(row);
            return play->patternId;
        }
    }
    sequenceRowOut = sequence.empty() ? -1 : 0;
    return std::nullopt;
}

}  // namespace

ProjectPanel::ProjectPanel(app::AppState& appState) : appState_(appState) {}

void ProjectPanel::draw() {
    if (!appState_.project.has_value()) {
        ImGui::TextDisabled("No project loaded");
        ImGui::TextDisabled("Use File > Import to load an SPC file");
        return;
    }

    auto& project = appState_.project.value();
    auto& songs = project.songs();

    if (songs.empty()) {
        appState_.selectedSongIndex = -1;
        appState_.selectedSequenceRow = -1;
        appState_.selectedSequenceChannel = 0;
        appState_.selectedPatternId.reset();
    } else if (appState_.selectedSongIndex < 0 || appState_.selectedSongIndex >= static_cast<int>(songs.size())) {
        appState_.selectedSongIndex =
            std::clamp(appState_.selectedSongIndex, 0, static_cast<int>(songs.size()) - 1);
    }

    const auto& config = project.engineConfig();
    const auto& instruments = project.instruments();
    const auto& samples = project.samples();
    const int userSongCount = static_cast<int>(
        std::count_if(songs.begin(), songs.end(), [](const nspc::NspcSong& song) { return song.isUserProvided(); }));
    const int engineSongCount = static_cast<int>(songs.size()) - userSongCount;

    auto selectSongForEditing = [&](int songIndex) {
        appState_.selectedSongIndex = songIndex;
        appState_.selectedSequenceChannel = 0;
        appState_.selectedPatternId.reset();
        appState_.selectedSequenceRow = -1;

        if (songIndex < 0 || songIndex >= static_cast<int>(songs.size())) {
            return;
        }

        int sequenceRow = -1;
        const auto patternId = firstPlayablePatternId(songs[static_cast<size_t>(songIndex)], sequenceRow);
        appState_.selectedSequenceRow = sequenceRow;
        appState_.selectedPatternId = patternId;
    };

    const bool hasSelectedSong =
        appState_.selectedSongIndex >= 0 && appState_.selectedSongIndex < static_cast<int>(songs.size());
    const bool selectedSongLocked =
        hasSelectedSong && appState_.lockEngineContent &&
        songs[static_cast<size_t>(appState_.selectedSongIndex)].isEngineProvided();
    const bool canAddMoreSongs = songs.size() < kMaxSongs;

    // Project info section
    ImGui::Text("Engine: %s", config.name.c_str());
    ImGui::TextDisabled("Songs: %zu | Instruments: %zu | Samples: %zu", songs.size(), instruments.size(),
                        samples.size());
    ImGui::TextDisabled("Song ownership: %d user | %d engine", userSongCount, engineSongCount);

    ImGui::Separator();

    // Song selector
    ImGui::Text("Song:");
    ImGui::SetNextItemWidth(200);
    std::string selectedSongLabel = "No songs";
    if (hasSelectedSong) {
        const auto& song = songs[static_cast<size_t>(appState_.selectedSongIndex)];
        selectedSongLabel = std::format("[{}] Song {}", contentOriginTag(song.contentOrigin()), song.songId());
    }

    if (ImGui::BeginCombo("##SongSelector", selectedSongLabel.c_str())) {
        for (size_t i = 0; i < songs.size(); i++) {
            const auto& song = songs[i];
            bool isSelected = (static_cast<int>(i) == appState_.selectedSongIndex);

            char label[64];
            snprintf(label, sizeof(label), "[%s] Song %d (%zu patterns)", contentOriginTag(song.contentOrigin()),
                     song.songId(), song.patterns().size());

            if (ImGui::Selectable(label, isSelected)) {
                selectSongForEditing(static_cast<int>(i));
            }

            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // Song management buttons
    ImGui::BeginDisabled(!canAddMoreSongs);
    if (ImGui::Button("Add")) {
        if (const auto addedSongIndex = project.addEmptySong(); addedSongIndex.has_value()) {
            selectSongForEditing(static_cast<int>(*addedSongIndex));
        }
    }
    ImGui::EndDisabled();
    if (!canAddMoreSongs && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Song table is full (max %zu songs)", kMaxSongs);
    }
    ImGui::SameLine();

    ImGui::BeginDisabled(!hasSelectedSong || !canAddMoreSongs);
    if (ImGui::Button("Duplicate")) {
        const auto duplicatedSongIndex = project.duplicateSong(static_cast<size_t>(appState_.selectedSongIndex));
        if (duplicatedSongIndex.has_value()) {
            selectSongForEditing(static_cast<int>(*duplicatedSongIndex));
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(!hasSelectedSong || selectedSongLocked);
    if (ImGui::Button("Remove")) {
        ImGui::OpenPopup(kRemoveSongPopupId);
    }
    ImGui::EndDisabled();

    if (hasSelectedSong) {
        ImGui::SameLine();
        const auto& selectedSong = songs[static_cast<size_t>(appState_.selectedSongIndex)];
        ImGui::BeginDisabled(selectedSong.isUserProvided());
        if (ImGui::Button("Mark User")) {
            (void)project.setSongContentOrigin(static_cast<size_t>(appState_.selectedSongIndex),
                                               nspc::NspcContentOrigin::UserProvided);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(selectedSong.isEngineProvided());
        if (ImGui::Button("Mark Engine")) {
            (void)project.setSongContentOrigin(static_cast<size_t>(appState_.selectedSongIndex),
                                               nspc::NspcContentOrigin::EngineProvided);
        }
        ImGui::EndDisabled();
    }

    if (ImGui::BeginPopupModal(kRemoveSongPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (hasSelectedSong) {
            ImGui::Text("Remove Song %02X?", songs[static_cast<size_t>(appState_.selectedSongIndex)].songId());
            ImGui::TextDisabled("This removes the song from the project list.");
        } else {
            ImGui::TextDisabled("No song selected.");
        }

        if (hasSelectedSong && songs.size() == 1) {
            ImGui::Separator();
            ImGui::TextDisabled("Removing this song leaves the project with no songs.");
        }

        ImGui::Separator();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!hasSelectedSong || selectedSongLocked);
        if (ImGui::Button("Remove")) {
            const size_t removedIndex = static_cast<size_t>(appState_.selectedSongIndex);
            if (project.removeSong(removedIndex)) {
                if (songs.empty()) {
                    selectSongForEditing(-1);
                } else {
                    const int nextIndex =
                        std::min<int>(static_cast<int>(removedIndex), static_cast<int>(songs.size()) - 1);
                    selectSongForEditing(nextIndex);
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    if (selectedSongLocked) {
        ImGui::TextDisabled("Selected song is engine-owned and locked from edits.");
    }

    // Show current song info
    const bool selectedSongValidNow =
        appState_.selectedSongIndex >= 0 && appState_.selectedSongIndex < static_cast<int>(songs.size());
    if (selectedSongValidNow) {
        auto& song = songs[static_cast<size_t>(appState_.selectedSongIndex)];
        auto toInputBuffer = [](const std::string& value) {
            std::array<char, 129> buffer{};
            const size_t copyLen = std::min(value.size(), buffer.size() - 1);
            std::copy_n(value.data(), copyLen, buffer.data());
            return buffer;
        };
        auto songNameBuffer = toInputBuffer(song.songName());
        auto authorBuffer = toInputBuffer(song.author());
        ImGui::BeginDisabled(appState_.lockEngineContent && song.isEngineProvided());
        if (ImGui::InputText("Song Name", songNameBuffer.data(), songNameBuffer.size())) {
            song.setSongName(songNameBuffer.data());
        }
        if (ImGui::InputText("Author", authorBuffer.data(), authorBuffer.size())) {
            song.setAuthor(authorBuffer.data());
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Tracks: %zu | Subroutines: %zu | Source: %s", song.tracks().size(), song.subroutines().size(),
                            song.isUserProvided() ? "User" : "Engine");
    }
}

}  // namespace ntrak::ui
