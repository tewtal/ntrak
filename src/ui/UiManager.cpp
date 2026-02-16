#include "ntrak/ui/UiManager.hpp"

#include "ntrak/common/UserGuide.hpp"
#include "ntrak/nspc/ItImport.hpp"
#include "ntrak/nspc/NspcCompile.hpp"
#include "ntrak/nspc/NspcParser.hpp"
#include "ntrak/nspc/NspcProjectFile.hpp"
#include "ntrak/nspc/NspcSpcExport.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <nfd.hpp>
#include <span>
#include <string>
#include <vector>

namespace ntrak::ui {
namespace {
constexpr const char* kPanelVisibilitySettingsType = "ntrak_panel_visibility";
constexpr const char* kPanelVisibilitySettingsName = "State";

std::string parseErrorToString(nspc::NspcParseError error) {
    switch (error) {
    case nspc::NspcParseError::InvalidConfig:
        return "Invalid engine configuration";
    case nspc::NspcParseError::InvalidHeader:
        return "File is not a valid SPC";
    case nspc::NspcParseError::UnsupportedVersion:
        return "SPC engine is not recognized by current engine configs";
    case nspc::NspcParseError::UnexpectedEndOfData:
        return "SPC file is truncated";
    case nspc::NspcParseError::InvalidEventData:
        return "SPC contains invalid event data";
    default:
        return "Unknown SPC parse error";
    }
}

std::expected<std::vector<uint8_t>, std::string> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(std::format("Failed to open '{}'", path.string()));
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

std::expected<void, std::string> writeBinaryFile(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected(std::format("Failed to open '{}' for writing", path.string()));
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out.good()) {
        return std::unexpected(std::format("Failed while writing '{}'", path.string()));
    }
    return {};
}

uint16_t readU16(std::span<const uint8_t> bytes, size_t offset) {
    return static_cast<uint16_t>(bytes[offset]) | (static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

std::expected<nspc::NspcUploadList, std::string> decodeNspcUpload(std::span<const uint8_t> nspcBytes) {
    nspc::NspcUploadList upload;
    size_t cursor = 0;
    bool foundTerminator = false;

    while (true) {
        if (cursor + 4 > nspcBytes.size()) {
            return std::unexpected("NSPC data is truncated while reading upload header");
        }

        const uint16_t length = readU16(nspcBytes, cursor);
        const uint16_t address = readU16(nspcBytes, cursor + 2);
        cursor += 4;

        if (length == 0) {
            if (cursor == nspcBytes.size()) {
                foundTerminator = true;
                break;
            } else {
                return std::unexpected("NSPC terminator chunk is not at the end of data");
            }
        }

        if (cursor + length > nspcBytes.size()) {
            return std::unexpected(
                std::format("NSPC upload chunk at ${:04X} is truncated ({} bytes expected)", address, length));
        }

        nspc::NspcUploadChunk chunk{};
        chunk.address = address;
        chunk.label = std::format("NSPC ${:04X}", address);
        chunk.bytes.assign(nspcBytes.data() + cursor, nspcBytes.data() + cursor + length);
        upload.chunks.push_back(std::move(chunk));
        cursor += length;
    }

    if (!foundTerminator) {
        return std::unexpected("NSPC data is missing a terminator chunk");
    }
    if (cursor != nspcBytes.size()) {
        return std::unexpected("NSPC data has trailing bytes after terminator");
    }
    if (upload.chunks.empty()) {
        return std::unexpected("NSPC contains no upload data");
    }
    return upload;
}

std::filesystem::path backupPathFor(const std::filesystem::path& projectPath) {
    return std::filesystem::path(projectPath.string() + ".bak");
}

void resetPlaybackTracking(app::PlaybackTrackingState& playback) {
    playback.hooksInstalled.store(false, std::memory_order_relaxed);
    playback.awaitingFirstPatternTrigger.store(false, std::memory_order_relaxed);
    playback.pendingStopAtEnd.store(false, std::memory_order_relaxed);
    playback.eventSerial.store(0, std::memory_order_relaxed);
    playback.engineTickEvents.store(0, std::memory_order_relaxed);
    playback.sequenceRow.store(-1, std::memory_order_relaxed);
    playback.patternId.store(-1, std::memory_order_relaxed);
    playback.patternTick.store(-1, std::memory_order_relaxed);
}

void selectFirstPlayableRow(app::AppState& appState, int songIndex) {
    appState.selectedSongIndex = songIndex;
    appState.selectedPatternId.reset();
    appState.selectedSequenceRow = -1;
    appState.selectedSequenceChannel = 0;

    if (!appState.project.has_value()) {
        return;
    }

    if (songIndex < 0 || songIndex >= static_cast<int>(appState.project->songs().size())) {
        return;
    }

    const auto& sequence = appState.project->songs()[static_cast<size_t>(songIndex)].sequence();
    for (size_t row = 0; row < sequence.size(); ++row) {
        if (const auto* play = std::get_if<nspc::PlayPattern>(&sequence[row])) {
            appState.selectedSequenceRow = static_cast<int>(row);
            appState.selectedPatternId = play->patternId;
            break;
        }
    }
    if (!appState.selectedPatternId.has_value() && !sequence.empty()) {
        appState.selectedSequenceRow = 0;
    }
}

std::string instrumentLabel(const nspc::NspcInstrument& inst) {
    if (inst.name.empty()) {
        return std::format("${:02X}", inst.id);
    }
    return std::format("${:02X} {}", inst.id, inst.name);
}

std::string sampleLabel(const nspc::BrrSample& sample) {
    if (sample.name.empty()) {
        return std::format("${:02X} ({} B)", sample.id, sample.data.size());
    }
    return std::format("${:02X} {} ({} B)", sample.id, sample.name, sample.data.size());
}

double sampleResampleRatioFor(const nspc::ItImportOptions& options, int sampleIndex) {
    for (const auto& entry : options.sampleResampleOptions) {
        if (entry.sampleIndex == sampleIndex) {
            return entry.resampleRatio;
        }
    }
    return 1.0;
}

void setSampleResampleRatio(nspc::ItImportOptions& options, int sampleIndex, double ratio) {
    for (auto& entry : options.sampleResampleOptions) {
        if (entry.sampleIndex == sampleIndex) {
            entry.resampleRatio = ratio;
            return;
        }
    }
    options.sampleResampleOptions.push_back(nspc::ItSampleResampleOption{
        .sampleIndex = sampleIndex,
        .resampleRatio = ratio,
    });
}

void initializeProjectSelection(app::AppState& appState) {
    appState.selectedSequenceChannel = 0;
    appState.selectedPatternId.reset();
    appState.selectedSequenceRow = -1;
    appState.selectedSongIndex = -1;
    appState.selectedInstrumentId = -1;

    if (!appState.project.has_value()) {
        return;
    }

    if (!appState.project->songs().empty()) {
        appState.selectedSongIndex = 0;
        const auto& firstSong = appState.project->songs().front();
        const auto& sequence = firstSong.sequence();
        for (size_t row = 0; row < sequence.size(); ++row) {
            if (const auto* play = std::get_if<nspc::PlayPattern>(&sequence[row])) {
                appState.selectedSequenceRow = static_cast<int>(row);
                appState.selectedPatternId = play->patternId;
                break;
            }
        }
        if (!appState.selectedPatternId.has_value() && !sequence.empty()) {
            appState.selectedSequenceRow = 0;
        }
    }

    if (!appState.project->instruments().empty()) {
        appState.selectedInstrumentId = appState.project->instruments().front().id;
    }
}

void maybeFlattenSubroutinesOnLoad(app::AppState& appState, nspc::NspcProject& project) {
    if (!appState.flattenSubroutinesOnLoad) {
        return;
    }
    for (auto& song : project.songs()) {
        song.flattenSubroutines();
    }
    project.refreshAramUsage();
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

void installLoadedProject(app::AppState& appState, nspc::NspcProject project) {
    appState.project = std::move(project);
    initializeProjectSelection(appState);
    resetPlaybackTracking(appState.playback);

    // Clear undo/redo history on project change
    appState.commandHistory.clear();

    const auto& spcData = appState.project->sourceSpcData();
    if (appState.spcPlayer && !spcData.empty()) {
        appState.spcPlayer->stop();
        (void)appState.spcPlayer->loadFromMemory(spcData.data(), static_cast<uint32_t>(spcData.size()));
    }
}

}  // namespace

UiManager::UiManager(app::AppState& appState) : appState_(appState), songPortDialog_(appState) {
    registerPanelVisibilitySettingsHandler();

    // Force-load settings now so panel visibility is available before panels are added.
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context != nullptr && !context->SettingsLoaded) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.IniFilename != nullptr && io.IniFilename[0] != '\0') {
            ImGui::LoadIniSettingsFromDisk(io.IniFilename);
        }
    }

