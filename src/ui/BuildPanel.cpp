#include "ntrak/ui/BuildPanel.hpp"

#include "ntrak/nspc/NspcCompile.hpp"

#include <imgui.h>

#include <format>
#include <optional>
#include <utility>

namespace ntrak::ui {
namespace {

void applyRelaxedOptimizerTuning(nspc::NspcOptimizerOptions& options) {
    options.maxOptimizeIterations = 64;
    options.topCandidatesFromSam = 1024;
    options.maxCandidateBytes = 1536;
    options.singleIterationCallPenaltyBytes = 16;
    options.allowSingleIterationCalls = false;
}

void applyBalancedOptimizerTuning(nspc::NspcOptimizerOptions& options) {
    options.maxOptimizeIterations = 128;
    options.topCandidatesFromSam = 2048;
    options.maxCandidateBytes = 2048;
    options.singleIterationCallPenaltyBytes = 16;
    options.allowSingleIterationCalls = true;
}

void applyAggressiveOptimizerTuning(nspc::NspcOptimizerOptions& options) {
    options.maxOptimizeIterations = 512;
    options.topCandidatesFromSam = 16384;
    options.maxCandidateBytes = 16384;
    options.singleIterationCallPenaltyBytes = 8;
    options.allowSingleIterationCalls = true;
}

std::optional<size_t> selectedSongIndex(const app::AppState& appState) {
    if (!appState.project.has_value()) {
        return std::nullopt;
    }
    if (appState.selectedSongIndex < 0) {
        return std::nullopt;
    }
    const auto& songs = appState.project->songs();
    if (static_cast<size_t>(appState.selectedSongIndex) >= songs.size()) {
        return std::nullopt;
    }
    return static_cast<size_t>(appState.selectedSongIndex);
}

void flattenSelectedSong(app::AppState& appState, size_t songIndex) {
    auto& project = *appState.project;
    auto& song = project.songs()[songIndex];
    song.flattenSubroutines();
    song.setContentOrigin(nspc::NspcContentOrigin::UserProvided);
    project.refreshAramUsage();
    appState.commandHistory.clear();
}

void optimizeSelectedSong(app::AppState& appState, size_t songIndex) {
    auto& project = *appState.project;
    auto& song = project.songs()[songIndex];
    nspc::optimizeSongSubroutines(song, appState.optimizerOptions);
    song.setContentOrigin(nspc::NspcContentOrigin::UserProvided);
    project.refreshAramUsage();
    appState.commandHistory.clear();
}

[[nodiscard]] nspc::NspcBuildOptions buildOptionsFromAppState(const app::AppState& appState) {
    return nspc::NspcBuildOptions{
        .optimizeSubroutines = appState.optimizeSubroutinesOnBuild,
        .optimizerOptions = appState.optimizerOptions,
        .applyOptimizedSongToProject = appState.optimizeSubroutinesOnBuild && !appState.flattenSubroutinesOnLoad,
        .includeEngineExtensions = true,
        .compactAramLayout = appState.compactAramLayoutOnBuild,
    };
}

[[nodiscard]] std::pair<std::string, bool> rebuildUserContentForAramStats(app::AppState& appState) {
    auto& project = *appState.project;
    auto buildResult = nspc::buildUserContentUpload(project, buildOptionsFromAppState(appState));
    if (buildResult.has_value()) {
        return {std::format("Rebuilt user content ({} upload chunk(s))", buildResult->chunks.size()), false};
    }

    if (buildResult.error() == "Project has no user-provided content to export") {
        project.refreshAramUsage();
        return {"No user content to build; ARAM usage refreshed from current project state", false};
    }

    return {std::format("Build failed while refreshing ARAM stats: {}", buildResult.error()), true};
}

}  // namespace

BuildPanel::BuildPanel(app::AppState& appState) : appState_(appState) {}

void BuildPanel::draw() {
    ImGui::Checkbox("Flatten subroutines on load", &appState_.flattenSubroutinesOnLoad);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Applies on the next SPC import or project open.");
    }

