#include "ntrak/ui/SequenceEditorPanel.hpp"

#include "ntrak/app/App.hpp"
#include "ntrak/nspc/NspcEditor.hpp"
#include "ntrak/nspc/NspcFlatten.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>

namespace ntrak::ui {

namespace {
constexpr uint32_t kDefaultNewPatternEndTick = 127;

uint64_t mixHash(uint64_t hash, uint64_t value) {
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    hash ^= value;
    hash *= kFnvPrime;
    return hash;
}

uint64_t hashSequenceOp(const nspc::NspcSequenceOp& op) {
    return std::visit(
        nspc::overloaded{
            [](const nspc::PlayPattern& value) -> uint64_t {
                uint64_t hash = 1469598103934665603ULL;
                hash = mixHash(hash, 1);
                hash = mixHash(hash, static_cast<uint64_t>(static_cast<uint32_t>(value.patternId)));
                hash = mixHash(hash, value.trackTableAddr);
                return hash;
            },
            [](const nspc::JumpTimes& value) -> uint64_t {
                uint64_t hash = 1469598103934665603ULL;
                hash = mixHash(hash, 2);
                hash = mixHash(hash, value.count);
                hash = mixHash(hash, value.target.index.has_value() ? 1 : 0);
                hash = mixHash(hash, static_cast<uint64_t>(value.target.index.value_or(-1) + 1));
                hash = mixHash(hash, value.target.addr);
                return hash;
            },
            [](const nspc::AlwaysJump& value) -> uint64_t {
                uint64_t hash = 1469598103934665603ULL;
                hash = mixHash(hash, 3);
                hash = mixHash(hash, value.opcode);
                hash = mixHash(hash, value.target.index.has_value() ? 1 : 0);
                hash = mixHash(hash, static_cast<uint64_t>(value.target.index.value_or(-1) + 1));
                hash = mixHash(hash, value.target.addr);
                return hash;
            },
            [](const nspc::FastForwardOn&) -> uint64_t { return 0xFF01ULL; },
            [](const nspc::FastForwardOff&) -> uint64_t { return 0xFF02ULL; },
            [](const nspc::EndSequence&) -> uint64_t { return 0xFF03ULL; },
        },
        op);
}

uint64_t sequenceEditorSongFingerprint(const nspc::NspcSong& song) {
    uint64_t hash = 1469598103934665603ULL;
    hash = mixHash(hash, static_cast<uint64_t>(song.songId() + 1));

    for (const auto& op : song.sequence()) {
        hash = mixHash(hash, hashSequenceOp(op));
    }
    hash = mixHash(hash, 0xABCD0001ULL);

    for (const auto& pattern : song.patterns()) {
        hash = mixHash(hash, static_cast<uint64_t>(static_cast<uint32_t>(pattern.id)));
        hash = mixHash(hash, pattern.trackTableAddr);
        if (pattern.channelTrackIds.has_value()) {
            hash = mixHash(hash, 1);
            for (const int trackId : *pattern.channelTrackIds) {
                hash = mixHash(hash, static_cast<uint64_t>(trackId + 1));
            }
        } else {
            hash = mixHash(hash, 0);
        }
    }
    return hash;
}

std::optional<uint32_t> templatePatternEndTick(const nspc::NspcSong& song, std::optional<int> patternId) {
    if (!patternId.has_value() || *patternId < 0) {
        return std::nullopt;
    }
    const auto flat = nspc::flattenPatternById(song, *patternId);
    if (!flat.has_value()) {
        return std::nullopt;
    }
    return flat->totalTicks;
}

}  // namespace

SequenceEditorPanel::SequenceEditorPanel(app::AppState& appState) : appState_(appState) {}

nspc::NspcPattern* SequenceEditorPanel::findPattern(nspc::NspcSong& song, int patternId) {
    auto& patterns = song.patterns();
    auto it = std::find_if(patterns.begin(), patterns.end(),
                           [patternId](const nspc::NspcPattern& pattern) { return pattern.id == patternId; });
    if (it == patterns.end()) {
        return nullptr;
    }
    return &(*it);
}

const nspc::NspcPattern* SequenceEditorPanel::findPattern(const nspc::NspcSong& song, int patternId) const {
    const auto& patterns = song.patterns();
    auto it = std::find_if(patterns.begin(), patterns.end(),
                           [patternId](const nspc::NspcPattern& pattern) { return pattern.id == patternId; });
    if (it == patterns.end()) {
        return nullptr;
    }
    return &(*it);
}

std::vector<int> SequenceEditorPanel::collectPatternIds(const nspc::NspcSong& song) const {
    std::vector<int> ids;
    ids.reserve(song.patterns().size());
    for (const auto& pattern : song.patterns()) {
        ids.push_back(pattern.id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

int SequenceEditorPanel::allocatePatternId(const nspc::NspcSong& song) const {
    int maxId = -1;
    for (const auto& pattern : song.patterns()) {
        maxId = std::max(maxId, pattern.id);
    }
    return maxId + 1;
}

int SequenceEditorPanel::allocateTrackId(const nspc::NspcSong& song) const {
    int maxId = -1;
    for (const auto& track : song.tracks()) {
        maxId = std::max(maxId, track.id);
    }
    return maxId + 1;
}

nspc::NspcPattern& SequenceEditorPanel::ensurePatternExists(nspc::NspcSong& song, int patternId) {
    if (auto* existing = findPattern(song, patternId)) {
        return *existing;
    }

    const uint32_t desiredEndTick =
        templatePatternEndTick(song, appState_.selectedPatternId).value_or(kDefaultNewPatternEndTick);

    auto& patterns = song.patterns();
    patterns.push_back(nspc::NspcPattern{
        .id = patternId,
        .channelTrackIds = std::array<int, kChannels>{-1, -1, -1, -1, -1, -1, -1, -1},
        .trackTableAddr = 0,
    });
    nspc::NspcEditor editor;
    (void)editor.setPatternLength(song, patternId, desiredEndTick);
    return patterns.back();
}

int SequenceEditorPanel::duplicatePattern(nspc::NspcSong& song, int sourcePatternId) {
    const auto* sourcePattern = findPattern(song, sourcePatternId);
    if (!sourcePattern) {
        return -1;
    }

    const int newPatternId = allocatePatternId(song);
    std::array<int, kChannels> newChannelTrackIds{-1, -1, -1, -1, -1, -1, -1, -1};
    std::unordered_map<int, int> clonedTrackIds;
    int nextTrackId = allocateTrackId(song);
    nspc::NspcEventId nextEventId = song.peekNextEventId();

    if (sourcePattern->channelTrackIds.has_value()) {
        for (int channel = 0; channel < kChannels; ++channel) {
            const int sourceTrackId = sourcePattern->channelTrackIds.value()[static_cast<size_t>(channel)];
            if (sourceTrackId < 0) {
                continue;
            }

            const auto existingClone = clonedTrackIds.find(sourceTrackId);
            if (existingClone != clonedTrackIds.end()) {
                newChannelTrackIds[static_cast<size_t>(channel)] = existingClone->second;
                continue;
            }

            const auto sourceTrackIt =
                std::find_if(song.tracks().begin(), song.tracks().end(),
                             [sourceTrackId](const nspc::NspcTrack& track) { return track.id == sourceTrackId; });
            if (sourceTrackIt == song.tracks().end()) {
                continue;
            }

            nspc::NspcTrack clonedTrack{};
            clonedTrack.id = nextTrackId++;
            clonedTrack.originalAddr = 0;
            clonedTrack.events = sourceTrackIt->events;
            for (auto& event : clonedTrack.events) {
                event.id = nextEventId++;
                event.originalAddr.reset();
            }

            song.tracks().push_back(std::move(clonedTrack));
            clonedTrackIds[sourceTrackId] = song.tracks().back().id;
            newChannelTrackIds[static_cast<size_t>(channel)] = song.tracks().back().id;
        }
    }

    song.setNextEventId(nextEventId);
    song.patterns().push_back(nspc::NspcPattern{
        .id = newPatternId,
        .channelTrackIds = newChannelTrackIds,
        .trackTableAddr = 0,
    });
    song.setContentOrigin(nspc::NspcContentOrigin::UserProvided);
    return newPatternId;
}

int SequenceEditorPanel::createNewPattern(nspc::NspcSong& song) {
    const int newPatternId = allocatePatternId(song);
    std::array<int, kChannels> newChannelTrackIds{-1, -1, -1, -1, -1, -1, -1, -1};
    int nextTrackId = allocateTrackId(song);

    for (int channel = 0; channel < kChannels; ++channel) {
        const int trackId = nextTrackId++;
        newChannelTrackIds[static_cast<size_t>(channel)] = trackId;
        song.tracks().push_back(nspc::NspcTrack{
            .id = trackId,
            .events = {},
            .originalAddr = 0,
        });
    }

    song.patterns().push_back(nspc::NspcPattern{
        .id = newPatternId,
        .channelTrackIds = newChannelTrackIds,
        .trackTableAddr = 0,
    });

    const uint32_t desiredEndTick =
        templatePatternEndTick(song, appState_.selectedPatternId).value_or(kDefaultNewPatternEndTick);
    nspc::NspcEditor editor;
    (void)editor.setPatternLength(song, newPatternId, desiredEndTick);
    song.setContentOrigin(nspc::NspcContentOrigin::UserProvided);
    return newPatternId;
}

void SequenceEditorPanel::insertPlayPatternOp(nspc::NspcSong& song, int patternId) {
    auto& pattern = ensurePatternExists(song, patternId);
    auto& sequence = song.sequence();
    const int insertIndex = std::clamp(defaultInsertIndex(song), 0, static_cast<int>(sequence.size()));
    sequence.insert(sequence.begin() + insertIndex, nspc::PlayPattern{
                                                    .patternId = pattern.id,
                                                    .trackTableAddr = pattern.trackTableAddr,
                                                });
    selectedRow() = insertIndex;
    appState_.selectedPatternId = pattern.id;
    song.setContentOrigin(nspc::NspcContentOrigin::UserProvided);
    syncSelectedPatternFromRow(song);
}

std::string SequenceEditorPanel::describeSequenceOp(const nspc::NspcSequenceOp& op) const {
    return std::visit(nspc::overloaded{
                          [](const nspc::PlayPattern&) { return std::string{}; },
                          [](const nspc::JumpTimes& jump) {
                              if (jump.target.index.has_value()) {
                                  return std::format("Jx{:02X} -> {:02X}", jump.count, *jump.target.index);
                              }
                              return std::format("Jx{:02X} -> ${:04X}", jump.count, jump.target.addr);
                          },
                          [](const nspc::AlwaysJump& jump) {
                              if (jump.target.index.has_value()) {
                                  return std::format("AJ {:02X} -> {:02X}", jump.opcode, *jump.target.index);
                              }
                              return std::format("AJ {:02X} -> ${:04X}", jump.opcode, jump.target.addr);
                          },
                          [](const nspc::FastForwardOn&) { return std::string{"FF On"}; },
                          [](const nspc::FastForwardOff&) { return std::string{"FF Off"}; },
                          [](const nspc::EndSequence&) { return std::string{"End"}; },
                      },
                      op);
}

int SequenceEditorPanel::defaultInsertIndex(const nspc::NspcSong& song) const {
    if (selectedRow() >= 0 && selectedRow() < static_cast<int>(song.sequence().size())) {
        return selectedRow() + 1;
    }

    const auto& sequence = song.sequence();
    for (size_t i = 0; i < sequence.size(); ++i) {
        if (std::holds_alternative<nspc::EndSequence>(sequence[i])) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(sequence.size());
}

std::optional<nspc::PlayPattern> SequenceEditorPanel::selectedPlayPattern(const nspc::NspcSong& song) const {
    const auto& sequence = song.sequence();
    if (selectedRow() < 0 || selectedRow() >= static_cast<int>(sequence.size())) {
        return std::nullopt;
    }

    if (const auto* play = std::get_if<nspc::PlayPattern>(&sequence[static_cast<size_t>(selectedRow())])) {
        return *play;
    }
    return std::nullopt;
}

void SequenceEditorPanel::syncSelectedPatternFromRow(const nspc::NspcSong& song) {
    if (const auto selectedPlay = selectedPlayPattern(song); selectedPlay.has_value()) {
        appState_.selectedPatternId = selectedPlay->patternId;
    } else {
        appState_.selectedPatternId.reset();
    }
}

const char* SequenceEditorPanel::insertOpTypeLabel(InsertOpType type) const {
    switch (type) {
    case InsertOpType::PlayPattern:
        return "PlayPattern";
    case InsertOpType::JumpTimes:
        return "JumpTimes";
    case InsertOpType::AlwaysJump:
        return "AlwaysJump";
    case InsertOpType::FastForwardOn:
        return "FastForwardOn";
    case InsertOpType::FastForwardOff:
        return "FastForwardOff";
    case InsertOpType::EndSequence:
        return "EndSequence";
    }
    return "PlayPattern";
}

nspc::NspcSequenceOp SequenceEditorPanel::buildInsertOperation(nspc::NspcSong& song) {
    switch (insertType_) {
    case InsertOpType::PlayPattern: {
        if (insertPatternId_ < 0) {
            insertPatternId_ = allocatePatternId(song);
        }
        auto& pattern = ensurePatternExists(song, insertPatternId_);
        return nspc::PlayPattern{
            .patternId = pattern.id,
            .trackTableAddr = pattern.trackTableAddr,
        };
    }
    case InsertOpType::JumpTimes: {
        const uint8_t count = static_cast<uint8_t>(std::clamp(insertJumpCount_, 1, 0x7F));
        const int target = std::max(0, insertJumpTarget_);
        return nspc::JumpTimes{
            .count = count,
            .target = nspc::SequenceTarget{.index = target, .addr = 0},
        };
    }
    case InsertOpType::AlwaysJump: {
        const uint8_t opcode = static_cast<uint8_t>(std::clamp(insertAlwaysOpcode_, 0x82, 0xFF));
        const int target = std::max(0, insertJumpTarget_);
        return nspc::AlwaysJump{
            .opcode = opcode,
            .target = nspc::SequenceTarget{.index = target, .addr = 0},
        };
    }
    case InsertOpType::FastForwardOn:
        return nspc::FastForwardOn{};
    case InsertOpType::FastForwardOff:
        return nspc::FastForwardOff{};
    case InsertOpType::EndSequence:
        return nspc::EndSequence{};
    }
    return nspc::EndSequence{};
}

bool SequenceEditorPanel::appendTypedHexNibble() {
    struct KeyNibble {
        ImGuiKey key;
        char hex;
    };

    static constexpr std::array<KeyNibble, 26> kMap = {{
        {ImGuiKey_0, '0'},       {ImGuiKey_1, '1'},       {ImGuiKey_2, '2'},       {ImGuiKey_3, '3'},
        {ImGuiKey_4, '4'},       {ImGuiKey_5, '5'},       {ImGuiKey_6, '6'},       {ImGuiKey_7, '7'},
        {ImGuiKey_8, '8'},       {ImGuiKey_9, '9'},       {ImGuiKey_A, 'A'},       {ImGuiKey_B, 'B'},
        {ImGuiKey_C, 'C'},       {ImGuiKey_D, 'D'},       {ImGuiKey_E, 'E'},       {ImGuiKey_F, 'F'},
        {ImGuiKey_Keypad0, '0'}, {ImGuiKey_Keypad1, '1'}, {ImGuiKey_Keypad2, '2'}, {ImGuiKey_Keypad3, '3'},
        {ImGuiKey_Keypad4, '4'}, {ImGuiKey_Keypad5, '5'}, {ImGuiKey_Keypad6, '6'}, {ImGuiKey_Keypad7, '7'},
        {ImGuiKey_Keypad8, '8'}, {ImGuiKey_Keypad9, '9'},
    }};

    for (const auto& keyNibble : kMap) {
        if (ImGui::IsKeyPressed(keyNibble.key)) {
            hexInput_.push_back(keyNibble.hex);
            return true;
        }
    }
    return false;
}

void SequenceEditorPanel::applyHexEdit(nspc::NspcSong& song, int value) {
    auto& sequence = song.sequence();
    if (selectedRow() < 0 || selectedRow() >= static_cast<int>(sequence.size())) {
        return;
    }

    auto& op = sequence[static_cast<size_t>(selectedRow())];
    switch (gridEditField_) {
    case GridEditField::Pattern: {
        auto* play = std::get_if<nspc::PlayPattern>(&op);
        if (!play) {
            return;
        }
        auto& pattern = ensurePatternExists(song, value);
        play->patternId = pattern.id;
        play->trackTableAddr = pattern.trackTableAddr;
        appState_.selectedPatternId = pattern.id;
        insertPatternId_ = pattern.id;
        return;
    }
    case GridEditField::Track: {
        auto* play = std::get_if<nspc::PlayPattern>(&op);
        if (!play) {
            return;
        }
        auto* pattern = findPattern(song, play->patternId);
        if (!pattern) {
            pattern = &ensurePatternExists(song, play->patternId);
        }
        if (!pattern->channelTrackIds.has_value()) {
            pattern->channelTrackIds = std::array<int, kChannels>{-1, -1, -1, -1, -1, -1, -1, -1};
        }
        if (selectedChannel() < 0 || selectedChannel() >= kChannels) {
            return;
        }
        pattern->channelTrackIds.value()[static_cast<size_t>(selectedChannel())] = std::clamp(value, 0, 0xFF);
        return;
    }
    case GridEditField::JumpCount: {
        auto* jump = std::get_if<nspc::JumpTimes>(&op);
        if (!jump) {
            return;
        }
        jump->count = static_cast<uint8_t>(std::clamp(value, 1, 0x7F));
        return;
    }
    case GridEditField::JumpTarget: {
        auto* jump = std::get_if<nspc::JumpTimes>(&op);
        if (!jump) {
            return;
        }
        jump->target.index = std::clamp(value, 0, 0xFF);
        jump->target.addr = 0;
        return;
    }
    case GridEditField::AlwaysOpcode: {
        auto* always = std::get_if<nspc::AlwaysJump>(&op);
        if (!always) {
            return;
        }
        always->opcode = static_cast<uint8_t>(std::clamp(value, 0x82, 0xFF));
        return;
    }
    case GridEditField::AlwaysTarget: {
        auto* always = std::get_if<nspc::AlwaysJump>(&op);
        if (!always) {
            return;
        }
        always->target.index = std::clamp(value, 0, 0xFF);
        always->target.addr = 0;
        return;
    }
    case GridEditField::None:
        return;
    }
}

void SequenceEditorPanel::handleInlineHexEditing(nspc::NspcSong& song, bool songLocked) {
    if (songLocked) {
        return;
    }
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        return;
    }
    if (gridEditField_ == GridEditField::None) {
        return;
    }
    if (ImGui::IsAnyItemActive()) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        hexInput_.clear();
        gridEditField_ = GridEditField::None;
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !hexInput_.empty()) {
        hexInput_.pop_back();
        return;
    }

    if ((ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) &&
        gridEditField_ == GridEditField::Track && hexInput_.empty()) {
        auto& sequence = song.sequence();
        if (selectedRow() < 0 || selectedRow() >= static_cast<int>(sequence.size())) {
            return;
        }
        auto* play = std::get_if<nspc::PlayPattern>(&sequence[static_cast<size_t>(selectedRow())]);
        if (!play) {
            return;
        }
        auto* pattern = findPattern(song, play->patternId);
        if (!pattern) {
            pattern = &ensurePatternExists(song, play->patternId);
        }
        if (!pattern->channelTrackIds.has_value()) {
            pattern->channelTrackIds = std::array<int, kChannels>{-1, -1, -1, -1, -1, -1, -1, -1};
        }
        if (selectedChannel() >= 0 && selectedChannel() < kChannels) {
            pattern->channelTrackIds.value()[static_cast<size_t>(selectedChannel())] = -1;
        }
        return;
    }

    if (!appendTypedHexNibble()) {
        return;
    }

    if (hexInput_.size() < 2) {
        return;
    }

    int value = 0;
    for (const char c : hexInput_) {
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= (c - '0');
        } else if (c >= 'A' && c <= 'F') {
            value |= (c - 'A' + 10);
        }
    }
    applyHexEdit(song, value);
    hexInput_.clear();
}

void SequenceEditorPanel::drawHeader(nspc::NspcSong& song, bool songLocked) {
    auto& sequence = song.sequence();
    if (sequence.empty()) {
        sequence.push_back(nspc::EndSequence{});
    }

    auto patternIds = collectPatternIds(song);
    if (insertPatternId_ < 0) {
        insertPatternId_ = patternIds.empty() ? allocatePatternId(song) : patternIds.front();
    }

    ImGui::PushFont(ntrak::app::App::fonts().mono, 13.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::BeginDisabled(songLocked);
    constexpr float kButtonWidthSmall = 40.0f;
    constexpr float kButtonWidthMedium = 48.0f;
    ImGui::SetNextItemWidth(132.0f);
    if (ImGui::BeginCombo("##InsertType", insertOpTypeLabel(insertType_))) {
        constexpr std::array<InsertOpType, 6> kTypes = {
            InsertOpType::PlayPattern,   InsertOpType::JumpTimes,      InsertOpType::AlwaysJump,
            InsertOpType::FastForwardOn, InsertOpType::FastForwardOff, InsertOpType::EndSequence,
        };
        for (const auto type : kTypes) {
            const bool selected = (type == insertType_);
            if (ImGui::Selectable(insertOpTypeLabel(type), selected)) {
                insertType_ = type;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (insertType_ == InsertOpType::PlayPattern) {
        std::string insertLabel = std::format("P {:02X}", std::clamp(insertPatternId_, 0, 0xFF));
        ImGui::SetNextItemWidth(84.0f);
        if (ImGui::BeginCombo("##InsertPattern", insertLabel.c_str())) {
            for (const int patternId : patternIds) {
                const bool selected = (patternId == insertPatternId_);
                std::string label = std::format("Pattern {}", patternId);
                if (ImGui::Selectable(label.c_str(), selected)) {
                    insertPatternId_ = patternId;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::Button("Duplicate", ImVec2(86.0f, 0.0f))) {
            const int sourcePatternId = std::clamp(insertPatternId_, 0, 0xFFFF);
            const int duplicatedPatternId = duplicatePattern(song, sourcePatternId);
            if (duplicatedPatternId >= 0) {
                insertPatternId_ = duplicatedPatternId;
                appState_.selectedPatternId = duplicatedPatternId;
                insertPlayPatternOp(song, duplicatedPatternId);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("New", ImVec2(kButtonWidthMedium + 8.0f, 0.0f))) {
            const int newPatternId = createNewPattern(song);
            insertPatternId_ = newPatternId;
            appState_.selectedPatternId = newPatternId;
            insertPlayPatternOp(song, newPatternId);
        }
    } else if (insertType_ == InsertOpType::JumpTimes) {
        ImGui::TextUnformatted("x");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(46.0f);
        if (ImGui::InputInt("##InsertJumpCount", &insertJumpCount_, 1, 4)) {
            insertJumpCount_ = std::clamp(insertJumpCount_, 1, 0x7F);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("->");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(52.0f);
        if (ImGui::InputInt("##InsertJumpTarget", &insertJumpTarget_, 1, 8)) {
            insertJumpTarget_ = std::max(0, insertJumpTarget_);
        }
    } else if (insertType_ == InsertOpType::AlwaysJump) {
        ImGui::TextUnformatted("Op");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(52.0f);
        if (ImGui::InputInt("##InsertAlwaysOpcode", &insertAlwaysOpcode_, 1, 8)) {
            insertAlwaysOpcode_ = std::clamp(insertAlwaysOpcode_, 0x82, 0xFF);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("->");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(52.0f);
        if (ImGui::InputInt("##InsertAlwaysTarget", &insertJumpTarget_, 1, 8)) {
            insertJumpTarget_ = std::max(0, insertJumpTarget_);
        }
    } else {
        ImGui::TextDisabled("-");
    }

    ImGui::SameLine();
    if (ImGui::Button("Ins", ImVec2(kButtonWidthMedium, 0.0f))) {
        if (insertType_ == InsertOpType::PlayPattern) {
            insertPlayPatternOp(song, std::clamp(insertPatternId_, 0, 0xFFFF));
        } else {
            const nspc::NspcSequenceOp newOp = buildInsertOperation(song);
            const int insertIndex = std::clamp(defaultInsertIndex(song), 0, static_cast<int>(sequence.size()));
            sequence.insert(sequence.begin() + insertIndex, newOp);
            selectedRow() = insertIndex;
            song.setContentOrigin(nspc::NspcContentOrigin::UserProvided);
            syncSelectedPatternFromRow(song);
        }
    }

    const bool hasSelection = selectedRow() >= 0 && selectedRow() < static_cast<int>(sequence.size());
    const bool selectedIsEnd = hasSelection &&
                               std::holds_alternative<nspc::EndSequence>(sequence[static_cast<size_t>(selectedRow())]);

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasSelection || selectedIsEnd);
    if (ImGui::Button("Del", ImVec2(kButtonWidthMedium, 0.0f))) {
        if (hasSelection) {
            sequence.erase(sequence.begin() + selectedRow());
            if (sequence.empty()) {
                selectedRow() = -1;
            } else {
                selectedRow() = std::min(selectedRow(), static_cast<int>(sequence.size()) - 1);
            }
            syncSelectedPatternFromRow(song);
        }
    }
    ImGui::EndDisabled();

    const bool canMoveUp = hasSelection && !selectedIsEnd && selectedRow() > 0;
    ImGui::SameLine();
    ImGui::BeginDisabled(!canMoveUp);
    if (ImGui::Button("Up", ImVec2(kButtonWidthSmall, 0.0f))) {
        if (canMoveUp) {
            std::swap(sequence[static_cast<size_t>(selectedRow())], sequence[static_cast<size_t>(selectedRow() - 1)]);
            --selectedRow();
        }
    }
    ImGui::EndDisabled();

    const bool canMoveDown =
        hasSelection && !selectedIsEnd && selectedRow() + 1 < static_cast<int>(sequence.size()) &&
        !std::holds_alternative<nspc::EndSequence>(sequence[static_cast<size_t>(selectedRow() + 1)]);
    ImGui::SameLine();
    ImGui::BeginDisabled(!canMoveDown);
    if (ImGui::Button("Dn", ImVec2(kButtonWidthSmall, 0.0f))) {
        if (canMoveDown) {
            std::swap(sequence[static_cast<size_t>(selectedRow())], sequence[static_cast<size_t>(selectedRow() + 1)]);
            ++selectedRow();
        }
    }
    ImGui::EndDisabled();

    if (!hexInput_.empty()) {
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextDisabled("Input: %s_", hexInput_.c_str());
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar(2);
    ImGui::PopFont();

    syncSelectedPatternFromRow(song);
}

void SequenceEditorPanel::drawSequenceTable(nspc::NspcSong& song, bool songLocked) {
    auto& sequence = song.sequence();
    const bool followPlayback = appState_.playback.followPlayback &&
                                appState_.playback.hooksInstalled.load(std::memory_order_relaxed);
    const int playbackSequenceRow = appState_.playback.sequenceRow.load(std::memory_order_relaxed);
    const bool hasPlaybackRow = followPlayback && playbackSequenceRow >= 0 &&
                                playbackSequenceRow < static_cast<int>(sequence.size());
    if (!hasPlaybackRow) {
        lastPlaybackScrollRow_ = -1;
    }

    if (ImGui::BeginTable("SequenceTable", kChannels + 3,
                          ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg)) {
        ImGui::PushFont(ntrak::app::App::fonts().mono, 16.0f);
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("S", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("P", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        for (int ch = 0; ch < kChannels; ++ch) {
            ImGui::TableSetupColumn(std::format("T{}", ch + 1).c_str(), ImGuiTableColumnFlags_WidthFixed, 46.0f);
        }
        ImGui::TableSetupColumn("Cmd", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int row = 0; row < static_cast<int>(sequence.size()); ++row) {
            ImGui::TableNextRow();
            const auto& op = sequence[static_cast<size_t>(row)];
            const bool rowSelected = (row == selectedRow());
            const bool playbackRow = hasPlaybackRow && (row == playbackSequenceRow);
            if (playbackRow) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(52, 64, 44, 255));
            } else if (rowSelected) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(50, 50, 70, 255));
            }

            ImGui::PushID(row);

            ImGui::TableNextColumn();
            const std::string seqStr = std::format("{:02X}", row);
            const std::string seqLabel = std::format("{}##seq_{}", seqStr, row);
            const bool seqSelected = rowSelected && gridEditField_ == GridEditField::None;
            if (playbackRow) {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 255, 180, 255));
            }
            if (ImGui::Selectable(seqLabel.c_str(), seqSelected)) {
                selectedRow() = row;
                gridEditField_ = GridEditField::None;
                hexInput_.clear();
                syncSelectedPatternFromRow(song);
            }
            if (playbackRow) {
                ImGui::PopStyleColor();
            }
            if (playbackRow && appState_.playback.autoScroll && lastPlaybackScrollRow_ != row) {
                ImGui::SetScrollHereY(0.35f);
                lastPlaybackScrollRow_ = row;
            }

            auto* play = std::get_if<nspc::PlayPattern>(&sequence[static_cast<size_t>(row)]);
            nspc::NspcPattern* pattern = nullptr;
            if (play) {
                pattern = findPattern(song, play->patternId);
            }

            ImGui::TableNextColumn();
            if (play) {
                const std::string patStr = std::format("{:02X}", play->patternId);
                const std::string patLabel = std::format("{}##pat_{}", patStr, row);
                const bool patternSelected = rowSelected && gridEditField_ == GridEditField::Pattern;
                if (ImGui::Selectable(patLabel.c_str(), patternSelected)) {
                    selectedRow() = row;
                    gridEditField_ = GridEditField::Pattern;
                    hexInput_.clear();
                    appState_.selectedPatternId = play->patternId;
                }
            } else {
                ImGui::TextDisabled("--");
            }

            for (int ch = 0; ch < kChannels; ++ch) {
                ImGui::TableNextColumn();

                if (!play || !pattern || !pattern->channelTrackIds.has_value()) {
                    ImGui::TextDisabled("--");
                    continue;
                }

                const int trackId = pattern->channelTrackIds.value()[static_cast<size_t>(ch)];
                const std::string trackStr = (trackId >= 0) ? std::format("{:02X}", trackId) : std::string{"--"};
                const std::string trackLabel = std::format("{}##trk_{}_{}", trackStr, row, ch);
                const bool cellSelected = rowSelected && (selectedChannel() == ch) &&
                                          gridEditField_ == GridEditField::Track;
                if (ImGui::Selectable(trackLabel.c_str(), cellSelected)) {
                    selectedRow() = row;
                    selectedChannel() = ch;
                    gridEditField_ = GridEditField::Track;
                    hexInput_.clear();
                    if (play) {
                        appState_.selectedPatternId = play->patternId;
                    }
                }
            }

            ImGui::TableNextColumn();
            auto classify_op = [](const nspc::NspcSequenceOp& value) -> InsertOpType {
                return std::visit(nspc::overloaded{
                                      [](const nspc::PlayPattern&) { return InsertOpType::PlayPattern; },
                                      [](const nspc::JumpTimes&) { return InsertOpType::JumpTimes; },
                                      [](const nspc::AlwaysJump&) { return InsertOpType::AlwaysJump; },
                                      [](const nspc::FastForwardOn&) { return InsertOpType::FastForwardOn; },
                                      [](const nspc::FastForwardOff&) { return InsertOpType::FastForwardOff; },
                                      [](const nspc::EndSequence&) { return InsertOpType::EndSequence; },
                                  },
                                  value);
            };

            auto op_type_short_label = [](InsertOpType type) -> const char* {
                switch (type) {
                case InsertOpType::PlayPattern:
                    return "Play";
                case InsertOpType::JumpTimes:
                    return "JmpN";
                case InsertOpType::AlwaysJump:
                    return "AJump";
                case InsertOpType::FastForwardOn:
                    return "FF+";
                case InsertOpType::FastForwardOff:
                    return "FF-";
                case InsertOpType::EndSequence:
                    return "End";
                }
                return "Play";
            };

            auto replace_row_type = [&](InsertOpType type) {
                auto& targetOp = sequence[static_cast<size_t>(row)];
                switch (type) {
                case InsertOpType::PlayPattern: {
                    int patternId = appState_.selectedPatternId.value_or(insertPatternId_);
                    if (patternId < 0) {
                        patternId = allocatePatternId(song);
                    }
                    auto& pattern = ensurePatternExists(song, patternId);
                    targetOp = nspc::PlayPattern{
                        .patternId = pattern.id,
                        .trackTableAddr = pattern.trackTableAddr,
                    };
                    appState_.selectedPatternId = pattern.id;
                    break;
                }
                case InsertOpType::JumpTimes:
                    targetOp = nspc::JumpTimes{
                        .count = 1,
                        .target = nspc::SequenceTarget{.index = 0, .addr = 0},
                    };
                    break;
                case InsertOpType::AlwaysJump:
                    targetOp = nspc::AlwaysJump{
                        .opcode = static_cast<uint8_t>(std::clamp(insertAlwaysOpcode_, 0x82, 0xFF)),
                        .target = nspc::SequenceTarget{.index = 0, .addr = 0},
                    };
                    break;
                case InsertOpType::FastForwardOn:
                    targetOp = nspc::FastForwardOn{};
                    break;
                case InsertOpType::FastForwardOff:
                    targetOp = nspc::FastForwardOff{};
                    break;
                case InsertOpType::EndSequence:
                    targetOp = nspc::EndSequence{};
                    break;
                }
                gridEditField_ = GridEditField::None;
                hexInput_.clear();
                syncSelectedPatternFromRow(song);
            };

            if (!rowSelected) {
                const std::string cmd = describeSequenceOp(op);
                if (cmd.empty()) {
                    ImGui::TextUnformatted("");
                } else {
                    ImGui::TextDisabled("%s", cmd.c_str());
                }
                ImGui::PopID();
                continue;
            }

            const InsertOpType currentType = classify_op(sequence[static_cast<size_t>(row)]);
            ImGui::SetNextItemWidth(78.0f);
            const std::string opTypeId = std::format("##op_type_{}", row);
            ImGui::BeginDisabled(songLocked);
            if (ImGui::BeginCombo(opTypeId.c_str(), op_type_short_label(currentType))) {
                constexpr std::array<InsertOpType, 6> kTypes = {
                    InsertOpType::PlayPattern,   InsertOpType::JumpTimes,      InsertOpType::AlwaysJump,
                    InsertOpType::FastForwardOn, InsertOpType::FastForwardOff, InsertOpType::EndSequence,
                };
                for (const auto type : kTypes) {
                    const bool selected = (type == currentType);
                    if (ImGui::Selectable(insertOpTypeLabel(type), selected) && !selected) {
                        replace_row_type(type);
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();

            auto& editableOp = sequence[static_cast<size_t>(row)];
            if (auto* jump = std::get_if<nspc::JumpTimes>(&editableOp)) {
                ImGui::SameLine(0.0f, 4.0f);
                const bool countSelected = (gridEditField_ == GridEditField::JumpCount);
                const std::string countLabel = std::format("x{:02X}##jmp_count_{}", jump->count, row);
                if (ImGui::Selectable(countLabel.c_str(), countSelected, ImGuiSelectableFlags_None,
                                      ImVec2(38.0f, 0.0f))) {
                    selectedRow() = row;
                    gridEditField_ = GridEditField::JumpCount;
                    hexInput_.clear();
                }

                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextDisabled("->");
                ImGui::SameLine(0.0f, 4.0f);
                const int target = jump->target.index.value_or(static_cast<int>(jump->target.addr & 0xFF));
                const bool targetSelected = (gridEditField_ == GridEditField::JumpTarget);
                const std::string targetLabel = std::format("{:02X}##jmp_target_{}", std::clamp(target, 0, 0xFF), row);
                if (ImGui::Selectable(targetLabel.c_str(), targetSelected, ImGuiSelectableFlags_None,
                                      ImVec2(30.0f, 0.0f))) {
                    selectedRow() = row;
                    gridEditField_ = GridEditField::JumpTarget;
                    hexInput_.clear();
                }
            } else if (auto* always = std::get_if<nspc::AlwaysJump>(&editableOp)) {
                ImGui::SameLine(0.0f, 4.0f);
                const bool opcodeSelected = (gridEditField_ == GridEditField::AlwaysOpcode);
                const std::string opcodeLabel = std::format("o{:02X}##aj_op_{}", always->opcode, row);
                if (ImGui::Selectable(opcodeLabel.c_str(), opcodeSelected, ImGuiSelectableFlags_None,
                                      ImVec2(38.0f, 0.0f))) {
                    selectedRow() = row;
                    gridEditField_ = GridEditField::AlwaysOpcode;
                    hexInput_.clear();
                }

                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextDisabled("->");
                ImGui::SameLine(0.0f, 4.0f);
                const int target = always->target.index.value_or(static_cast<int>(always->target.addr & 0xFF));
                const bool targetSelected = (gridEditField_ == GridEditField::AlwaysTarget);
                const std::string targetLabel = std::format("{:02X}##aj_target_{}", std::clamp(target, 0, 0xFF), row);
                if (ImGui::Selectable(targetLabel.c_str(), targetSelected, ImGuiSelectableFlags_None,
                                      ImVec2(30.0f, 0.0f))) {
                    selectedRow() = row;
                    gridEditField_ = GridEditField::AlwaysTarget;
                    hexInput_.clear();
                }
            } else if (std::holds_alternative<nspc::PlayPattern>(editableOp)) {
                ImGui::SameLine(0.0f, 6.0f);
                ImGui::TextDisabled("P/T");
            } else {
                ImGui::SameLine(0.0f, 6.0f);
                ImGui::TextDisabled("-");
            }

            ImGui::PopID();
        }

        ImGui::PopFont();
        ImGui::EndTable();
    }
}

void SequenceEditorPanel::draw() {
    if (!appState_.project.has_value()) {
        ImGui::TextDisabled("No project loaded");
        ImGui::TextDisabled("Import an SPC to edit sequence data");
        return;
    }

    auto& project = appState_.project.value();
    auto& songs = project.songs();
    if (songs.empty()) {
        ImGui::TextDisabled("Project has no songs");
        return;
    }

    if (appState_.selectedSongIndex < 0 || appState_.selectedSongIndex >= static_cast<int>(songs.size())) {
        ImGui::TextDisabled("Selected song index is out of range");
        return;
    }

    auto& song = songs[static_cast<size_t>(appState_.selectedSongIndex)];
    const bool songLocked = appState_.lockEngineContent && song.isEngineProvided();
    if (song.sequence().empty()) {
        song.sequence().push_back(nspc::EndSequence{});
        selectedRow() = 0;
    }

    selectedChannel() = std::clamp(selectedChannel(), 0, kChannels - 1);
    if (selectedRow() >= static_cast<int>(song.sequence().size())) {
        selectedRow() = static_cast<int>(song.sequence().size()) - 1;
    }

    syncSelectedPatternFromRow(song);
    if (!appState_.playback.followPlayback || !appState_.playback.hooksInstalled.load(std::memory_order_relaxed)) {
        lastPlaybackScrollRow_ = -1;
    }
    const uint64_t songFingerprintBefore = sequenceEditorSongFingerprint(song);

    if (songLocked) {
        ImGui::TextDisabled("Selected song is engine-owned and locked from edits.");
    }
    drawHeader(song, songLocked);
    drawSequenceTable(song, songLocked);
    handleInlineHexEditing(song, songLocked);
    if (sequenceEditorSongFingerprint(song) != songFingerprintBefore) {
        song.setContentOrigin(nspc::NspcContentOrigin::UserProvided);
    }
}

}  // namespace ntrak::ui