    songPortDialog_.onInstallProject = [this](nspc::NspcProject project) {
        installProject(std::move(project));
    };

    // Wire undo/redo callbacks
    appState_.undo = [this]() {
        if (!appState_.project.has_value()) {
            return;
        }
        auto& song = appState_.project->songs()[appState_.selectedSongIndex];
        (void)appState_.commandHistory.undo(song);
    };

    appState_.redo = [this]() {
        if (!appState_.project.has_value()) {
            return;
        }
        auto& song = appState_.project->songs()[appState_.selectedSongIndex];
        (void)appState_.commandHistory.redo(song);
    };
}

void UiManager::addPanel(std::unique_ptr<Panel> panel) {
    if (panel) {
        if (const auto it = persistedPanelVisibility_.find(panel->title()); it != persistedPanelVisibility_.end()) {
            panel->setVisible(it->second);
        }
        panels_.push_back(std::move(panel));
    }
}

Panel* UiManager::panel(const std::string& title) {
    for (auto& panel : panels_) {
        if (panel && panel->title() == title) {
            return panel.get();
        }
    }
    return nullptr;
}

void UiManager::draw() {
    handleGlobalShortcuts();
    drawTitleBar();
    drawDockspace();
    drawPanelWindows();
    songPortDialog_.draw();
    drawItImportDialog();
    drawItImportWarningsModal();
}

void UiManager::setFileStatus(std::string message, bool isError) {
    fileStatus_ = std::move(message);
    fileStatusIsError_ = isError;
}

void UiManager::registerPanelVisibilitySettingsHandler() {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr) {
        return;
    }

    const ImGuiID typeHash = ImHashStr(kPanelVisibilitySettingsType);
    for (const ImGuiSettingsHandler& existing : context->SettingsHandlers) {
        if (existing.TypeHash == typeHash) {
            return;
        }
    }

    ImGuiSettingsHandler handler{};
    handler.TypeName = kPanelVisibilitySettingsType;
    handler.TypeHash = typeHash;
    handler.UserData = this;
    handler.ClearAllFn = &UiManager::panelVisibilitySettingsClearAll;
    handler.ReadOpenFn = &UiManager::panelVisibilitySettingsReadOpen;
    handler.ReadLineFn = &UiManager::panelVisibilitySettingsReadLine;
    handler.WriteAllFn = &UiManager::panelVisibilitySettingsWriteAll;
    context->SettingsHandlers.push_back(handler);
}

void UiManager::parsePanelVisibilitySettingsLine(const char* line) {
    if (line == nullptr || line[0] == '\0') {
        return;
    }

    const char* separator = std::strchr(line, '=');
    if (separator == nullptr || separator == line || separator[1] == '\0') {
        return;
    }

    std::string title(line, separator);
    std::string value(separator + 1);
    while (!value.empty() && (value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.erase(value.begin());
    }

    if (value == "1" || value == "true") {
        persistedPanelVisibility_[std::move(title)] = true;
    } else if (value == "0" || value == "false") {
        persistedPanelVisibility_[std::move(title)] = false;
    }
}

void UiManager::writePanelVisibilitySettings(ImGuiTextBuffer& outBuffer) const {
    outBuffer.appendf("[%s][%s]\n", kPanelVisibilitySettingsType, kPanelVisibilitySettingsName);
    for (const auto& panel : panels_) {
        if (!panel) {
            continue;
        }
        outBuffer.appendf("%s=%d\n", panel->title(), panel->isVisible() ? 1 : 0);
    }
    outBuffer.append("\n");
}

void UiManager::markPanelVisibilityDirty() {
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::MarkIniSettingsDirty();
    }
}

void UiManager::panelVisibilitySettingsClearAll(ImGuiContext*, ImGuiSettingsHandler* handler) {
    if (handler == nullptr) {
        return;
    }

    if (auto* uiManager = static_cast<UiManager*>(handler->UserData); uiManager != nullptr) {
        uiManager->persistedPanelVisibility_.clear();
    }
}

void* UiManager::panelVisibilitySettingsReadOpen(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name) {
    if (handler == nullptr || name == nullptr || std::strcmp(name, kPanelVisibilitySettingsName) != 0) {
        return nullptr;
    }
    return handler->UserData;
}

