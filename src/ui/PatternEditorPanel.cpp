#include "ntrak/ui/PatternEditorPanel.hpp"

#include "ntrak/ui/PatternEditorPanelUtils.hpp"
#include "ntrak/app/App.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ntrak::ui {

using namespace detail;

namespace {

void normalizeSelectedInstrumentId(app::AppState& appState, const nspc::NspcProject& project) {
    const auto& instruments = project.instruments();
    if (instruments.empty()) {
        appState.selectedInstrumentId = -1;
        return;
    }

    const bool hasSelectedInstrument = std::any_of(
        instruments.begin(), instruments.end(),
        [&](const nspc::NspcInstrument& instrument) { return instrument.id == appState.selectedInstrumentId; });
    if (!hasSelectedInstrument) {
        appState.selectedInstrumentId = instruments.front().id;
    }
}

bool songContainsPatternId(const nspc::NspcSong& song, int patternId) {
    return std::any_of(song.patterns().begin(), song.patterns().end(),
                       [patternId](const nspc::NspcPattern& pattern) { return pattern.id == patternId; });
}

std::optional<int> patternIdFromSequenceRow(const nspc::NspcSong& song, int sequenceRow) {
    const auto& sequence = song.sequence();
    if (sequenceRow < 0 || sequenceRow >= static_cast<int>(sequence.size())) {
        return std::nullopt;
    }
    if (const auto* play = std::get_if<nspc::PlayPattern>(&sequence[static_cast<size_t>(sequenceRow)])) {
        if (songContainsPatternId(song, play->patternId)) {
            return play->patternId;
        }
    }
    return std::nullopt;
}

std::optional<int> resolveActivePlaybackPatternId(const nspc::NspcSong& song, int playbackPatternId,
                                                  int playbackSequenceRow) {
    if (playbackPatternId >= 0 && songContainsPatternId(song, playbackPatternId)) {
        return playbackPatternId;
    }
    return patternIdFromSequenceRow(song, playbackSequenceRow);
}

}  // namespace

PatternEditorPanel::PatternEditorPanel(app::AppState& appState) : appState_(appState) {
    flattenOptions_.maxSubroutineDepth = 16;
    flattenOptions_.maxEventsPerChannel = 50000;
    flattenOptions_.maxTicksPerChannel = kMaxVisibleRows;

    // Wire copy/cut/paste callbacks (only active when pattern editor has focus)
    appState_.copy = [this]() {
        if (!appState_.project.has_value()) {
            return;
        }
        (void)copyCellSelectionToClipboard();
    };

    appState_.cut = [this]() {
        if (!appState_.project.has_value()) {
            return;
        }
        const auto& songs = appState_.project->songs();
        if (appState_.selectedSongIndex < 0 || appState_.selectedSongIndex >= static_cast<int>(songs.size())) {
            return;
        }
        if (appState_.lockEngineContent && songs[static_cast<size_t>(appState_.selectedSongIndex)].isEngineProvided()) {
            return;
        }
        auto& song = appState_.project->songs()[appState_.selectedSongIndex];
        if (auto patternId = resolveSelectedPatternId(song)) {
            if (copyCellSelectionToClipboard()) {
                clearSelectedCells(song, *patternId);
                rebuildPatternRows(song, *patternId);
            }
        }
    };

    appState_.paste = [this]() {
        if (!appState_.project.has_value()) {
            return;
        }
        const auto& songs = appState_.project->songs();
        if (appState_.selectedSongIndex < 0 || appState_.selectedSongIndex >= static_cast<int>(songs.size())) {
            return;
        }
        if (appState_.lockEngineContent && songs[static_cast<size_t>(appState_.selectedSongIndex)].isEngineProvided()) {
            return;
        }
        auto& song = appState_.project->songs()[appState_.selectedSongIndex];
        if (auto patternId = resolveSelectedPatternId(song)) {
            if (pasteClipboardAtCursor(song, *patternId)) {
                rebuildPatternRows(song, *patternId);
            }
        }
    };
}

std::optional<int> PatternEditorPanel::resolveSelectedPatternId(const nspc::NspcSong& song) {
    const auto& patterns = song.patterns();
    auto pattern_exists = [&](int patternId) {
        return std::any_of(patterns.begin(), patterns.end(),
                           [patternId](const nspc::NspcPattern& pattern) { return pattern.id == patternId; });
    };

    const auto& sequence = song.sequence();
    if (appState_.selectedSequenceRow < 0 || appState_.selectedSequenceRow >= static_cast<int>(sequence.size())) {
        appState_.selectedPatternId.reset();
        return std::nullopt;
    }

    if (const auto* play =
            std::get_if<nspc::PlayPattern>(&sequence[static_cast<size_t>(appState_.selectedSequenceRow)])) {
        if (pattern_exists(play->patternId)) {
            appState_.selectedPatternId = play->patternId;
            return play->patternId;
        }
    }

    appState_.selectedPatternId.reset();
    return std::nullopt;
}

