#include "ntrak/ui/SongPortDialog.hpp"

#include "ntrak/nspc/NspcParser.hpp"

#include <imgui.h>
#include <nfd.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <unordered_map>

namespace ntrak::ui {
namespace {

std::string songLabel(const nspc::NspcSong& song, int index) {
    return std::format("List {:02X} | Ptr {:02X} | {}", index, song.songId(),
                       song.isUserProvided() ? "User" : "Engine");
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

void previewInstrumentInProject(app::AppState& appState, const nspc::NspcProject& project,
                                const nspc::NspcInstrument& inst);

template <typename Item>
using IdLookup = std::unordered_map<int, const Item*>;

template <typename Item>
IdLookup<Item> buildIdLookup(const std::vector<Item>& items) {
    IdLookup<Item> lookup;
    lookup.reserve(items.size());
    for (const auto& item : items) {
        lookup.emplace(item.id, &item);
    }
    return lookup;
}

template <typename Item>
const Item* lookupById(const IdLookup<Item>& lookup, int id) {
    const auto it = lookup.find(id);
    if (it == lookup.end()) {
        return nullptr;
    }
    return it->second;
}

std::string sourceMappingLabel(const nspc::InstrumentMapping& mapping, const IdLookup<nspc::NspcInstrument>& sourceInstById,
                               const IdLookup<nspc::BrrSample>& sourceSampleById) {
    std::string label = std::format("${:02X}", mapping.sourceInstrumentId);
    const auto* sourceInst = lookupById(sourceInstById, mapping.sourceInstrumentId);
    if (!sourceInst) {
        return label;
    }
    if (!sourceInst->name.empty()) {
        label += " " + sourceInst->name;
    }
    const auto* sourceSample = lookupById(sourceSampleById, sourceInst->sampleIndex);
    if (sourceSample && !sourceSample->name.empty()) {
        label += std::format(" ({})", sourceSample->name);
    }
    return label;
}

std::string targetInstrumentPreview(int targetInstrumentId, const std::vector<nspc::NspcInstrument>& targetInstruments,
                                    const IdLookup<nspc::NspcInstrument>& targetInstById) {
    const auto* targetInst = lookupById(targetInstById, targetInstrumentId);
    if (targetInst) {
        return instrumentLabel(*targetInst);
    }
    return targetInstruments.empty() ? "(none)" : "(invalid)";
}

std::string targetSamplePreview(int targetSampleId, const std::vector<nspc::BrrSample>& targetSamples,
                                const IdLookup<nspc::BrrSample>& targetSampleById) {
    const auto* targetSample = lookupById(targetSampleById, targetSampleId);
    if (targetSample) {
        return sampleLabel(*targetSample);
    }
    return targetSamples.empty() ? "(none)" : "(invalid)";
}

void setDeleteSelection(std::set<int>& instrumentsToDelete, int instrumentId, bool checked) {
    if (checked) {
        instrumentsToDelete.insert(instrumentId);
        return;
    }
    instrumentsToDelete.erase(instrumentId);
}

void syncProjectAramToSpcData(const nspc::NspcProject& project, std::vector<uint8_t>& spcData) {
    constexpr size_t kSpcHeaderSize = 0x100;
    constexpr size_t kAramSize = 0x10000;
    if (spcData.size() < kSpcHeaderSize + kAramSize) {
        return;
    }
    const auto aramAll = project.aram().all();
    std::copy(aramAll.begin(), aramAll.end(), spcData.begin() + static_cast<std::ptrdiff_t>(kSpcHeaderSize));
}

void drawMappingSourceColumns(app::AppState& appState, const nspc::NspcProject& sourceProject,
                              const nspc::InstrumentMapping& mapping,
                              const IdLookup<nspc::NspcInstrument>& sourceInstById,
                              const IdLookup<nspc::BrrSample>& sourceSampleById) {
    ImGui::TableSetColumnIndex(0);
    {
        std::string srcLabel = sourceMappingLabel(mapping, sourceInstById, sourceSampleById);
        ImGui::TextUnformatted(srcLabel.c_str());
    }

    ImGui::TableSetColumnIndex(1);
    {
        const auto* sourceInst = lookupById(sourceInstById, mapping.sourceInstrumentId);
        if (sourceInst != nullptr && ImGui::SmallButton("Listen##src")) {
            previewInstrumentInProject(appState, sourceProject, *sourceInst);
        }
    }
}

void drawMappingTargetInstrumentColumns(app::AppState& appState, const nspc::NspcProject& targetProject,
                                        nspc::InstrumentMapping& mapping,
                                        const std::vector<nspc::NspcInstrument>& targetInstruments,
                                        const IdLookup<nspc::NspcInstrument>& targetInstById) {
    ImGui::TableSetColumnIndex(2);
    ImGui::SetNextItemWidth(-1);
    const char* actionItems[] = {"Copy", "Map To"};
    int actionIdx = (mapping.action == nspc::InstrumentMapping::Action::MapToExisting) ? 1 : 0;
    if (ImGui::Combo("##action", &actionIdx, actionItems, 2)) {
        mapping.action = (actionIdx == 1) ? nspc::InstrumentMapping::Action::MapToExisting
                                          : nspc::InstrumentMapping::Action::Copy;
        if (mapping.action == nspc::InstrumentMapping::Action::MapToExisting && !targetInstruments.empty()) {
            mapping.targetInstrumentId = targetInstruments.front().id;
        }
    }

    ImGui::TableSetColumnIndex(3);
    if (mapping.action == nspc::InstrumentMapping::Action::Copy) {
        ImGui::TextDisabled("new slot");
    } else {
        ImGui::SetNextItemWidth(-1);
        std::string tgtPreview = targetInstrumentPreview(mapping.targetInstrumentId, targetInstruments, targetInstById);
        if (ImGui::BeginCombo("##tgtinst", tgtPreview.c_str())) {
            for (const auto& targetInst : targetInstruments) {
                const bool selected = (targetInst.id == mapping.targetInstrumentId);
                if (ImGui::Selectable(instrumentLabel(targetInst).c_str(), selected)) {
                    mapping.targetInstrumentId = targetInst.id;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::TableSetColumnIndex(4);
    if (mapping.action == nspc::InstrumentMapping::Action::MapToExisting) {
        const auto* targetInst = lookupById(targetInstById, mapping.targetInstrumentId);
        if (targetInst != nullptr && ImGui::SmallButton("Listen##tgt")) {
            previewInstrumentInProject(appState, targetProject, *targetInst);
        }
    }
}

void drawMappingSampleColumns(nspc::InstrumentMapping& mapping, const std::vector<nspc::BrrSample>& targetSamples,
                              const IdLookup<nspc::BrrSample>& targetSampleById) {
    ImGui::TableSetColumnIndex(5);
    if (mapping.action == nspc::InstrumentMapping::Action::Copy) {
        ImGui::SetNextItemWidth(-1);
        const char* sampleActionItems[] = {"New", "Reuse", "Replace"};
        int sampleActionIndex = static_cast<int>(mapping.sampleAction);
        if (ImGui::Combo("##smpact", &sampleActionIndex, sampleActionItems, 3)) {
            mapping.sampleAction = static_cast<nspc::InstrumentMapping::SampleAction>(sampleActionIndex);
            if (mapping.sampleAction != nspc::InstrumentMapping::SampleAction::CopyNew && !targetSamples.empty()) {
                mapping.targetSampleId = targetSamples.front().id;
            }
        }
    } else {
        ImGui::TextDisabled("-");
    }

    ImGui::TableSetColumnIndex(6);
    if (mapping.action == nspc::InstrumentMapping::Action::Copy &&
        mapping.sampleAction != nspc::InstrumentMapping::SampleAction::CopyNew) {
        ImGui::SetNextItemWidth(-1);
        std::string samplePreview = targetSamplePreview(mapping.targetSampleId, targetSamples, targetSampleById);
        if (ImGui::BeginCombo("##tgtsmpl", samplePreview.c_str())) {
            for (const auto& targetSample : targetSamples) {
                const bool selected = (targetSample.id == mapping.targetSampleId);
                if (ImGui::Selectable(sampleLabel(targetSample).c_str(), selected)) {
                    mapping.targetSampleId = targetSample.id;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    } else if (mapping.action == nspc::InstrumentMapping::Action::Copy) {
        ImGui::TextDisabled("(auto)");
    } else {
        ImGui::TextDisabled("-");
    }
}

void drawInstrumentMappingRow(app::AppState& appState, const nspc::NspcProject& sourceProject,
                              const nspc::NspcProject& targetProject, nspc::InstrumentMapping& mapping, size_t rowIndex,
                              const IdLookup<nspc::NspcInstrument>& sourceInstById,
                              const IdLookup<nspc::BrrSample>& sourceSampleById,
                              const std::vector<nspc::NspcInstrument>& targetInstruments,
                              const IdLookup<nspc::NspcInstrument>& targetInstById,
                              const std::vector<nspc::BrrSample>& targetSamples,
                              const IdLookup<nspc::BrrSample>& targetSampleById) {
    ImGui::TableNextRow();
    ImGui::PushID(static_cast<int>(rowIndex));
    drawMappingSourceColumns(appState, sourceProject, mapping, sourceInstById, sourceSampleById);
    drawMappingTargetInstrumentColumns(appState, targetProject, mapping, targetInstruments, targetInstById);
    drawMappingSampleColumns(mapping, targetSamples, targetSampleById);
    ImGui::PopID();
}

void drawTargetInstrumentRemovalRow(app::AppState& appState, const nspc::NspcProject& targetProject,
                                    std::set<int>& instrumentsToDelete, const nspc::NspcInstrument& instrument,
                                    const IdLookup<nspc::BrrSample>& targetSampleById) {
    ImGui::TableNextRow();
    ImGui::PushID(instrument.id);

    ImGui::TableSetColumnIndex(0);
    bool checked = instrumentsToDelete.contains(instrument.id);
    if (ImGui::Checkbox("##del", &checked)) {
        setDeleteSelection(instrumentsToDelete, instrument.id, checked);
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(instrumentLabel(instrument).c_str());

    const auto* sample = lookupById(targetSampleById, instrument.sampleIndex);

    ImGui::TableSetColumnIndex(2);
    if (sample != nullptr) {
        ImGui::TextUnformatted(sampleLabel(*sample).c_str());
    } else {
        ImGui::TextDisabled("(none)");
    }

    ImGui::TableSetColumnIndex(3);
    if (sample != nullptr) {
        ImGui::Text("%zu B", sample->data.size());
    } else {
        ImGui::TextDisabled("---");
    }

    ImGui::TableSetColumnIndex(4);
    if (ImGui::SmallButton("Listen")) {
        previewInstrumentInProject(appState, targetProject, instrument);
    }

    ImGui::PopID();
}

void previewInstrumentInProject(app::AppState& appState, const nspc::NspcProject& project,
                                const nspc::NspcInstrument& inst) {
    if (!appState.spcPlayer) {
        return;
    }

    const auto& engine = project.engineConfig();
    if (engine.sampleHeaders == 0) {
        return;
    }

    appState.spcPlayer->allNotesOff();

    auto sourceAram = project.aram();
    auto playerAram = appState.spcPlayer->spcDsp().aram();
    std::copy(sourceAram.all().begin(), sourceAram.all().end(), playerAram.all().begin());

    appState.spcPlayer->spcDsp().writeDspRegister(0x5D, static_cast<uint8_t>(engine.sampleHeaders >> 8));

    audio::NotePreviewParams params{};
    params.sampleIndex = static_cast<uint8_t>(inst.sampleIndex & 0x7F);
    params.pitch = audio::NotePreviewParams::pitchFromMidi(60, 0x1000);  // Middle C
    params.volumeL = 127;
    params.volumeR = 127;
    params.adsr1 = inst.adsr1;
    params.adsr2 = inst.adsr2;
    params.gain = inst.gain;
    params.voice = 0;

    appState.spcPlayer->noteOn(params);
}

}  // namespace

SongPortDialog::SongPortDialog(app::AppState& appState) : appState_(appState) {}

void SongPortDialog::open() {
    pendingOpen_ = true;
    portError_.clear();
    portStatus_.clear();

    if (appState_.project.has_value()) {
        const int selected = appState_.selectedSongIndex;
        const int songCount = static_cast<int>(appState_.project->songs().size());
        if (selected >= 0 && selected < songCount) {
            sourceSongIndex_ = selected;
        } else {
            sourceSongIndex_ = 0;
        }
    } else {
        sourceSongIndex_ = 0;
    }

    rebuildMappings();
}

void SongPortDialog::draw() {
    if (pendingOpen_) {
        ImGui::OpenPopup("Port Song##dialog");
        pendingOpen_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(900, 850), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Port Song##dialog", nullptr, ImGuiWindowFlags_NoResize)) {
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

    const auto& sourceProject = *appState_.project;
    drawSourceSongSection(sourceProject);
    drawTargetEngineSection();

    if (targetProject_.has_value()) {
        drawInstrumentMappingSection(sourceProject);
        drawTargetInstrumentsRemovalSection();
        drawTargetSongSlotSection();

        ImGui::Spacing();
        const bool canPort = !sourceProject.songs().empty();
        ImGui::BeginDisabled(!canPort);
        if (ImGui::Button("Port Song", ImVec2(120, 0))) {
            executePort();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
    }

    // --- Cancel / status ---
    if (ImGui::Button("Cancel", ImVec2(80, 0))) {
        ImGui::CloseCurrentPopup();
    }

    drawStatusSection();

    ImGui::EndPopup();
}

void SongPortDialog::drawSourceSongSection(const nspc::NspcProject& sourceProject) {
    ImGui::SeparatorText("Source Song");

    const auto& songs = sourceProject.songs();
    if (songs.empty()) {
        ImGui::TextDisabled("No songs in current project.");
        ImGui::Spacing();
        return;
    }

    if (sourceSongIndex_ >= static_cast<int>(songs.size())) {
        sourceSongIndex_ = 0;
        rebuildMappings();
    }

    std::string previewLabel = songLabel(songs[sourceSongIndex_], sourceSongIndex_);
    if (ImGui::BeginCombo("Song##source", previewLabel.c_str())) {
        for (int i = 0; i < static_cast<int>(songs.size()); ++i) {
            const bool selected = (i == sourceSongIndex_);
            if (ImGui::Selectable(songLabel(songs[i], i).c_str(), selected)) {
                if (sourceSongIndex_ != i) {
                    sourceSongIndex_ = i;
                    rebuildMappings();
                }
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
}

void SongPortDialog::drawTargetEngineSection() {
    ImGui::SeparatorText("Target Engine");

    if (ImGui::Button("Select Target SPC...")) {
        NFD::UniquePath outPath;
        nfdfilteritem_t filters[1] = {{"SPC Files", "spc"}};
        if (NFD::OpenDialog(outPath, filters, 1) == NFD_OKAY) {
            if (loadTargetSpc(std::filesystem::path(outPath.get()))) {
                rebuildMappings();
            }
        }
    }

    ImGui::SameLine();
    if (targetSpcPath_.has_value()) {
        ImGui::TextUnformatted(targetSpcPath_->filename().string().c_str());
        if (targetProject_.has_value()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", targetProject_->engineConfig().name.c_str());
        }
    } else {
        ImGui::TextDisabled("(none)");
    }

    if (!targetLoadError_.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", targetLoadError_.c_str());
    }

    ImGui::Spacing();
}

void SongPortDialog::drawInstrumentMappingSection(const nspc::NspcProject& sourceProject) {
    if (!targetProject_.has_value()) {
        return;
    }

    ImGui::SeparatorText("Instrument Mapping");

    const auto& srcInstruments = sourceProject.instruments();
    const auto& srcSamples = sourceProject.samples();
    const auto& tgtInstruments = targetProject_->instruments();
    const auto& tgtSamples = targetProject_->samples();
    const auto sourceInstById = buildIdLookup(srcInstruments);
    const auto sourceSampleById = buildIdLookup(srcSamples);
    const auto targetInstById = buildIdLookup(tgtInstruments);
    const auto targetSampleById = buildIdLookup(tgtSamples);

    if (usedInstrumentIds_.empty()) {
        ImGui::TextDisabled("No instruments used in this song.");
        ImGui::Spacing();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 3));
    if (ImGui::BeginTable("##instrmap", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 200))) {
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Inst", ImGuiTableColumnFlags_WidthFixed, 75);
        ImGui::TableSetupColumn("Target Inst", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Sample", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Target Sample", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < instrumentMappings_.size(); ++i) {
            auto& mapping = instrumentMappings_[i];
            drawInstrumentMappingRow(appState_, sourceProject, *targetProject_, mapping, i, sourceInstById,
                                     sourceSampleById, tgtInstruments, targetInstById, tgtSamples, targetSampleById);
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::Spacing();
}

void SongPortDialog::drawTargetInstrumentsRemovalSection() {
    if (!targetProject_.has_value()) {
        return;
    }

    const auto& tgtInstruments = targetProject_->instruments();
    const auto& tgtSamples = targetProject_->samples();
    const auto targetSampleById = buildIdLookup(tgtSamples);

    ImGui::SeparatorText("Target Instruments to Remove");
    ImGui::TextDisabled("Mark instruments to delete before porting. Unused samples from those instruments are removed too.");

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 3));
    if (ImGui::BeginTable("##tgtinstruments", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 220))) {
        ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 28);
        ImGui::TableSetupColumn("Instrument", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Sample", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 65);
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, 65);
        ImGui::TableHeadersRow();

        for (const auto& instrument : tgtInstruments) {
            drawTargetInstrumentRemovalRow(appState_, *targetProject_, instrumentsToDelete_, instrument, targetSampleById);
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::Spacing();
}

void SongPortDialog::drawTargetSongSlotSection() {
    if (!targetProject_.has_value()) {
        return;
    }

    ImGui::SeparatorText("Target Song Slot");
    ImGui::RadioButton("Append as new song", &appendNewSong_, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Overwrite existing", &appendNewSong_, 0);

    if (appendNewSong_ != 0) {
        return;
    }

    const auto& targetSongs = targetProject_->songs();
    ImGui::SetNextItemWidth(300);
    if (targetSongs.empty()) {
        ImGui::TextDisabled("Target has no songs.");
        return;
    }

    if (targetSongOverwriteIndex_ >= static_cast<int>(targetSongs.size())) {
        targetSongOverwriteIndex_ = 0;
    }
    std::string targetPreview = songLabel(targetSongs[targetSongOverwriteIndex_], targetSongOverwriteIndex_);
    if (!ImGui::BeginCombo("##tgtsong", targetPreview.c_str())) {
        return;
    }

    for (int i = 0; i < static_cast<int>(targetSongs.size()); ++i) {
        const bool selected = (i == targetSongOverwriteIndex_);
        if (ImGui::Selectable(songLabel(targetSongs[i], i).c_str(), selected)) {
            targetSongOverwriteIndex_ = i;
        }
        if (selected) {
            ImGui::SetItemDefaultFocus();
        }
    }
    ImGui::EndCombo();
}

void SongPortDialog::drawStatusSection() {
    if (!portError_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s", portError_.c_str());
    }
    if (!portStatus_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.4f, 1.0f), "%s", portStatus_.c_str());
    }
}

void SongPortDialog::rebuildMappings() {
    portError_.clear();
    portStatus_.clear();

    if (!appState_.project.has_value()) {
        usedInstrumentIds_.clear();
        instrumentMappings_.clear();
        instrumentsToDelete_.clear();
        return;
    }

    usedInstrumentIds_ = nspc::findUsedInstrumentIds(*appState_.project, sourceSongIndex_);

    if (targetProject_.has_value()) {
        instrumentMappings_ = nspc::buildDefaultMappings(*appState_.project, *targetProject_, sourceSongIndex_);
    } else {
        instrumentMappings_.clear();
    }
}

bool SongPortDialog::loadTargetSpc(const std::filesystem::path& path) {
    targetLoadError_.clear();
    targetProject_.reset();
    targetSpcData_.clear();
    instrumentsToDelete_.clear();

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        targetLoadError_ = std::format("Failed to open '{}'", path.filename().string());
        return false;
    }
    targetSpcData_ = std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});

    auto parseResult = nspc::NspcParser::load(targetSpcData_);
    if (!parseResult.has_value()) {
        targetLoadError_ = std::format("Failed to parse SPC '{}'", path.filename().string());
        targetSpcData_.clear();
        return false;
    }

    targetProject_ = std::move(*parseResult);
    targetSpcPath_ = path;
    return true;
}

void SongPortDialog::executePort() {
    portError_.clear();
    portStatus_.clear();

    if (!appState_.project.has_value() || !targetProject_.has_value()) {
        portError_ = "Missing source or target project.";
        return;
    }

    nspc::SongPortRequest req;
    req.sourceSongIndex = sourceSongIndex_;
    req.targetSongIndex = (appendNewSong_ != 0) ? -1 : targetSongOverwriteIndex_;
    req.instrumentMappings = instrumentMappings_;
    req.instrumentsToDelete.assign(instrumentsToDelete_.begin(), instrumentsToDelete_.end());

    nspc::NspcProject targetCopy = *targetProject_;
    nspc::SongPortResult portResult = nspc::portSong(*appState_.project, targetCopy, req);

    if (!portResult.success) {
        portError_ = std::format("Port failed: {}", portResult.error);
        return;
    }

    portStatus_ = std::format("Song ported successfully to slot {}. Loading target project...",
                              portResult.resultSongIndex);

    // Playback patches song/sequence data on top of loaded SPC bytes, so mirror ARAM updates into SPC data.
    syncProjectAramToSpcData(targetCopy, targetSpcData_);

    // Keep dialog target state in sync so repeated ports keep working without reloading.
    targetProject_ = targetCopy;

    if (onInstallProject) {
        onInstallProject(targetCopy, targetSpcData_, targetSpcPath_);
    }

    ImGui::CloseCurrentPopup();
}

}  // namespace ntrak::ui