void UiManager::panelVisibilitySettingsReadLine(ImGuiContext*, ImGuiSettingsHandler* handler, void* entry,
                                                const char* line) {
    if (line == nullptr) {
        return;
    }

    UiManager* uiManager = static_cast<UiManager*>(entry);
    if (uiManager == nullptr && handler != nullptr) {
        uiManager = static_cast<UiManager*>(handler->UserData);
    }
    if (uiManager != nullptr) {
        uiManager->parsePanelVisibilitySettingsLine(line);
    }
}

void UiManager::panelVisibilitySettingsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler,
                                                ImGuiTextBuffer* outBuffer) {
    if (handler == nullptr || outBuffer == nullptr) {
        return;
    }

    if (auto* uiManager = static_cast<UiManager*>(handler->UserData); uiManager != nullptr) {
        uiManager->writePanelVisibilitySettings(*outBuffer);
    }
}

bool UiManager::importSpcFromDialog() {
    NFD::UniquePath outPath;
    nfdfilteritem_t filters[1] = {{"SPC Files", "spc"}};
    const nfdresult_t result = NFD::OpenDialog(outPath, filters, 1);
    if (result == NFD_CANCEL) {
        return false;
    }
    if (result == NFD_ERROR) {
        setFileStatus(std::format("File dialog error: {}", NFD::GetError() ? NFD::GetError() : "unknown"), true);
        return false;
    }

    const std::filesystem::path spcPath = outPath.get();
    auto fileData = readBinaryFile(spcPath);
    if (!fileData.has_value()) {
        setFileStatus(fileData.error(), true);
        return false;
    }

    auto parseResult = nspc::NspcParser::load(*fileData);
    if (!parseResult.has_value()) {
        setFileStatus(std::format("Failed to import SPC: {}", parseErrorToString(parseResult.error())), true);
        return false;
    }
    maybeFlattenSubroutinesOnLoad(appState_, *parseResult);

    parseResult->setSourceSpcPath(spcPath);
    installLoadedProject(appState_, std::move(*parseResult));
    currentProjectPath_.reset();
    setFileStatus(std::format("Imported SPC '{}'", spcPath.filename().string()), false);
    return true;
}

bool UiManager::importNspcFromDialog() {
    NFD::UniquePath nspcPathRaw;
    nfdfilteritem_t nspcFilters[1] = {{"NSPC Files", "nspc"}};
    const nfdresult_t nspcDialogResult = NFD::OpenDialog(nspcPathRaw, nspcFilters, 1);
    if (nspcDialogResult == NFD_CANCEL) {
        return false;
    }
    if (nspcDialogResult == NFD_ERROR) {
        setFileStatus(std::format("File dialog error: {}", NFD::GetError() ? NFD::GetError() : "unknown"), true);
        return false;
    }

    NFD::UniquePath baseSpcPathRaw;
    nfdfilteritem_t spcFilters[1] = {{"SPC Files", "spc"}};
    const nfdresult_t spcDialogResult = NFD::OpenDialog(baseSpcPathRaw, spcFilters, 1);
    if (spcDialogResult == NFD_CANCEL) {
        return false;
    }
    if (spcDialogResult == NFD_ERROR) {
        setFileStatus(std::format("File dialog error: {}", NFD::GetError() ? NFD::GetError() : "unknown"), true);
        return false;
    }

    const std::filesystem::path nspcPath = nspcPathRaw.get();
    const std::filesystem::path spcPath = baseSpcPathRaw.get();

    auto nspcData = readBinaryFile(nspcPath);
    if (!nspcData.has_value()) {
        setFileStatus(nspcData.error(), true);
        return false;
    }

    auto baseSpcData = readBinaryFile(spcPath);
    if (!baseSpcData.has_value()) {
        setFileStatus(baseSpcData.error(), true);
        return false;
    }

    auto upload = decodeNspcUpload(*nspcData);
    if (!upload.has_value()) {
        setFileStatus(std::format("Failed to parse NSPC: {}", upload.error()), true);
        return false;
    }

    auto patchedSpcData = nspc::applyUploadToSpcImage(*upload, *baseSpcData);
    if (!patchedSpcData.has_value()) {
        setFileStatus(std::format("Failed to apply NSPC over base SPC: {}", patchedSpcData.error()), true);
        return false;
    }

    auto parseResult = nspc::NspcParser::load(*patchedSpcData);
    if (!parseResult.has_value()) {
        setFileStatus(std::format("Failed to import patched SPC: {}", parseErrorToString(parseResult.error())), true);
        return false;
    }
    maybeFlattenSubroutinesOnLoad(appState_, *parseResult);

    installLoadedProject(appState_, std::move(*parseResult));
    currentProjectPath_.reset();
    setFileStatus(std::format("Imported NSPC '{}' over base SPC '{}'", nspcPath.filename().string(),
                              spcPath.filename().string()),
                  false);
    return true;
}

void UiManager::openItImportDialog() {
    if (!appState_.project.has_value()) {
        setFileStatus("No project loaded", true);
        return;
    }
    const int targetSongIndex = appState_.selectedSongIndex;
    if (targetSongIndex < 0 || targetSongIndex >= static_cast<int>(appState_.project->songs().size())) {
        setFileStatus("Select a valid song slot before importing IT", true);
        return;
    }
    if (appState_.lockEngineContent &&
        appState_.project->songs()[static_cast<size_t>(targetSongIndex)].isEngineProvided()) {
        setFileStatus("IT import blocked: selected song is engine-owned and locked. Use 'Mark User' first.", true);
        return;
    }

    pendingOpenItImportDialog_ = true;
    itImportPath_.reset();
    itImportOptions_ = nspc::ItImportOptions{};
    itImportPreview_.reset();
    itImportDialogError_.clear();
}

bool UiManager::rebuildItImportPreview() {
    if (!appState_.project.has_value() || !itImportPath_.has_value()) {
        return false;
    }
    const int targetSongIndex = appState_.selectedSongIndex;
    if (targetSongIndex < 0 || targetSongIndex >= static_cast<int>(appState_.project->songs().size())) {
        itImportDialogError_ = "Selected song slot is invalid";
        itImportPreview_.reset();
        return false;
    }
    if (appState_.lockEngineContent &&
        appState_.project->songs()[static_cast<size_t>(targetSongIndex)].isEngineProvided()) {
        itImportDialogError_ = "IT import blocked: selected song is engine-owned and locked. Use 'Mark User' first.";
        itImportPreview_.reset();
        return false;
    }

    auto preview = nspc::analyzeItFileForSongSlot(*appState_.project, *itImportPath_, targetSongIndex,
                                                  itImportOptions_);
    if (!preview.has_value()) {
        itImportDialogError_ = preview.error();
        itImportPreview_.reset();
        return false;
    }
    itImportDialogError_.clear();
    itImportPreview_ = std::move(*preview);
    return true;
}