void PatternEditorPanel::rebuildPatternRows(const nspc::NspcSong& song, int patternId) {
    rows_.clear();
    flatPattern_.reset();
    rowsTruncated_ = false;

    flatPattern_ = nspc::flattenPatternById(song, patternId, flattenOptions_);
    if (!flatPattern_.has_value()) {
        return;
    }

    size_t maxTick = static_cast<size_t>(flatPattern_->totalTicks);
    for (const auto& channel : flatPattern_->channels) {
        for (const auto& event : channel.events) {
            maxTick = std::max(maxTick, static_cast<size_t>(event.tick));
        }
    }

    size_t rowCount = std::max(static_cast<size_t>(kDefaultVisibleRows), maxTick + 1);
    if (rowCount > kMaxVisibleRows) {
        rowCount = kMaxVisibleRows;
        rowsTruncated_ = true;
    }

    rows_ = std::vector<PatternRow>(rowCount);
    const size_t patternEndExclusive = std::min(rows_.size(), static_cast<size_t>(flatPattern_->totalTicks) + 1);

    for (int channelIndex = 0; channelIndex < kChannels; ++channelIndex) {
        const auto& flatChannel = flatPattern_->channels[static_cast<size_t>(channelIndex)];
        int currentInstrument = -1;
        int currentVolume = -1;
        uint8_t currentDurationTicks = 1;
        std::optional<uint8_t> currentQvByte = std::nullopt;
        int instrumentSetTick = -1;
        int volumeSetTick = -1;
        int qvSetTick = -1;
        // Volume fade tracking
        int fadeStartTick = -1;
        int fadeStartVolume = -1;
        int fadeDuration = 0;
        int fadeTargetVolume = -1;

        auto computeVolumeAtTick = [&](int tick) -> int {
            if (fadeStartTick >= 0 && fadeDuration > 0 && currentVolume >= 0) {
                const int elapsed = tick - fadeStartTick;
                if (elapsed >= fadeDuration) {
                    currentVolume = fadeTargetVolume;
                    fadeStartTick = -1;
                    return currentVolume;
                }
                if (elapsed > 0) {
                    return fadeStartVolume + (fadeTargetVolume - fadeStartVolume) * elapsed / fadeDuration;
                }
            }
            return currentVolume;
        };

        auto mark_subroutine_span = [&](size_t startRow, size_t spanLength, int subroutineId) {
            if (subroutineId < 0 || spanLength == 0) {
                return;
            }
            const size_t endRow = std::min(startRow + spanLength, patternEndExclusive);
            for (size_t row = startRow; row < endRow; ++row) {
                auto& target = rows_[row][static_cast<size_t>(channelIndex)];
                target.hasSubroutineData = true;
                target.subroutineId = subroutineId;
            }
        };

        for (const auto& flatEvent : flatChannel.events) {
            const size_t row = static_cast<size_t>(flatEvent.tick);
            if (row >= rows_.size()) {
                rowsTruncated_ = true;
                continue;
            }

            auto& cell = rows_[row][static_cast<size_t>(channelIndex)];
            int eventSubroutineId = -1;
            const bool isSubroutineEnd = !flatEvent.subroutineStack.empty() &&
                                         std::holds_alternative<nspc::End>(flatEvent.event);
            if (!flatEvent.subroutineStack.empty() && !isSubroutineEnd) {
                const auto& frame = flatEvent.subroutineStack.back();
                eventSubroutineId = frame.subroutineId;
                cell.hasSubroutineData = true;
                cell.subroutineId = frame.subroutineId;
            }

            if (const auto* duration = std::get_if<nspc::Duration>(&flatEvent.event)) {
                currentDurationTicks = std::max<uint8_t>(duration->ticks, 1);
                if (duration->quantization.has_value() || duration->velocity.has_value()) {
                    const uint8_t quant = static_cast<uint8_t>(duration->quantization.value_or(0) & 0x07);
                    const uint8_t vel = static_cast<uint8_t>(duration->velocity.value_or(0) & 0x0F);
                    currentQvByte = static_cast<uint8_t>((quant << 4) | vel);
                    qvSetTick = static_cast<int>(row);
                    cell.qv = std::format("{:02X}", *currentQvByte);
                    cell.qvDerived = false;
                }
                continue;
            }

            if (const auto* vcmd = std::get_if<nspc::Vcmd>(&flatEvent.event)) {
                std::visit(nspc::overloaded{
                               [&](const nspc::VcmdInst& inst) {
                                   currentInstrument = inst.instrumentIndex;
                                   instrumentSetTick = static_cast<int>(row);
                                   cell.instrument = std::format("{:02X}", inst.instrumentIndex);
                               },
                               [&](const nspc::VcmdVolume& volume) {
                                   currentVolume = volume.volume;
                                   volumeSetTick = static_cast<int>(row);
                                   fadeStartTick = -1;  // explicit volume cancels any fade
                                   cell.volume = std::format("{:02X}", volume.volume);
                               },
                               [&](const nspc::VcmdVolumeFade& vf) {
                                   fadeStartTick = static_cast<int>(row);
                                   fadeStartVolume = currentVolume >= 0 ? currentVolume : 0;
                                   fadeDuration = vf.time;
                                   fadeTargetVolume = vf.target;
                               },
                               [&](const auto&) {},
                           },
                           vcmd->vcmd);

                const bool showAsFxChip = std::visit(nspc::overloaded{
                                                         [](const nspc::VcmdInst&) { return false; },
                                                         [](const nspc::VcmdVolume&) { return false; },
                                                         [](const auto&) { return true; },
                                                     },
                                                     vcmd->vcmd);
                if (showAsFxChip) {
                    cell.effects.push_back(makeEffectChipFromVcmd(*vcmd));
                }
                continue;
            }

            if (const auto* note = std::get_if<nspc::Note>(&flatEvent.event)) {
                cell.note = noteToString(note->pitch);
                if (currentInstrument >= 0) {
                    cell.instrumentDerived = (instrumentSetTick != static_cast<int>(row));
                    cell.instrument = std::format("{:02X}", currentInstrument);
                }
                if (currentVolume >= 0) {
                    cell.volumeDerived = (volumeSetTick != static_cast<int>(row));
                    const int displayVolume = computeVolumeAtTick(static_cast<int>(row));
                    cell.volume = std::format("{:02X}", displayVolume);
                }
                if (currentQvByte.has_value()) {
                    cell.qvDerived = (qvSetTick != static_cast<int>(row));
                    cell.qv = std::format("{:02X}", *currentQvByte);
                }
                mark_subroutine_span(row, currentDurationTicks, eventSubroutineId);
                if (currentDurationTicks > 1) {
                    const size_t endExclusive = std::min(row + currentDurationTicks, patternEndExclusive);
                    for (size_t spanRow = row + 1; spanRow < endExclusive; ++spanRow) {
                        auto& target = rows_[spanRow][static_cast<size_t>(channelIndex)];
                        const bool isEnd = (spanRow + 1 == endExclusive);
                        target.note = isEnd ? "^^>" : "^^^";
                        target.instrument = "..";
                        target.volume = "..";
                        target.qv = "..";
                    }
                }
                continue;
            }

            if (std::holds_alternative<nspc::Tie>(flatEvent.event)) {
                const size_t spanLength = std::max<size_t>(currentDurationTicks, 1);
                const size_t endExclusive = std::min(row + spanLength, patternEndExclusive);
                for (size_t spanRow = row; spanRow < endExclusive; ++spanRow) {
                    auto& target = rows_[spanRow][static_cast<size_t>(channelIndex)];
                    if (spanRow == row) {
                        target.note = "~~~";
                    } else {
                        target.note = "^^^";
                    }
                    if (spanRow != row) {
                        target.instrument = "..";
                        target.volume = "..";
                        target.qv = "..";
                    }
                }
                mark_subroutine_span(row, spanLength, eventSubroutineId);
                continue;
            }

            if (std::holds_alternative<nspc::Rest>(flatEvent.event)) {
                const size_t spanLength = std::max<size_t>(currentDurationTicks, 1);
                const size_t endExclusive = std::min(row + spanLength, patternEndExclusive);
                for (size_t spanRow = row; spanRow < endExclusive; ++spanRow) {
                    auto& target = rows_[spanRow][static_cast<size_t>(channelIndex)];
                    if (spanRow == row) {
                        target.note = "===";
                    } else {
                        target.note = "---";
                    }
                    if (spanRow != row) {
                        target.instrument = "..";
                        target.volume = "..";
                        target.qv = "..";
                    }
                }
                mark_subroutine_span(row, spanLength, eventSubroutineId);
                continue;
            }

            if (const auto* perc = std::get_if<nspc::Percussion>(&flatEvent.event)) {
                cell.note = std::format("P{:02X}", perc->index);
                if (currentInstrument >= 0) {
                    cell.instrumentDerived = (instrumentSetTick != static_cast<int>(row));
                    cell.instrument = std::format("{:02X}", currentInstrument);
                }
                if (currentVolume >= 0) {
                    cell.volumeDerived = (volumeSetTick != static_cast<int>(row));
                    const int displayVolume = computeVolumeAtTick(static_cast<int>(row));
                    cell.volume = std::format("{:02X}", displayVolume);
                }
                if (currentQvByte.has_value()) {
                    cell.qvDerived = (qvSetTick != static_cast<int>(row));
                    cell.qv = std::format("{:02X}", *currentQvByte);
                }
                mark_subroutine_span(row, currentDurationTicks, eventSubroutineId);
                if (currentDurationTicks > 1) {
                    const size_t endExclusive = std::min(row + currentDurationTicks, patternEndExclusive);
                    for (size_t spanRow = row + 1; spanRow < endExclusive; ++spanRow) {
                        auto& target = rows_[spanRow][static_cast<size_t>(channelIndex)];
                        const bool isEnd = (spanRow + 1 == endExclusive);
                        target.note = isEnd ? "^^>" : "^^^";
                        target.instrument = "..";
                        target.volume = "..";
                        target.qv = "..";
                    }
                }
                continue;
            }

            if (std::holds_alternative<nspc::End>(flatEvent.event)) {
                if (flatEvent.subroutineStack.empty()) {
                    cell.hasEndMarker = true;
                }
                continue;
            }
        }
    }

    // Post-pass: detect subroutine start/end transitions
    for (int ch = 0; ch < kChannels; ++ch) {
        for (size_t row = 0; row < rows_.size(); ++row) {
            auto& cell = rows_[row][static_cast<size_t>(ch)];
            if (!cell.hasSubroutineData || cell.subroutineId < 0) {
                continue;
            }

            bool prevHasSameSub = false;
            if (row > 0) {
                const auto& prev = rows_[row - 1][static_cast<size_t>(ch)];
                prevHasSameSub = prev.hasSubroutineData && prev.subroutineId == cell.subroutineId;
            }
            cell.isSubroutineStart = !prevHasSameSub;

            bool nextHasSameSub = false;
            if (row + 1 < rows_.size()) {
                const auto& next = rows_[row + 1][static_cast<size_t>(ch)];
                nextHasSameSub = next.hasSubroutineData && next.subroutineId == cell.subroutineId;
            }
            cell.isSubroutineEnd = !nextHasSameSub;
        }
    }
}