    ImGui::Checkbox("Optimize subroutines on build", &appState_.optimizeSubroutinesOnBuild);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("When disabled, song build/upload skips subroutine extraction.");
    }

    ImGui::Checkbox("Compact ARAM layout on build", &appState_.compactAramLayoutOnBuild);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Pack relocatable song data into tighter ARAM ranges to reduce holes and NSPC upload segments.");
    }

    ImGui::Checkbox("Lock engine content edits", &appState_.lockEngineContent);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("When enabled, engine-marked songs/assets are read-only and cannot be edited or deleted.");
    }

    if (ImGui::CollapsingHeader("Optimizer Tuning")) {
        ImGui::TextDisabled("Used by both manual Optimize and optional optimize-on-build.");
        if (ImGui::Button("Relaxed")) {
            applyRelaxedOptimizerTuning(appState_.optimizerOptions);
        }
        ImGui::SameLine();
        if (ImGui::Button("Balanced")) {
            applyBalancedOptimizerTuning(appState_.optimizerOptions);
        }
        ImGui::SameLine();
        if (ImGui::Button("Aggressive")) {
            applyAggressiveOptimizerTuning(appState_.optimizerOptions);
        }

        ImGui::Checkbox("Allow single-run calls (count=1)", &appState_.optimizerOptions.allowSingleIterationCalls);
        ImGui::SliderInt("Max optimize passes", &appState_.optimizerOptions.maxOptimizeIterations, 1, 512);
        ImGui::SliderInt("Top SAM candidates", &appState_.optimizerOptions.topCandidatesFromSam, 64, 8192);
        int maxCandidateBytes = static_cast<int>(appState_.optimizerOptions.maxCandidateBytes);
        if (ImGui::SliderInt("Max candidate bytes", &maxCandidateBytes, 32, 8192)) {
            appState_.optimizerOptions.maxCandidateBytes = static_cast<uint32_t>(maxCandidateBytes);
        }
        ImGui::SliderInt("Single-run penalty", &appState_.optimizerOptions.singleIterationCallPenaltyBytes, 0, 32);
        ImGui::TextDisabled("Higher penalty reduces one-shot call extraction and runtime dispatch overhead.");
    }

    if (appState_.project.has_value()) {
        const auto songIndex = selectedSongIndex(appState_);
        auto& project = *appState_.project;
        const bool selectedSongLocked =
            songIndex.has_value() && appState_.lockEngineContent && project.songs()[*songIndex].isEngineProvided();

        ImGui::Separator();
        ImGui::TextUnformatted("One-time Song Actions");
        ImGui::BeginDisabled(!songIndex.has_value() || selectedSongLocked);
        if (ImGui::Button("Flatten") && songIndex.has_value()) {
            flattenSelectedSong(appState_, *songIndex);
            auto [status, isError] = rebuildUserContentForAramStats(appState_);
            actionStatus_ = std::move(status);
            actionStatusIsError_ = isError;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Immediately flatten the selected song's subroutine calls into tracks and rebuild ARAM stats.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Optimize") && songIndex.has_value()) {
            optimizeSelectedSong(appState_, *songIndex);
            auto [status, isError] = rebuildUserContentForAramStats(appState_);
            actionStatus_ = std::move(status);
            actionStatusIsError_ = isError;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Immediately run subroutine optimization on the selected song and rebuild ARAM stats.");
        }
        ImGui::EndDisabled();
        if (songIndex.has_value()) {
            const int songId = project.songs()[*songIndex].songId();
            ImGui::TextDisabled("Applies to selected song %02X", songId);
            if (selectedSongLocked) {
                ImGui::TextDisabled("Selected song is engine-owned and locked from edits.");
            }
        } else {
            ImGui::TextDisabled("No song selected");
        }
        if (!actionStatus_.empty()) {
            if (actionStatusIsError_) {
                ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", actionStatus_.c_str());
            } else {
                ImGui::TextDisabled("%s", actionStatus_.c_str());
            }
        }

        const auto& extensions = project.engineConfig().extensions;
        if (!extensions.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Engine Extensions");
            for (size_t i = 0; i < extensions.size(); ++i) {
                const auto& extension = extensions[i];
                bool enabled = extension.enabled;
                const std::string label = std::format("{}##engine_ext_{}", extension.name, i);
                if (ImGui::Checkbox(label.c_str(), &enabled)) {
                    (void)project.setEngineExtensionEnabled(extension.name, enabled);
                }
                if (ImGui::IsItemHovered()) {
                    if (!extension.description.empty()) {
                        ImGui::SetTooltip("%s", extension.description.c_str());
                    } else {
                        ImGui::SetTooltip("Engine extension");
                    }
                }
            }
        }
    }
}

}  // namespace ntrak::ui