bool UiManager::executeItImportFromWorkbench() {
    if (!appState_.project.has_value()) {
        itImportDialogError_ = "No project loaded";
        return false;
    }
    if (!itImportPath_.has_value()) {
        itImportDialogError_ = "Select an IT file first";
        return false;
    }

    const int targetSongIndex = appState_.selectedSongIndex;
    if (targetSongIndex < 0 || targetSongIndex >= static_cast<int>(appState_.project->songs().size())) {
        itImportDialogError_ = "Selected song slot is invalid";
        return false;
    }
    if (appState_.lockEngineContent &&
        appState_.project->songs()[static_cast<size_t>(targetSongIndex)].isEngineProvided()) {
        itImportDialogError_ = "IT import blocked: selected song is engine-owned and locked. Use 'Mark User' first.";
        setFileStatus(itImportDialogError_, true);
        return false;
    }

    auto importResult = nspc::importItFileIntoSongSlot(*appState_.project, *itImportPath_, targetSongIndex,
                                                       itImportOptions_);
    if (!importResult.has_value()) {
        itImportDialogError_ = importResult.error();
        setFileStatus(std::format("IT import failed: {}", importResult.error()), true);
        return false;
    }

    auto [project, report] = std::move(*importResult);
    appState_.project = std::move(project);
    appState_.commandHistory.clear();
    resetPlaybackTracking(appState_.playback);
    selectFirstPlayableRow(appState_, targetSongIndex);

    if (appState_.project.has_value()) {
        appState_.project->syncAramToSpcData();
    }
    const auto& spcData = appState_.project->sourceSpcData();
    if (appState_.spcPlayer && !spcData.empty()) {
        appState_.spcPlayer->stop();
        (void)appState_.spcPlayer->loadFromMemory(spcData.data(), static_cast<uint32_t>(spcData.size()));
    }

    itImportWarnings_ = std::move(report.warnings);
    showItImportWarnings_ = !itImportWarnings_.empty();

    std::string extensionSummary;
    if (!report.enabledExtensions.empty()) {
        extensionSummary = std::format(", enabled extensions: {}", report.enabledExtensions.front());
        for (size_t i = 1; i < report.enabledExtensions.size(); ++i) {
            extensionSummary += std::format(", {}", report.enabledExtensions[i]);
        }
    }

    setFileStatus(std::format("Imported IT '{}' into song slot {:02d} ({} patterns, {} tracks, {} instruments, {} "
                              "samples, {} warnings{})",
                              itImportPath_->filename().string(), report.targetSongIndex, report.importedPatternCount,
                              report.importedTrackCount, report.importedInstrumentCount, report.importedSampleCount,
                              itImportWarnings_.size(), extensionSummary),
                  false);

    itImportDialogError_.clear();
    return true;
}