void PatternEditorPanel::applyPlaybackChannelMaskToPlayer(bool forceWhileStopped) {
    if (!appState_.spcPlayer) {
        return;
    }
    if (!forceWhileStopped && !appState_.spcPlayer->isPlaying()) {
        return;
    }
    appState_.spcPlayer->setChannelMask(appState_.playback.channelMask);
}

void PatternEditorPanel::handleChannelHeaderClick(int channel, bool soloModifier) {
    if (channel < 0 || channel >= kChannels) {
        return;
    }

    const uint8_t bit = static_cast<uint8_t>(1u << channel);
    uint8_t nextMask = appState_.playback.channelMask;
    if (soloModifier) {
        nextMask = (appState_.playback.channelMask == bit) ? 0xFFu : bit;
    } else {
        nextMask ^= bit;
    }

    appState_.playback.channelMask = nextMask;
    applyPlaybackChannelMaskToPlayer(false);
}

void PatternEditorPanel::draw() {
    if (!appState_.project.has_value()) {
        stopTrackerPreview();
        ImGui::TextDisabled("No project loaded");
        ImGui::TextDisabled("Import an SPC to see pattern data");
        return;
    }

    auto& project = appState_.project.value();
    auto& songs = project.songs();
    if (songs.empty()) {
        stopTrackerPreview();
        ImGui::TextDisabled("Project has no songs");
        return;
    }

    if (appState_.selectedSongIndex < 0 || appState_.selectedSongIndex >= static_cast<int>(songs.size())) {
        stopTrackerPreview();
        ImGui::TextDisabled("Selected song index is out of range");
        return;
    }

    auto& song = songs[static_cast<size_t>(appState_.selectedSongIndex)];
    normalizeSelectedInstrumentId(appState_, project);
    const bool songLocked = appState_.lockEngineContent && song.isEngineProvided();

    const int playbackPatternId = appState_.playback.patternId.load(std::memory_order_relaxed);
    const int playbackSequenceRow = appState_.playback.sequenceRow.load(std::memory_order_relaxed);
    const int playbackTick = appState_.playback.patternTick.load(std::memory_order_relaxed);
    const bool followPlayback = appState_.playback.followPlayback &&
                                appState_.playback.hooksInstalled.load(std::memory_order_relaxed);
    const auto activePlaybackPatternId = resolveActivePlaybackPatternId(song, playbackPatternId, playbackSequenceRow);

    std::optional<int> patternId;
    if (followPlayback && activePlaybackPatternId.has_value()) {
        patternId = *activePlaybackPatternId;
        appState_.selectedPatternId = *activePlaybackPatternId;
    } else {
        patternId = resolveSelectedPatternId(song);
    }
    if (!patternId.has_value()) {
        stopTrackerPreview();
        lastViewedSongIndex_ = appState_.selectedSongIndex;
        lastViewedPatternId_.reset();
        pendingScrollToSelection_ = false;
        if (appState_.selectedSequenceRow < 0) {
            ImGui::TextDisabled("No pattern selected");
            ImGui::TextDisabled("Select a PlayPattern row in Sequence Editor");
        } else {
            ImGui::TextDisabled("Selected sequence row %02X is not a PlayPattern", appState_.selectedSequenceRow);
            ImGui::TextDisabled("Pick a row with a pattern ID in Sequence Editor");
        }
        return;
    }

    const bool patternViewChanged = (lastViewedSongIndex_ != appState_.selectedSongIndex) ||
                                    (!lastViewedPatternId_.has_value()) || (*lastViewedPatternId_ != *patternId);
    lastViewedSongIndex_ = appState_.selectedSongIndex;
    lastViewedPatternId_ = *patternId;

    rebuildPatternRows(song, *patternId);
    if (patternViewChanged && !rows_.empty()) {
        int firstNoteRow = 0;
        bool found = false;
        for (size_t row = 0; row < rows_.size() && !found; ++row) {
            for (int ch = 0; ch < kChannels; ++ch) {
                if (canShowInstVol(rows_[row][static_cast<size_t>(ch)].note)) {
                    firstNoteRow = static_cast<int>(row);
                    found = true;
                    break;
                }
            }
        }

        selectedRow_ = firstNoteRow;
        selectedChannel_ = 0;
        selectedItem_ = 0;
        hexInput_.clear();
        clearCellSelection();
        selectionAnchorValid_ = false;
        mouseSelecting_ = false;
        pendingScrollToSelection_ = true;
    }
    clampSelectionToRows();
    const bool patternMutated = songLocked ? false : handleKeyboardEditing(song, *patternId);
    if (patternMutated) {
        rebuildPatternRows(song, *patternId);
    }
    clampSelectionToRows();

    const bool trackingPlayback = followPlayback && activePlaybackPatternId.has_value() &&
                                  *activePlaybackPatternId == *patternId && playbackTick >= 0;
    ticksPerRow_ = std::clamp(ticksPerRow_, kMinTicksPerRow, kMaxTicksPerRow);
    const auto visible_row_count = [&]() -> size_t {
        const size_t ticksPerRow = static_cast<size_t>(std::max(ticksPerRow_, kMinTicksPerRow));
        return (rows_.size() + ticksPerRow - 1) / ticksPerRow;
    };

    ImGui::PushFont(ntrak::app::App::fonts().mono, 14.0f);
    ImGui::Text("Pattern %02X", *patternId);
    if (followPlayback && playbackSequenceRow >= 0) {
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextDisabled("live row %02X", playbackSequenceRow);
        if (playbackTick >= 0) {
            ImGui::SameLine(0.0f, 10.0f);
            ImGui::TextDisabled("tick %04X", playbackTick);
        }
    } else if (appState_.selectedSequenceRow >= 0) {
        const auto& sequence = song.sequence();
        if (appState_.selectedSequenceRow < static_cast<int>(sequence.size())) {
            if (const auto* play =
                    std::get_if<nspc::PlayPattern>(&sequence[static_cast<size_t>(appState_.selectedSequenceRow)]);
                play && play->patternId == *patternId) {
                ImGui::SameLine(0.0f, 16.0f);
                ImGui::TextDisabled("from row %02X", appState_.selectedSequenceRow);
            }
        }
    }

    ImGui::SameLine(0.0f, 16.0f);
    ImGui::TextDisabled("Ticks/Row");
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::SetNextItemWidth(60.0f);
    if (ImGui::BeginCombo("##ticks_per_row", std::format("{}", ticksPerRow_).c_str())) {
        for (int option = kMinTicksPerRow; option <= kMaxTicksPerRow; ++option) {
            const bool selected = (option == ticksPerRow_);
            if (ImGui::Selectable(std::format("{}", option).c_str(), selected)) {
                ticksPerRow_ = option;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::TextDisabled("* hidden");
    if (ticksPerRow_ > 1) {
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextDisabled("(events may occur on hidden ticks)");
    }

    if (flatPattern_.has_value()) {
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextDisabled("Ticks: %u | Rows: %zu", flatPattern_->totalTicks, visible_row_count());
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::BeginDisabled(songLocked);
        if (ImGui::Button("Length...")) {
            patternLengthPopupOpen_ = true;
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::TextDisabled("Oct");
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::SetNextItemWidth(46.0f);
    appState_.trackerInputOctave = std::clamp(appState_.trackerInputOctave, 0, 7);
    if (ImGui::BeginCombo("##octave", std::format("{}", appState_.trackerInputOctave).c_str())) {
        for (int oct = 0; oct <= 7; ++oct) {
            const bool selected = (oct == appState_.trackerInputOctave);
            if (ImGui::Selectable(std::format("{}", oct).c_str(), selected)) {
                appState_.trackerInputOctave = oct;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine(0.0f, 10.0f);
    ImGui::TextDisabled("Step");
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::SetNextItemWidth(52.0f);
    if (ImGui::BeginCombo("##editstep", std::format("{}", editStep_).c_str())) {
        for (int s = 0; s <= 16; ++s) {
            const bool selected = (s == editStep_);
            if (ImGui::Selectable(std::format("{}", s).c_str(), selected)) {
                editStep_ = s;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine(0.0f, 16.0f);
    int selectionCount = 0;
    for (const uint8_t cellSelected : selectedCells_) {
        selectionCount += (cellSelected != 0U) ? 1 : 0;
    }
    if (selectionCount > 1) {
        ImGui::TextDisabled("Sel %04X C%d %s (%d cells)", std::max(selectedRow_, 0),
                            std::max(selectedChannel_, 0) + 1, itemLabel(selectedItem_),
                            selectionCount);
    } else {
        ImGui::TextDisabled("Sel %04X C%d %s", std::max(selectedRow_, 0),
                            std::max(selectedChannel_, 0) + 1, itemLabel(selectedItem_));
    }
    if (!hexInput_.empty()) {
        ImGui::SameLine(0.0f, 10.0f);
        if (selectedItem_ == 4) {
            std::string fmtHex;
            for (size_t i = 0; i < hexInput_.size(); ++i) {
                if (i > 0 && i % 2 == 0) {
                    fmtHex += ' ';
                }
                fmtHex += hexInput_[i];
            }

            const auto selectedRow = static_cast<size_t>(std::max(selectedRow_, 0));
            const auto selectedChannel = static_cast<size_t>(std::clamp(selectedChannel_, 0, kChannels - 1));
            const PatternCell* selectedCell = (selectedRow < rows_.size()) ? &rows_[selectedRow][selectedChannel] : nullptr;
            const EffectChip* singleFx = nullptr;
            if (selectedCell && selectedCell->effects.size() == 1) {
                singleFx = &selectedCell->effects.front();
            }
            const auto labelForFxId = [&](uint8_t id) -> std::string {
                if (const auto* extension = extensionVcmdInfoForCurrentEngine(id); extension != nullptr) {
                    if (!extension->name.empty()) {
                        return std::format("{} (Ext ${:02X})", extension->name, id);
                    }
                    return std::format("Extension ${:02X}", id);
                }
                if (const char* name = nspc::vcmdNameForId(id); name != nullptr) {
                    return name;
                }
                return std::format("${:02X}", id);
            };
            const auto hexView = std::string_view(hexInput_);
            const auto startsWithVirtualPrefix = [&]() {
                return hexInput_.size() >= 2 && parseHexValue(hexView.substr(0, 2)) == 0xFF;
            };

            if (singleFx && fxParamCountForCurrentEngine(singleFx->id).has_value() && singleFx->paramCount > 0) {
                bool overwriteMode = false;
                uint8_t overwriteId = 0;
                size_t overwriteLeadChars = 2;
                std::optional<uint8_t> overwriteParamCount = std::nullopt;
                if (const auto decodedLead = decodeTypedFxLeadForCurrentEngine(hexView); decodedLead.has_value()) {
                    overwriteId = decodedLead->first;
                    overwriteLeadChars = decodedLead->second;
                    overwriteParamCount = fxParamCountForCurrentEngine(overwriteId);
                    overwriteMode = overwriteParamCount.has_value() &&
                                    (overwriteId != singleFx->id || overwriteLeadChars > 2);
                } else if (startsWithVirtualPrefix() && hexInput_.size() >= 4) {
                    overwriteMode = true;
                }

                if (!overwriteMode) {
                    const char* name = nspc::vcmdNameForId(singleFx->id);
                    const size_t totalChars = static_cast<size_t>(singleFx->paramCount) * 2;
                    const size_t remainingNibbles = (hexInput_.size() < totalChars) ? (totalChars - hexInput_.size()) : 0;
                    const size_t remainingBytes = (remainingNibbles + 1) / 2;
                    if (remainingBytes > 0) {
                        ImGui::TextDisabled("FX %s %s_ [%s +%zu bytes]", hex2(singleFx->id).c_str(), fmtHex.c_str(),
                                            name ? name : "invalid", remainingBytes);
                    } else {
                        ImGui::TextDisabled("FX %s %s [%s]", hex2(singleFx->id).c_str(), fmtHex.c_str(),
                                            name ? name : "invalid");
                    }
                } else {
                    const auto overwriteName = labelForFxId(overwriteId);
                    const size_t totalChars = overwriteLeadChars + static_cast<size_t>(overwriteParamCount.value_or(0)) * 2;
                    const size_t remainingNibbles = (hexInput_.size() < totalChars) ? (totalChars - hexInput_.size()) : 0;
                    const size_t remainingBytes = (remainingNibbles + 1) / 2;
                    if (overwriteParamCount.has_value()) {
                        if (remainingBytes > 0) {
                            ImGui::TextDisabled("FX + %s_ [%s +%zu bytes]", fmtHex.c_str(), overwriteName.c_str(),
                                                remainingBytes);
                        } else {
                            ImGui::TextDisabled("FX + %s [%s]", fmtHex.c_str(), overwriteName.c_str());
                        }
                    } else {
                        ImGui::TextDisabled("FX %s_ [invalid]", fmtHex.c_str());
                    }
                }
            } else if (hexInput_.size() >= 2) {
                const auto decodedLead = decodeTypedFxLeadForCurrentEngine(hexView);
                if (decodedLead.has_value()) {
                    const uint8_t vcmdId = decodedLead->first;
                    const auto name = labelForFxId(vcmdId);
                    const auto paramCount = fxParamCountForCurrentEngine(vcmdId);
                    const size_t totalChars = decodedLead->second + static_cast<size_t>(paramCount.value_or(0)) * 2;
                    const size_t remainingNibbles = (hexInput_.size() < totalChars) ? (totalChars - hexInput_.size()) : 0;
                    const size_t remainingBytes = (remainingNibbles + 1) / 2;
                    if (paramCount.has_value()) {
                        if (remainingBytes > 0) {
                            ImGui::TextDisabled("FX %s_ [%s +%zu bytes]", fmtHex.c_str(), name.c_str(), remainingBytes);
                        } else {
                            ImGui::TextDisabled("FX %s [%s]", fmtHex.c_str(), name.c_str());
                        }
                    } else {
                        ImGui::TextDisabled("FX %s_ [invalid]", fmtHex.c_str());
                    }
                } else {
                    if (startsWithVirtualPrefix() && hexInput_.size() < 4) {
                        ImGui::TextDisabled("FX %s_ [FF ext id...]", fmtHex.c_str());
                    } else {
                        ImGui::TextDisabled("FX %s_ [invalid]", fmtHex.c_str());
                    }
                }
            } else {
                ImGui::TextDisabled("FX %s_", fmtHex.c_str());
            }
        } else {
            ImGui::TextDisabled("Hex %s_", hexInput_.c_str());
        }
    }

    ImGui::SameLine(0.0f, 16.0f);
    if (appState_.selectedInstrumentId >= 0) {
        ImGui::TextDisabled("Inst %02X (Ctrl+Shift+,/.)", appState_.selectedInstrumentId & 0xFF);
    } else {
        ImGui::TextDisabled("Inst --");
    }
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::BeginDisabled(songLocked);
    if (ImGui::Button("Remap Inst...")) {
        songInstrumentRemapPopupOpen_ = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::TextDisabled("Header: click mute | Shift+click solo");
    if (songLocked) {
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextDisabled("Read-only (engine song lock)");
    }
    ImGui::PopFont();

    if (rows_.empty()) {
        ImGui::TextDisabled("No pattern data available");
        return;
    }

    if (rowsTruncated_) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "Pattern view truncated to %d rows", kMaxVisibleRows);
    }

    const float tableAreaTop = ImGui::GetCursorScreenPos().y;
    const float tableAreaHeight = ImGui::GetContentRegionAvail().y;

    bool drawPlaybackMarker = false;
    float playbackMarkerXMin = 0.0f;
    float playbackMarkerXMax = 0.0f;
    float playbackMarkerYMin = 0.0f;
    float playbackMarkerYMax = 0.0f;
    float playbackMarkerClipYMin = 0.0f;
    float playbackMarkerClipYMax = 0.0f;

    if (ImGui::BeginTable("Pattern", kChannels + 1, ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersV)) {
        ImGui::PushFont(ntrak::app::App::fonts().mono, 16.0f);
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("Row", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        for (int ch = 0; ch < kChannels; ++ch) {
            ImGui::TableSetupColumn(std::format("Ch {}", ch + 1).c_str(), ImGuiTableColumnFlags_WidthStretch);
        }

        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableSetColumnIndex(0);
        ImGui::TableHeader("Row");
        for (int ch = 0; ch < kChannels; ++ch) {
            ImGui::TableSetColumnIndex(ch + 1);
            const uint8_t channelBit = static_cast<uint8_t>(1u << ch);
            const bool enabled = (appState_.playback.channelMask & channelBit) != 0u;
            const bool soloed = (appState_.playback.channelMask == channelBit);
            std::string headerLabel = std::format("Ch {}", ch + 1);
            if (soloed) {
                headerLabel += " [S]";
            } else if (!enabled) {
                headerLabel += " [M]";
            }

            ImGui::PushID(ch + 1000);
            if (soloed) {
                ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(65, 95, 45, 220));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(80, 125, 55, 230));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(90, 140, 60, 240));
            } else if (!enabled) {
                ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(45, 45, 45, 200));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(60, 60, 60, 220));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(75, 75, 75, 235));
            }
            ImGui::TableHeader(headerLabel.c_str());
            const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly);
            if (hovered) {
                ImGui::SetTooltip("Click: mute/unmute channel\nShift+Click: solo channel");
            }
            if (clicked) {
                handleChannelHeaderClick(ch, ImGui::GetIO().KeyShift);
            }
            if (soloed || !enabled) {
                ImGui::PopStyleColor(3);
            }
            ImGui::PopID();
        }

        const float textLineHeight = ImGui::GetTextLineHeight();
        const float rowTopPadding = 2.0f;
        const float rowBottomPadding = 3.0f;
        const float fxLaneTopPadding = 2.0f;
        const float fxChipHeight = std::max(16.0f, textLineHeight + 4.0f);
        const float compactNoteLaneOffsetY = rowTopPadding;
        const float compactRowHeight = compactNoteLaneOffsetY + textLineHeight + rowBottomPadding;
        const float expandedNoteLaneOffsetY = fxLaneTopPadding + fxChipHeight + 2.0f;
        const float expandedRowHeight = expandedNoteLaneOffsetY + textLineHeight + rowBottomPadding;
        const ImU32 noteTieColor = IM_COL32(165, 230, 165, 255);
        const ImU32 noteRestColor = IM_COL32(170, 190, 210, 255);
        const ImU32 noteIdleColor = IM_COL32(185, 185, 185, 255);

        const size_t activeTicksPerRow = static_cast<size_t>(ticksPerRow_);
        const int renderedRowCount = static_cast<int>((rows_.size() + activeTicksPerRow - 1) / activeTicksPerRow);
        std::vector<uint8_t> visualRowHasFxLane(static_cast<size_t>(renderedRowCount), 0);
        for (int visualRow = 0; visualRow < renderedRowCount; ++visualRow) {
            const size_t rowStartTick = static_cast<size_t>(visualRow) * activeTicksPerRow;
            const size_t rowEndTick = std::min(rowStartTick + activeTicksPerRow, rows_.size());
            bool hasFxLane = false;
            for (size_t tick = rowStartTick; tick < rowEndTick && !hasFxLane; ++tick) {
                for (int ch = 0; ch < kChannels; ++ch) {
                    if (rows_[tick][static_cast<size_t>(ch)].effects.size() > 1) {
                        hasFxLane = true;
                        break;
                    }
                }
            }
            visualRowHasFxLane[static_cast<size_t>(visualRow)] = hasFxLane ? 1u : 0u;
        }

        const int paddingRows = std::max(2, static_cast<int>(std::ceil(tableAreaHeight / compactRowHeight)) + 2);
        const int totalDisplayRows = renderedRowCount + paddingRows * 2;

        int playbackDisplayRow = -1;
        if (trackingPlayback) {
            const int playbackVisualRow = std::clamp(
                static_cast<int>(static_cast<size_t>(playbackTick) / activeTicksPerRow),
                0, renderedRowCount - 1);
            playbackDisplayRow = paddingRows + playbackVisualRow;
        }
        bool selectionScrollApplied = false;

        bool playbackRowScreenYCaptured = false;
        float playbackRowScreenY = 0.0f;

        for (int displayRow = 0; displayRow < totalDisplayRows; ++displayRow) {
            const bool paddingRow = (displayRow < paddingRows) || (displayRow >= paddingRows + renderedRowCount);
            const int visualRow = displayRow - paddingRows;
            const bool rowHasFxLane = (!paddingRow && visualRowHasFxLane[static_cast<size_t>(visualRow)] != 0u);
            const float rowHeight = rowHasFxLane ? expandedRowHeight : compactRowHeight;
            const float noteLaneOffsetY = rowHasFxLane ? expandedNoteLaneOffsetY : compactNoteLaneOffsetY;
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);

            if (!paddingRow) {
                if (visualRow % 4 == 0) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(30, 30, 30, 255));
                } else if (visualRow % 2 == 0) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(20, 20, 20, 255));
                }
            }

            ImGui::TableNextColumn();
            if (paddingRow) {
                ImGui::TextUnformatted(" ");
                for (int ch = 0; ch < kChannels; ++ch) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(" ");
                }
                continue;
            }

            const size_t rowStartTick = static_cast<size_t>(visualRow) * activeTicksPerRow;
            const size_t rowEndTick = std::min(rowStartTick + activeTicksPerRow, rows_.size());

            if (displayRow == playbackDisplayRow) {
                playbackRowScreenY = ImGui::GetCursorScreenPos().y + (compactRowHeight / 2);
                playbackRowScreenYCaptured = true;
            }

            ImGui::TextDisabled("%04X", static_cast<unsigned>(rowStartTick));
            if (pendingScrollToSelection_ && !trackingPlayback && !selectionScrollApplied &&
                static_cast<int>(rowStartTick) == std::max(selectedRow_, 0)) {
                ImGui::SetScrollHereY(0.0f);
                selectionScrollApplied = true;
            }

            for (int ch = 0; ch < kChannels; ++ch) {
                ImGui::TableNextColumn();
                const auto& cell = rows_[rowStartTick][static_cast<size_t>(ch)];
                int hiddenNoteCount = 0;
                int hiddenRestCount = 0;
                int hiddenTieCount = 0;
                int hiddenEffectCount = 0;
                bool hiddenEndMarker = false;
                for (size_t hiddenTick = rowStartTick + 1; hiddenTick < rowEndTick; ++hiddenTick) {
                    const auto& hiddenCell = rows_[hiddenTick][static_cast<size_t>(ch)];
                    if (isTieMarker(hiddenCell.note) || hiddenCell.note == "^^>") {
                        ++hiddenTieCount;
                    } else if (isRestMarker(hiddenCell.note)) {
                        ++hiddenRestCount;
                    } else if (hiddenCell.note != "...") {
                        ++hiddenNoteCount;
                    }
                    hiddenEffectCount += static_cast<int>(hiddenCell.effects.size());
                    if (hiddenCell.hasEndMarker) {
                        hiddenEndMarker = true;
                    }
                }
                const bool hasHiddenData = (hiddenNoteCount > 0) || (hiddenRestCount > 0) || (hiddenEffectCount > 0);
                const bool showEndLine = cell.hasEndMarker || hiddenEndMarker;

                ImGui::PushID(displayRow * kChannels + ch);
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImVec2 cellPos = ImGui::GetCursorScreenPos();
                const float cellWidth = ImGui::GetContentRegionAvail().x;

                // Keep lane alignment stable across all rows; subroutine marker draws in a reserved gutter.
                float laneX = cellPos.x + 7.0f;
                const int rowTick = static_cast<int>(rowStartTick);
                if (cell.hasSubroutineData && cell.subroutineId >= 0) {
                    const ImU32 stripeColor = subroutineColorU32(cell.subroutineId);
                    const float stripeW = 3.0f;
                    const float bracketExtend = 8.0f;

                    // Vertical stripe: extend to row edges for mid-rows, add margin for start/end
                    const float stripeTop = cell.isSubroutineStart ? cellPos.y + 1.0f : cellPos.y;
                    const float stripeBot = cell.isSubroutineEnd ? cellPos.y + rowHeight - 1.0f : cellPos.y + rowHeight;
                    drawList->AddRectFilled(ImVec2(cellPos.x, stripeTop),
                                            ImVec2(cellPos.x + stripeW, stripeBot), stripeColor);

                    // Start bracket: horizontal bar at top
                    if (cell.isSubroutineStart) {
                        drawList->AddRectFilled(ImVec2(cellPos.x, cellPos.y + 1.0f),
                                                ImVec2(cellPos.x + bracketExtend, cellPos.y + 3.0f), stripeColor);
                    }
                    // End bracket: horizontal bar at bottom
                    if (cell.isSubroutineEnd) {
                        drawList->AddRectFilled(ImVec2(cellPos.x, cellPos.y + rowHeight - 3.0f),
                                                ImVec2(cellPos.x + bracketExtend, cellPos.y + rowHeight - 1.0f),
                                                stripeColor);
                    }
                }

                if (rowHasFxLane) {
                    ImGui::SetCursorScreenPos(ImVec2(laneX, cellPos.y + fxLaneTopPadding));
                    if (cell.effects.size() > 1) {
                        float usedWidth = 0.0f;
                        const float maxWidth = std::max(0.0f, cellWidth - (laneX - cellPos.x) - 2.0f);
                        size_t effectIndex = 0;
                        for (; effectIndex < cell.effects.size(); ++effectIndex) {
                            const auto& fx = cell.effects[effectIndex];
                            const float tokenWidth = ImGui::CalcTextSize(fx.label.c_str()).x + 8.0f;
                            const float spacing = (usedWidth > 0.0f) ? 3.0f : 0.0f;
                            if (usedWidth + spacing + tokenWidth > maxWidth) {
                                break;
                            }

                            if (usedWidth > 0.0f) {
                                ImGui::SameLine(0.0f, 3.0f);
                            }
                            const auto style = fxCategoryStyle(fx.category);
                            const ImVec2 chipPos = ImGui::GetCursorScreenPos();
                            const ImVec2 chipSize(tokenWidth, fxChipHeight);
                            drawList->AddRectFilled(chipPos, ImVec2(chipPos.x + chipSize.x, chipPos.y + chipSize.y),
                                                    style.bg, 3.0f);
                            drawList->AddRect(chipPos, ImVec2(chipPos.x + chipSize.x, chipPos.y + chipSize.y),
                                              style.border, 3.0f);
                            drawList->AddText(ImVec2(chipPos.x + 4.0f,
                                                     chipPos.y + (chipSize.y - textLineHeight) * 0.5f),
                                              style.text, fx.label.c_str());
                            ImGui::Dummy(chipSize);
                            const bool fxChipHovered =
                                ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly |
                                                     ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                            const bool fxChipClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                            handleCellSelectionInput(rowTick, ch, 4, fxChipClicked, fxChipHovered);
                            if (fxChipHovered && !mouseSelecting_) {
                                ImGui::SetTooltip("%s", fx.tooltip.c_str());
                                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                    requestFxEditorOpen(static_cast<int>(rowStartTick), ch,
                                                        static_cast<int>(effectIndex));
                                }
                            }
                            usedWidth += spacing + tokenWidth;
                        }

                        if (effectIndex < cell.effects.size()) {
                            const std::string more = std::format("+{}", cell.effects.size() - effectIndex);
                            const float tokenWidth = ImGui::CalcTextSize(more.c_str()).x + 8.0f;
                            if (usedWidth > 0.0f) {
                                ImGui::SameLine(0.0f, 3.0f);
                            }
                            const ImVec2 chipPos = ImGui::GetCursorScreenPos();
                            const ImVec2 chipSize(tokenWidth, fxChipHeight);
                            drawList->AddRectFilled(chipPos, ImVec2(chipPos.x + chipSize.x, chipPos.y + chipSize.y),
                                                    IM_COL32(45, 45, 55, 220), 3.0f);
                            drawList->AddRect(chipPos, ImVec2(chipPos.x + chipSize.x, chipPos.y + chipSize.y),
                                              IM_COL32(100, 100, 120, 220), 3.0f);
                            drawList->AddText(ImVec2(chipPos.x + 4.0f,
                                                     chipPos.y + (chipSize.y - textLineHeight) * 0.5f),
                                              IM_COL32(200, 200, 220, 255), more.c_str());
                            ImGui::Dummy(chipSize);
                            const bool fxMoreHovered =
                                ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly |
                                                     ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                            const bool fxMoreClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                            handleCellSelectionInput(rowTick, ch, 4, fxMoreClicked, fxMoreHovered);
                            if (fxMoreHovered && !mouseSelecting_) {
                                std::string tip;
                                for (size_t hiddenIdx = effectIndex; hiddenIdx < cell.effects.size(); ++hiddenIdx) {
                                    if (!tip.empty()) {
                                        tip += "\n";
                                    }
                                    tip += cell.effects[hiddenIdx].tooltip;
                                }
                                ImGui::SetTooltip("%s", tip.c_str());
                                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                    requestFxEditorOpen(static_cast<int>(rowStartTick), ch);
                                }
                            }
                        }
                    }
                }

                ImGui::SetCursorScreenPos(ImVec2(laneX, cellPos.y + noteLaneOffsetY));
                const bool noteSelected = isCellSelected(rowTick, ch, 0);
                const bool tieMarker = isTieMarker(cell.note) || cell.note == "^^>";
                const bool restMarker = isRestMarker(cell.note);
                ImU32 noteColor = IM_COL32(255, 255, 255, 255);
                if (tieMarker) {
                    noteColor = noteTieColor;
                } else if (restMarker) {
                    noteColor = noteRestColor;
                } else if (cell.note == "...") {
                    noteColor = noteIdleColor;
                }

                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(noteColor));
                const std::string noteLabel = std::format("{}##note_{}_{}", cell.note, visualRow, ch);
                (void)ImGui::Selectable(noteLabel.c_str(), noteSelected, ImGuiSelectableFlags_None,
                                        ImVec2(35.0f, 0.0f));
                const bool noteHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly);
                const bool noteClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                handleCellSelectionInput(rowTick, ch, 0, noteClicked, noteHovered);
                ImGui::PopStyleColor();

                const bool hasInstVolData = (cell.instrument != "..") || (cell.volume != "..") || (cell.qv != "..");
                const bool showInstVol = canShowInstVol(cell.note) || hasInstVolData;
                const std::string instText = showInstVol ? cell.instrument : "..";
                const std::string volText = showInstVol ? cell.volume : "..";
                const std::string qvText = showInstVol ? cell.qv : "..";
                const ImU32 instColor = (showInstVol && cell.instrumentDerived)
                                            ? IM_COL32(220, 200, 120, 100)
                                            : IM_COL32(220, 200, 120, 255);
                const ImU32 volColor = (showInstVol && cell.volumeDerived)
                                           ? IM_COL32(110, 200, 255, 100)
                                           : IM_COL32(110, 200, 255, 255);
                const ImU32 qvColor = (showInstVol && cell.qvDerived)
                                          ? IM_COL32(180, 220, 160, 100)
                                          : IM_COL32(180, 220, 160, 255);

                ImGui::SameLine(0.0f, 4.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(instColor));
                const std::string instLabel = std::format("{}##inst_{}_{}", instText, visualRow, ch);
                (void)ImGui::Selectable(instLabel.c_str(), isCellSelected(rowTick, ch, 1), ImGuiSelectableFlags_None,
                                        ImVec2(20.0f, 0.0f));
                const bool instHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly);
                const bool instClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                handleCellSelectionInput(rowTick, ch, 1, instClicked, instHovered);
                ImGui::PopStyleColor();

                ImGui::SameLine(0.0f, 4.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(volColor));
                const std::string volLabel = std::format("{}##vol_{}_{}", volText, visualRow, ch);
                (void)ImGui::Selectable(volLabel.c_str(), isCellSelected(rowTick, ch, 2), ImGuiSelectableFlags_None,
                                        ImVec2(20.0f, 0.0f));
                const bool volHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly);
                const bool volClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                handleCellSelectionInput(rowTick, ch, 2, volClicked, volHovered);
                ImGui::PopStyleColor();

                ImGui::SameLine(0.0f, 4.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(qvColor));
                const std::string qvLabel = std::format("{}##qv_{}_{}", qvText, visualRow, ch);
                (void)ImGui::Selectable(qvLabel.c_str(), isCellSelected(rowTick, ch, 3), ImGuiSelectableFlags_None,
                                        ImVec2(20.0f, 0.0f));
                const bool qvHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly);
                const bool qvClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                if (qvHovered && !mouseSelecting_ && showInstVol && qvText != "..") {
                    const int qv = parseHexValue(qvText);
                    ImGui::SetTooltip("Duration QV $%02X\nQuantization: %d\nVelocity: %d", qv, (qv >> 4) & 0x07,
                                      qv & 0x0F);
                }
                handleCellSelectionInput(rowTick, ch, 3, qvClicked, qvHovered);
                ImGui::PopStyleColor();

                const bool singleInlineFx = (cell.effects.size() == 1);
                int subCallCount = 0;
                int nonSubFxCount = 0;
                for (const auto& fx : cell.effects) {
                    if (fx.id == nspc::VcmdSubroutineCall::id) {
                        ++subCallCount;
                    } else {
                        ++nonSubFxCount;
                    }
                }

                std::string fxToken = "..";
                if (!cell.effects.empty()) {
                    if (subCallCount > 0 && nonSubFxCount == 0) {
                        if (subCallCount == 1) {
                            const auto& subFx = cell.effects.front();
                            fxToken = std::format("S{}", subFx.subroutineId.value_or(-1));
                        } else {
                            fxToken = std::format("S{}", std::min(subCallCount, 9));
                        }
                    } else if (subCallCount > 0 && nonSubFxCount > 0) {
                        fxToken = std::format("S{}F{}", std::min(subCallCount, 9), std::min(nonSubFxCount, 9));
                    } else if (singleInlineFx) {
                        fxToken = vcmdInlineText(cell.effects.front().id, cell.effects.front().params,
                                                 cell.effects.front().paramCount);
                    } else {
                        fxToken = std::format("F{}", std::min(nonSubFxCount, 9));
                    }
                }
                const float fxWidth = (singleInlineFx && subCallCount == 0)
                                          ? std::max(20.0f, ImGui::CalcTextSize(fxToken.c_str()).x + 10.0f)
                                          : std::max(24.0f, ImGui::CalcTextSize(fxToken.c_str()).x + 8.0f);
                const ImU32 fxColor = IM_COL32(245, 155, 155, 255);
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, fxColor);
                const std::string fxLabel = std::format("{}##fx_{}_{}", fxToken, visualRow, ch);
                (void)ImGui::Selectable(fxLabel.c_str(), isCellSelected(rowTick, ch, 4), ImGuiSelectableFlags_None,
                                        ImVec2(fxWidth, 0.0f));
                const bool fxHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly |
                                                            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                const bool fxTooltipHovered = !mouseSelecting_ && ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly);
                const bool fxClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                if (fxTooltipHovered) {
                    if (!cell.effects.empty()) {
                        std::string subCallTip;
                        std::string otherFxTip;
                        for (const auto& fx : cell.effects) {
                            if (fx.id == nspc::VcmdSubroutineCall::id) {
                                if (!subCallTip.empty()) {
                                    subCallTip += "\n";
                                }
                                subCallTip += fx.tooltip;
                            } else {
                                if (!otherFxTip.empty()) {
                                    otherFxTip += "\n";
                                }
                                otherFxTip += fx.tooltip;
                            }
                        }
                        std::string tip;
                        if (!subCallTip.empty()) {
                            tip += "Subroutine Calls:\n";
                            tip += subCallTip;
                        }
                        if (!otherFxTip.empty()) {
                            if (!tip.empty()) {
                                tip += "\n\n";
                            }
                            tip += "Effects:\n";
                            tip += otherFxTip;
                        }
                        if (singleInlineFx && subCallCount == 0) {
                            const auto& fx = cell.effects.front();
                            if (isEditableFxId(fx.id) && fx.paramCount > 0) {
                                tip += "\n\nType params to edit inline.\nType a new FX ID to add another effect.";
                            }
                        }
                        ImGui::SetTooltip("%s", tip.c_str());
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            requestFxEditorOpen(static_cast<int>(rowStartTick), ch);
                        }
                    } else {
                        ImGui::SetTooltip(
                            "FX: type vcmd ID + params (e.g. E1 80)\n"
                            "Extension commands also accept virtual prefix form (e.g. FF FB 01)\n"
                            "Delete clears all effects");
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            requestFxEditorOpen(static_cast<int>(rowStartTick), ch);
                        }
                    }
                }
                handleCellSelectionInput(rowTick, ch, 4, fxClicked, fxHovered);
                ImGui::PopStyleColor();

                if (hasHiddenData) {
                    ImGui::SameLine(0.0f, 4.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 175, 80, 255));
                    ImGui::TextUnformatted("*");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Hidden ticks %04X-%04X\nNotes: %d\nRests: %d\nEffects: %d\nTies: %d",
                                          static_cast<unsigned>(rowStartTick + 1),
                                          static_cast<unsigned>(rowEndTick - 1), hiddenNoteCount, hiddenRestCount,
                                          hiddenEffectCount, hiddenTieCount);
                    }
                    ImGui::PopStyleColor();
                }

                // End marker: red horizontal line at row bottom
                if (showEndLine) {
                    const float lineY = cellPos.y + rowHeight - 1.0f;
                    drawList->AddLine(ImVec2(cellPos.x, lineY), ImVec2(cellPos.x + cellWidth, lineY),
                                      IM_COL32(220, 50, 50, 200), 2.0f);
                }

                ImGui::SetCursorScreenPos(ImVec2(cellPos.x, cellPos.y + rowHeight - 1.0f));
                ImGui::Dummy(ImVec2(1.0f, 1.0f));
                ImGui::PopID();
            }
        }
        if (selectionScrollApplied) {
            pendingScrollToSelection_ = false;
        }

        const float indicatorCenterY = tableAreaTop + tableAreaHeight * 0.5f + compactRowHeight * 0.5f;

        if (trackingPlayback && appState_.playback.autoScroll && playbackRowScreenYCaptured) {
            const float rowCenterY = playbackRowScreenY;
            const float error = rowCenterY - indicatorCenterY;
            const float currentScroll = ImGui::GetScrollY();
            const float maxScroll = ImGui::GetScrollMaxY();
            const float dt = ImGui::GetIO().DeltaTime;
            float newScroll;
            if (std::abs(error) > tableAreaHeight * 0.4f) {
                newScroll = currentScroll + error;
            } else {
                const float t = 1.0f - std::exp(-12.0f * dt);
                newScroll = currentScroll + error * t;
            }
            ImGui::SetScrollY(std::clamp(newScroll, 0.0f, maxScroll));
        }

        if (trackingPlayback) {
            const float markerHeight = compactRowHeight;
            const float xMin = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x;
            const float xMax = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            drawPlaybackMarker = true;
            playbackMarkerXMin = xMin;
            playbackMarkerXMax = xMax;
            playbackMarkerYMin = indicatorCenterY - markerHeight * 0.5f;
            playbackMarkerYMax = indicatorCenterY + markerHeight * 0.5f;
            playbackMarkerClipYMin = tableAreaTop;
            playbackMarkerClipYMax = tableAreaTop + tableAreaHeight;
        }

        // Detect right-click inside the table for context menu (deferred to parent scope)
        bool contextMenuRequested = !songLocked && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
                                    ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

        ImGui::PopFont();
        ImGui::EndTable();

        // Open context menu in the parent window scope so BeginPopup can find it
        if (contextMenuRequested) {
            ImGui::OpenPopup("PatternContextMenu");
        }
    }

    // Don't draw playback marker on top of modal dialogs
    const bool anyModalOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (drawPlaybackMarker && !anyModalOpen) {
        ImDrawList* overlay = ImGui::GetForegroundDrawList();
        overlay->PushClipRect(ImVec2(playbackMarkerXMin, playbackMarkerClipYMin),
                              ImVec2(playbackMarkerXMax, playbackMarkerClipYMax), true);
        overlay->AddRectFilled(ImVec2(playbackMarkerXMin, playbackMarkerYMin),
                               ImVec2(playbackMarkerXMax, playbackMarkerYMax), IM_COL32(110, 225, 130, 70));
        overlay->AddRect(ImVec2(playbackMarkerXMin, playbackMarkerYMin),
                         ImVec2(playbackMarkerXMax, playbackMarkerYMax), IM_COL32(130, 240, 150, 145));
        overlay->PopClipRect();
    }

    if (!songLocked) {
        drawFxEditorPopup(song, *patternId);
        drawSetInstrumentPopup(song, *patternId);
        drawSetVolumePopup(song, *patternId);
        drawPatternLengthPopup(song, *patternId);
        drawSongInstrumentRemapPopup(song, *patternId);
        drawContextMenu(song, *patternId);
    }
}

}  // namespace ntrak::ui