void UiManager::drawItImportDialog() {
    if (pendingOpenItImportDialog_) {
        ImGui::OpenPopup("IT Import Workbench");
        pendingOpenItImportDialog_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(980, 780), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("IT Import Workbench", nullptr, ImGuiWindowFlags_NoResize)) {
        return;
    }

    if (!appState_.project.has_value()) {
        ImGui::TextDisabled("No project loaded.");
        if (ImGui::Button("Close")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    const int targetSongIndex = appState_.selectedSongIndex;
    const bool validSongSelection = targetSongIndex >= 0 &&
                                    targetSongIndex < static_cast<int>(appState_.project->songs().size());
    if (!validSongSelection) {
        ImGui::TextDisabled("Select a valid song slot in the Project panel first.");
        if (ImGui::Button("Close")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }
    const bool targetSongLocked = appState_.lockEngineContent &&
                                  appState_.project->songs()[static_cast<size_t>(targetSongIndex)].isEngineProvided();
    if (targetSongLocked) {
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1.0f),
                           "Selected song is engine-owned and locked. Use Project -> Mark User to allow IT import.");
    }

    ImGui::SeparatorText("Source IT File");
    if (ImGui::Button("Select IT File...")) {
        NFD::UniquePath outPath;
        nfdfilteritem_t filters[1] = {{"Impulse Tracker Module", "it"}};
        const nfdresult_t result = NFD::OpenDialog(outPath, filters, 1);
        if (result == NFD_OKAY) {
            itImportPath_ = std::filesystem::path(outPath.get());
            (void)rebuildItImportPreview();
        } else if (result == NFD_ERROR) {
            itImportDialogError_ = std::format("File dialog error: {}", NFD::GetError() ? NFD::GetError() : "unknown");
        }
    }
    ImGui::SameLine();
    if (itImportPath_.has_value()) {
        ImGui::TextUnformatted(itImportPath_->filename().string().c_str());
    } else {
        ImGui::TextDisabled("(none selected)");
    }

    if (itImportPreview_.has_value()) {
        const auto& preview = *itImportPreview_;

        ImGui::SeparatorText("Song Overview");
        ImGui::Text("Module: %s", preview.moduleName.empty() ? "(unnamed)" : preview.moduleName.c_str());
        ImGui::Text("Target song slot: %02d", targetSongIndex);
        ImGui::Text("Patterns: %d", preview.importedPatternCount);
        ImGui::SameLine();
        ImGui::Text("Tracks: %d", preview.importedTrackCount);
        ImGui::SameLine();
        ImGui::Text("Instruments: %d", preview.importedInstrumentCount);
        ImGui::SameLine();
        ImGui::Text("Samples: %d", preview.importedSampleCount);
        ImGui::Text("Estimated sample bytes: %u", preview.estimatedRequiredSampleBytes);
        ImGui::Text("Free ARAM: %u", preview.currentFreeAramBytes);
        ImGui::SameLine();
        ImGui::Text("After deletion: %u", preview.freeAramAfterDeletionBytes);
        if (preview.estimatedRequiredSampleBytes > preview.freeAramAfterDeletionBytes) {
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f),
                               "Sample data estimate exceeds free ARAM; reduce size or delete more assets.");
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.4f, 1.0f), "Sample size estimate fits current free ARAM.");
        }

        ImGui::SeparatorText("Resampling");
        float globalRatio = static_cast<float>(itImportOptions_.globalResampleRatio);
        if (ImGui::SliderFloat("Global Ratio", &globalRatio, 0.10f, 2.00f, "%.2f")) {
            itImportOptions_.globalResampleRatio = std::clamp(static_cast<double>(globalRatio), 0.10, 2.00);
            (void)rebuildItImportPreview();
        }
        bool highQualityResampling = itImportOptions_.highQualityResampling;
        if (ImGui::Checkbox("High Quality Resampling", &highQualityResampling)) {
            itImportOptions_.highQualityResampling = highQualityResampling;
            (void)rebuildItImportPreview();
        }
        bool enhanceTreble = itImportOptions_.enhanceTrebleOnEncode;
        if (ImGui::Checkbox("Treble Compensation (Gaussian)", &enhanceTreble)) {
            itImportOptions_.enhanceTrebleOnEncode = enhanceTreble;
            (void)rebuildItImportPreview();
        }

        if (ImGui::BeginTable("##it-import-samples", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, 230))) {
            ImGui::TableSetupColumn("Sample", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Loop", ImGuiTableColumnFlags_WidthFixed, 55);
            ImGui::TableSetupColumn("PCM In", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("Ratio", ImGuiTableColumnFlags_WidthFixed, 130);
            ImGui::TableSetupColumn("PCM Out", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("BRR Bytes", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableHeadersRow();

            for (const auto& sample : preview.samples) {
                ImGui::TableNextRow();
                ImGui::PushID(sample.sampleIndex);

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%02X %s", sample.sampleIndex, sample.name.empty() ? "(unnamed)" : sample.name.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(sample.looped ? "Yes" : "No");

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", sample.sourcePcmSampleCount);

                ImGui::TableSetColumnIndex(3);
                float ratio = static_cast<float>(sampleResampleRatioFor(itImportOptions_, sample.sampleIndex));
                if (ImGui::DragFloat("##ratio", &ratio, 0.01f, 0.10f, 2.00f, "%.2f")) {
                    setSampleResampleRatio(itImportOptions_, sample.sampleIndex,
                                           std::clamp(static_cast<double>(ratio), 0.10, 2.00));
                    (void)rebuildItImportPreview();
                }

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%u", sample.estimatedPcmSampleCount);

                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%u", sample.estimatedBrrBytes);

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        auto toggleId = [](std::vector<int>& ids, int id, bool enabled) {
            const auto it = std::find(ids.begin(), ids.end(), id);
            if (enabled) {
                if (it == ids.end()) {
                    ids.push_back(id);
                }
            } else if (it != ids.end()) {
                ids.erase(it);
            }
        };
        auto hasId = [](const std::vector<int>& ids, int id) {
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        };

        ImGui::SeparatorText("Delete Target Instruments");
        if (ImGui::BeginTable("##it-import-delete-inst", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, 140))) {
            ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 35);
            ImGui::TableSetupColumn("Instrument", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Sample", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const auto& instrument : appState_.project->instruments()) {
                ImGui::TableNextRow();
                ImGui::PushID(100000 + instrument.id);
                ImGui::TableSetColumnIndex(0);
                bool checked = hasId(itImportOptions_.instrumentsToDelete, instrument.id);
                if (ImGui::Checkbox("##delinst", &checked)) {
                    toggleId(itImportOptions_.instrumentsToDelete, instrument.id, checked);
                    (void)rebuildItImportPreview();
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(instrumentLabel(instrument).c_str());
                ImGui::TableSetColumnIndex(2);
                const auto sampleIt = std::find_if(
                    appState_.project->samples().begin(), appState_.project->samples().end(),
                    [&](const nspc::BrrSample& value) { return value.id == (instrument.sampleIndex & 0x7F); });
                if (sampleIt != appState_.project->samples().end()) {
                    ImGui::TextUnformatted(sampleLabel(*sampleIt).c_str());
                } else {
                    ImGui::TextDisabled("(none)");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Delete Target Samples");
        if (ImGui::BeginTable("##it-import-delete-sample", 2,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, 140))) {
            ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 35);
            ImGui::TableSetupColumn("Sample", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const auto& sample : appState_.project->samples()) {
                ImGui::TableNextRow();
                ImGui::PushID(200000 + sample.id);
                ImGui::TableSetColumnIndex(0);
                bool checked = hasId(itImportOptions_.samplesToDelete, sample.id);
                if (ImGui::Checkbox("##delsample", &checked)) {
                    toggleId(itImportOptions_.samplesToDelete, sample.id, checked);
                    (void)rebuildItImportPreview();
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(sampleLabel(sample).c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Import Warnings Preview");
        ImGui::BeginChild("##it-import-preview-warnings", ImVec2(0, 120), true);
        if (preview.warnings.empty()) {
            ImGui::TextDisabled("No warnings from current preview.");
        } else {
            for (const auto& warning : preview.warnings) {
                ImGui::BulletText("%s", warning.c_str());
            }
        }
        ImGui::EndChild();
    }

    if (!itImportDialogError_.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", itImportDialogError_.c_str());
    }

    ImGui::Spacing();
    const bool canImport = itImportPath_.has_value() && itImportPreview_.has_value() && !targetSongLocked;
    ImGui::BeginDisabled(!canImport);
    if (ImGui::Button("Import IT", ImVec2(120, 0))) {
        if (executeItImportFromWorkbench()) {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

bool UiManager::openProjectFromDialog() {
    NFD::UniquePath projectPath;
    nfdfilteritem_t projectFilters[1] = {{"ntrak Project", "ntrakproj"}};
    const nfdresult_t projectDialogResult = NFD::OpenDialog(projectPath, projectFilters, 1);
    if (projectDialogResult == NFD_CANCEL) {
        return false;
    }
    if (projectDialogResult == NFD_ERROR) {
        setFileStatus(std::format("File dialog error: {}", NFD::GetError() ? NFD::GetError() : "unknown"), true);
        return false;
    }

    const std::filesystem::path overlayPath = projectPath.get();
    auto overlayData = nspc::loadProjectIrFile(overlayPath);
    if (!overlayData.has_value()) {
        setFileStatus(std::format("Failed to load project file: {}", overlayData.error()), true);
        return false;
    }

    std::optional<std::filesystem::path> resolvedBaseSpcPath;
    bool promptedForBaseSpc = false;
    if (overlayData->baseSpcPath.has_value()) {
        std::filesystem::path candidate = *overlayData->baseSpcPath;
        if (candidate.is_relative()) {
            candidate = overlayPath.parent_path() / candidate;
        }
        std::error_code existsError;
        if (std::filesystem::exists(candidate, existsError) && !existsError) {
            resolvedBaseSpcPath = std::move(candidate);
        }
    }

    if (!resolvedBaseSpcPath.has_value()) {
        NFD::UniquePath baseSpcPath;
        nfdfilteritem_t spcFilters[1] = {{"SPC Files", "spc"}};
        const nfdresult_t spcDialogResult = NFD::OpenDialog(baseSpcPath, spcFilters, 1);
        if (spcDialogResult == NFD_CANCEL) {
            return false;
        }
        if (spcDialogResult == NFD_ERROR) {
            setFileStatus(std::format("File dialog error: {}", NFD::GetError() ? NFD::GetError() : "unknown"), true);
            return false;
        }
        resolvedBaseSpcPath = std::filesystem::path(baseSpcPath.get());
        promptedForBaseSpc = true;
    }

    const std::filesystem::path spcPath = *resolvedBaseSpcPath;
    auto baseSpcData = readBinaryFile(spcPath);
    if (!baseSpcData.has_value()) {
        setFileStatus(baseSpcData.error(), true);
        return false;
    }

    auto parsedBaseProject = nspc::NspcParser::load(*baseSpcData);
    if (!parsedBaseProject.has_value()) {
        setFileStatus(std::format("Failed to parse base SPC: {}", parseErrorToString(parsedBaseProject.error())), true);
        return false;
    }

    auto applyOverlayResult = nspc::applyProjectIrOverlay(*parsedBaseProject, *overlayData);
    if (!applyOverlayResult.has_value()) {
        setFileStatus(std::format("Failed to apply project overlay: {}", applyOverlayResult.error()), true);
        return false;
    }
    maybeFlattenSubroutinesOnLoad(appState_, *parsedBaseProject);

    // Bake user content (instruments, samples, BRR data) into the SPC bytes so that
    // playback — which uses sourceSpcData as the base and only patches song data on top — finds
    // the correct instrument table entries and sample directory entries.
    if (auto userContent = nspc::buildUserContentUpload(*parsedBaseProject); userContent.has_value()) {
        auto& spcData = parsedBaseProject->sourceSpcData();
        if (auto updatedSpc = nspc::applyUploadToSpcImage(*userContent, spcData); updatedSpc.has_value()) {
            spcData = std::move(*updatedSpc);
        }
    }
    parsedBaseProject->setSourceSpcPath(spcPath);
    installLoadedProject(appState_, std::move(*parsedBaseProject));
    currentProjectPath_ = overlayPath;

    bool rememberedBaseSpc = false;
    if (promptedForBaseSpc && !overlayData->baseSpcPath.has_value()) {
        auto rememberResult = nspc::saveProjectIrFile(*appState_.project, overlayPath,
                                                      appState_.project->sourceSpcPath());
        rememberedBaseSpc = rememberResult.has_value();
    }

    if (rememberedBaseSpc) {
        setFileStatus(std::format("Opened project '{}' with base SPC '{}' (remembered)",
                                  overlayPath.filename().string(), spcPath.filename().string()),
                      false);
    } else {
        setFileStatus(std::format("Opened project '{}' with base SPC '{}'", overlayPath.filename().string(),
                                  spcPath.filename().string()),
                      false);
    }
    return true;
}

bool UiManager::saveProjectToPath(const std::filesystem::path& path) {
    if (!appState_.project.has_value()) {
        setFileStatus("No project loaded", true);
        return false;
    }

    bool hadExistingFile = false;
    {
        std::error_code ec;
        hadExistingFile = std::filesystem::exists(path, ec);
        if (ec) {
            setFileStatus(std::format("Failed to check project path '{}': {}", path.string(), ec.message()), true);
            return false;
        }
    }

    std::filesystem::path backupPath;
    if (hadExistingFile) {
        backupPath = backupPathFor(path);
        std::error_code copyError;
        std::filesystem::copy_file(path, backupPath, std::filesystem::copy_options::overwrite_existing, copyError);
        if (copyError) {
            setFileStatus(
                std::format("Backup failed for '{}': {}", backupPath.filename().string(), copyError.message()), true);
            return false;
        }
    }

    auto saveResult = nspc::saveProjectIrFile(*appState_.project, path, appState_.project->sourceSpcPath());
    if (!saveResult.has_value()) {
        setFileStatus(std::format("Save failed: {}", saveResult.error()), true);
        return false;
    }

    currentProjectPath_ = path;
    if (hadExistingFile) {
        setFileStatus(std::format("Saved '{}' (backup '{}')", path.filename().string(), backupPath.filename().string()),
                      false);
    } else {
        setFileStatus(std::format("Saved '{}'", path.filename().string()), false);
    }
    return true;
}

bool UiManager::saveProjectAsFromDialog() {
    if (!appState_.project.has_value()) {
        setFileStatus("No project loaded", true);
        return false;
    }

    std::string defaultName = "project.ntrakproj";
    if (currentProjectPath_.has_value() && !currentProjectPath_->filename().empty()) {
        defaultName = currentProjectPath_->filename().string();
    }

    NFD::UniquePath outPath;
    nfdfilteritem_t filters[1] = {{"ntrak Project", "ntrakproj"}};
    const nfdresult_t result = NFD::SaveDialog(outPath, filters, 1, nullptr, defaultName.c_str());
    if (result == NFD_CANCEL) {
        return false;
    }
    if (result == NFD_ERROR) {
        setFileStatus(std::format("File dialog error: {}", NFD::GetError() ? NFD::GetError() : "unknown"), true);
        return false;
    }
    return saveProjectToPath(std::filesystem::path(outPath.get()));
}

bool UiManager::saveProjectQuick() {
    if (!appState_.project.has_value()) {
        setFileStatus("No project loaded", true);
        return false;
    }
    if (!currentProjectPath_.has_value()) {
        return saveProjectAsFromDialog();
    }
    return saveProjectToPath(*currentProjectPath_);
}

bool UiManager::exportUserDataFromDialog() {
    if (!appState_.project.has_value()) {
        setFileStatus("No project loaded", true);
        return false;
    }

    std::string defaultName = "project.nspc";
    if (currentProjectPath_.has_value()) {
        const std::string stem = currentProjectPath_->stem().string();
        if (!stem.empty()) {
            defaultName = stem + ".nspc";
        }
    }

    NFD::UniquePath outPath;
    nfdfilteritem_t filters[1] = {{"NSPC Export", "nspc"}};
    const nfdresult_t result = NFD::SaveDialog(outPath, filters, 1, nullptr, defaultName.c_str());
    if (result == NFD_CANCEL) {
        return false;
    }
    if (result == NFD_ERROR) {
        setFileStatus(std::format("File dialog error: {}", NFD::GetError() ? NFD::GetError() : "unknown"), true);
        return false;
    }

    const std::filesystem::path path = outPath.get();
    auto exportData = nspc::buildUserContentNspcExport(*appState_.project, buildOptionsFromAppState(appState_));
    if (!exportData.has_value()) {
        setFileStatus(std::format("Export failed: {}", exportData.error()), true);
        return false;
    }

    auto writeResult = writeBinaryFile(path, *exportData);
    if (!writeResult.has_value()) {
        setFileStatus(writeResult.error(), true);
        return false;
    }

    setFileStatus(std::format("Exported '{}'", path.filename().string()), false);
    return true;
}

bool UiManager::exportSpcFromDialog() {
    if (!appState_.project.has_value()) {
        setFileStatus("No project loaded", true);
        return false;
    }

    if (appState_.project->sourceSpcData().empty()) {
        setFileStatus("No base SPC file available for export", true);
        return false;
    }

    const int songIndex = appState_.selectedSongIndex;
    if (songIndex < 0 || songIndex >= static_cast<int>(appState_.project->songs().size())) {
        setFileStatus("Invalid song index", true);
        return false;
    }

    std::string defaultName = std::format("song_{:02d}.spc", songIndex);
    if (currentProjectPath_.has_value()) {
        const std::string stem = currentProjectPath_->stem().string();
        if (!stem.empty()) {
            defaultName = std::format("{}_{:02d}.spc", stem, songIndex);
        }
    }

    NFD::UniquePath outPath;
    nfdfilteritem_t filters[1] = {{"SPC File", "spc"}};
    const nfdresult_t result = NFD::SaveDialog(outPath, filters, 1, nullptr, defaultName.c_str());
    if (result == NFD_CANCEL) {
        return false;
    }
    if (result == NFD_ERROR) {
        setFileStatus(std::format("File dialog error: {}", NFD::GetError() ? NFD::GetError() : "unknown"), true);
        return false;
    }

    const std::filesystem::path path = outPath.get();
    auto spcData = nspc::buildAutoPlaySpc(*appState_.project, appState_.project->sourceSpcData(), songIndex,
                                          std::nullopt, buildOptionsFromAppState(appState_));
    if (!spcData.has_value()) {
        setFileStatus(std::format("SPC export failed: {}", spcData.error()), true);
        return false;
    }

    auto writeResult = writeBinaryFile(path, *spcData);
    if (!writeResult.has_value()) {
        setFileStatus(writeResult.error(), true);
        return false;
    }

    setFileStatus(std::format("Exported SPC '{}'", path.filename().string()), false);
    return true;
}

void UiManager::installProject(nspc::NspcProject project) {
    maybeFlattenSubroutinesOnLoad(appState_, project);
    installLoadedProject(appState_, std::move(project));
    currentProjectPath_.reset();
}

void UiManager::handleGlobalShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsKeyPressed(ImGuiKey_S) && io.KeyCtrl && !io.WantTextInput) {
        if (io.KeyShift) {
            (void)saveProjectAsFromDialog();
        } else {
            (void)saveProjectQuick();
        }
        return;
    }

    // Undo/Redo shortcuts — handled globally
    if (ImGui::IsKeyPressed(ImGuiKey_Z) && io.KeyCtrl && !io.KeyShift && !io.WantTextInput) {
        if (appState_.undo) {
            appState_.undo();
        }
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Y) && io.KeyCtrl && !io.WantTextInput) {
        if (appState_.redo) {
            appState_.redo();
        }
        return;
    }

    // Playback shortcuts — handled globally so they work regardless of focused panel
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
        if (appState_.playSong) {
            (void)appState_.playSong();
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_F6)) {
        if (appState_.playFromPattern) {
            (void)appState_.playFromPattern();
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_F8)) {
        if (appState_.stopPlayback) {
            appState_.stopPlayback();
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_Space) && !io.WantTextInput) {
        if (appState_.isPlaying && appState_.isPlaying()) {
            if (appState_.stopPlayback) {
                appState_.stopPlayback();
            }
        } else {
            if (appState_.playSong) {
                (void)appState_.playSong();
            }
        }
    }
}

void UiManager::drawTitleBar() {
    // BeginMainMenuBar creates a full-width window at the top of the viewport
    // and handles all the styling/sizing flags automatically.
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            const bool hasProject = appState_.project.has_value();
            const bool hasValidSelectedSong = hasProject && appState_.selectedSongIndex >= 0 &&
                                              appState_.selectedSongIndex <
                                                  static_cast<int>(appState_.project->songs().size());

            ImGui::MenuItem("New", "Ctrl+N");

            if (ImGui::MenuItem("Import SPC...")) {
                (void)importSpcFromDialog();
            }

            if (ImGui::MenuItem("Import NSPC...")) {
                (void)importNspcFromDialog();
            }

            ImGui::BeginDisabled(!hasValidSelectedSong);
            if (ImGui::MenuItem("Import IT...")) {
                openItImportDialog();
            }
            ImGui::EndDisabled();

            if (ImGui::MenuItem("Open Project...")) {
                (void)openProjectFromDialog();
            }

            ImGui::BeginDisabled(!hasProject);

            if (ImGui::MenuItem("Save Project", "Ctrl+S")) {
                (void)saveProjectQuick();
            }

            if (ImGui::MenuItem("Save Project As...", "Ctrl+Shift+S")) {
                (void)saveProjectAsFromDialog();
            }

            if (ImGui::MenuItem("Export User Data (.nspc)")) {
                (void)exportUserDataFromDialog();
            }

            if (ImGui::MenuItem("Export SPC...")) {
                (void)exportSpcFromDialog();
            }

            if (ImGui::MenuItem("Port Song to Engine...")) {
                songPortDialog_.open();
            }

            ImGui::EndDisabled();

            if (!fileStatus_.empty()) {
                ImGui::Separator();
                if (fileStatusIsError_) {
                    ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", fileStatus_.c_str());
                } else {
                    ImGui::TextDisabled("%s", fileStatus_.c_str());
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                if (exitCallback_) {
                    exitCallback_();
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            const bool canUndo = appState_.commandHistory.canUndo();
            const bool canRedo = appState_.commandHistory.canRedo();
            const bool hasProject = appState_.project.has_value();

            ImGui::BeginDisabled(!canUndo);
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
                if (appState_.undo) {
                    appState_.undo();
                }
            }
            if (canUndo) {
                const auto desc = appState_.commandHistory.undoDescription();
                if (desc.has_value()) {
                    ImGui::SetItemTooltip("Undo: %s", desc->c_str());
                }
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!canRedo);
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
                if (appState_.redo) {
                    appState_.redo();
                }
            }
            if (canRedo) {
                const auto desc = appState_.commandHistory.redoDescription();
                if (desc.has_value()) {
                    ImGui::SetItemTooltip("Redo: %s", desc->c_str());
                }
            }
            ImGui::EndDisabled();

            ImGui::Separator();

            ImGui::BeginDisabled(!hasProject);
            if (ImGui::MenuItem("Cut", "Ctrl+X")) {
                if (appState_.cut) {
                    appState_.cut();
                }
            }
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                if (appState_.copy) {
                    appState_.copy();
                }
            }
            if (ImGui::MenuItem("Paste", "Ctrl+V")) {
                if (appState_.paste) {
                    appState_.paste();
                }
            }
            ImGui::EndDisabled();

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            // function definition assumed to exist
            drawWindowMenu();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Transport")) {
            if (ImGui::MenuItem("Play Song", "F5")) {
                if (appState_.playSong) {
                    (void)appState_.playSong();
                }
            }
            if (ImGui::MenuItem("Play From Pattern", "F6")) {
                if (appState_.playFromPattern) {
                    (void)appState_.playFromPattern();
                }
            }
            if (ImGui::MenuItem("Stop", "F8")) {
                if (appState_.stopPlayback) {
                    appState_.stopPlayback();
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Show Quick Guide")) {
                if (Panel* quickGuide = panel("Quick Guide")) {
                    if (!quickGuide->isVisible()) {
                        quickGuide->setVisible(true);
                        markPanelVisibilityDirty();
                    }
                }
            }
            if (ImGui::MenuItem("Open User Guide")) {
                (void)openUserGuide();
            }
            ImGui::EndMenu();
        }

        if (menuCallback_) {
            menuCallback_();
        }

        // // Status area logic works exactly the same
        // if (statusCallback_) {
        //     float statusWidth = 200.0f;
        //     // GetWindowWidth() works correctly inside MainMenuBar
        //     ImGui::SetCursorPosX(ImGui::GetWindowWidth() - statusWidth);
        //     statusCallback_();
        // }

        ImGui::EndMainMenuBar();
    }
}

void UiManager::drawWindowMenu() {
    for (auto& panel : panels_) {
        if (panel) {
            const bool wasVisible = panel->isVisible();
            bool visible = wasVisible;
            (void)ImGui::MenuItem(panel->title(), nullptr, &visible);
            panel->setVisible(visible);
            if (visible != wasVisible) {
                markPanelVisibilityDirty();
            }
        }
    }
}

void UiManager::drawItImportWarningsModal() {
    if (showItImportWarnings_) {
        ImGui::OpenPopup("IT Import Warnings");
        showItImportWarnings_ = false;
    }

    if (!ImGui::BeginPopupModal("IT Import Warnings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextWrapped("Import completed with %zu warning(s).", itImportWarnings_.size());
    ImGui::Spacing();

    ImGui::BeginChild("##it-import-warning-list", ImVec2(780, 320), true);
    for (const auto& warning : itImportWarnings_) {
        ImGui::BulletText("%s", warning.c_str());
    }
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

bool UiManager::openUserGuide() {
    const auto openedGuide = ntrak::common::openUserGuideInDefaultApp();
    if (!openedGuide.has_value()) {
        setFileStatus(openedGuide.error(), true);
        return false;
    }
    setFileStatus("Opened user guide in browser", false);
    return true;
}

void UiManager::drawDockspace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImVec2 dockspacePos = viewport->WorkPos;
    ImVec2 dockspaceSize = viewport->WorkSize;

    ImGui::SetNextWindowPos(dockspacePos);
    ImGui::SetNextWindowSize(dockspaceSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags dockspaceWindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                            ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("##DockspaceWindow", nullptr, dockspaceWindowFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("MainDockspace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    // Set up default layout only if no saved layout exists
    // Check if the dockspace node exists and has been configured (from imgui.ini)
    ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
    const bool hasExistingLayout = node != nullptr && node->IsSplitNode();

    if (firstFrame_ && !hasExistingLayout) {
        firstFrame_ = false;

        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, dockspaceSize);

        // Create default layout: top strip (sequence/control/assets/project), main area, right sidebar
        ImGuiID dockMain = dockspaceId;
        ImGuiID dockTop = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Up, 0.2f, nullptr, &dockMain);
        ImGuiID dockTopControlArea = ImGui::DockBuilderSplitNode(dockTop, ImGuiDir_Right, 0.66f, nullptr, &dockTop);
        ImGuiID dockTopProject = ImGui::DockBuilderSplitNode(dockTopControlArea, ImGuiDir_Right, 0.34f, nullptr,
                                                             &dockTopControlArea);
        ImGuiID dockTopAssets = ImGui::DockBuilderSplitNode(dockTopControlArea, ImGuiDir_Right, 0.5f, nullptr,
                                                            &dockTopControlArea);
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.2f, nullptr, &dockMain);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.2f, nullptr, &dockMain);

        // Dock panels to their default positions
        for (auto& panel : panels_) {
            if (panel) {
                const char* title = panel->title();
                if (std::string(title).find("Sequence") != std::string::npos) {
                    ImGui::DockBuilderDockWindow(title, dockTop);
                } else if (std::string(title).find("Control") != std::string::npos) {
                    ImGui::DockBuilderDockWindow(title, dockTopControlArea);
                } else if (std::string(title).find("Build") != std::string::npos) {
                    ImGui::DockBuilderDockWindow(title, dockTopControlArea);
                } else if (std::string(title).find("Assets") != std::string::npos) {
                    ImGui::DockBuilderDockWindow(title, dockTopAssets);
                } else if (std::string(title).find("Project") != std::string::npos) {
                    ImGui::DockBuilderDockWindow(title, dockTopProject);
                } else if (std::string(title).find("Player") != std::string::npos ||
                           std::string(title).find("Info") != std::string::npos ||
                           std::string(title).find("ARAM") != std::string::npos) {
                    ImGui::DockBuilderDockWindow(title, dockRight);
                } else if (std::string(title).find("Pattern") != std::string::npos) {
                    ImGui::DockBuilderDockWindow(title, dockMain);
                } else {
                    ImGui::DockBuilderDockWindow(title, dockMain);
                }
            }
        }

        ImGui::DockBuilderFinish(dockspaceId);
    }

    ImGui::End();
}

void UiManager::drawPanelWindows() {
    for (auto& panel : panels_) {
        if (panel && panel->isVisible()) {
            const bool wasVisible = panel->isVisible();
            bool visible = wasVisible;

            if (ImGui::Begin(panel->title(), &visible)) {
                panel->draw();
            }
            ImGui::End();

            panel->setVisible(visible);
            if (visible != wasVisible) {
                markPanelVisibilityDirty();
            }
        }
    }
}

}  // namespace ntrak::ui
