#pragma once

#include "ntrak/audio/SpcPlayer.hpp"
#include "ntrak/nspc/NspcCommandHistory.hpp"
#include "ntrak/nspc/NspcOptimize.hpp"
#include "ntrak/nspc/NspcProject.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace ntrak::app {

struct PlaybackTrackingState {
    std::atomic<bool> hooksInstalled{false};
    std::atomic<bool> awaitingFirstPatternTrigger{false};
    std::atomic<bool> pendingStopAtEnd{false};
    std::atomic<uint64_t> eventSerial{0};
    std::atomic<uint64_t> engineTickEvents{0};
    std::atomic<int> sequenceRow{-1};
    std::atomic<int> patternId{-1};
    std::atomic<int> patternTick{-1};
    uint8_t channelMask = 0xFF;  // bit N = channel enabled (1), 0 = muted
    bool followPlayback = true;
    bool autoScroll = true;
};

struct AppState {
    std::optional<nspc::NspcProject> project;
    std::vector<std::uint8_t> sourceSpcData;
    std::optional<std::filesystem::path> sourceSpcPath;
    bool flattenSubroutinesOnLoad = false;
    bool optimizeSubroutinesOnBuild = false;
    bool compactAramLayoutOnBuild = true;
    bool lockEngineContent = true;
    nspc::NspcOptimizerOptions optimizerOptions{
        .maxOptimizeIterations = 64,
        .topCandidatesFromSam = 1024,
        .maxCandidateBytes = 1536,
        .singleIterationCallPenaltyBytes = 8,
        .allowSingleIterationCalls = false,
    };
    std::unique_ptr<audio::SpcPlayer> spcPlayer;
    int selectedSongIndex = 0;
    int selectedSequenceRow = -1;
    int selectedSequenceChannel = 0;
    std::optional<int> selectedPatternId;
    int selectedInstrumentId = -1;
    int trackerInputOctave = 4;
    PlaybackTrackingState playback;

    // Undo/redo system
    nspc::NspcCommandHistory commandHistory;

    // Playback callbacks (wired by ControlPanel, callable from any panel)
    std::function<bool()> playSong;
    std::function<bool()> playFromPattern;
    std::function<void()> stopPlayback;
    std::function<bool()> isPlaying;

    // Edit callbacks (wired by UiManager and PatternEditorPanel)
    std::function<void()> undo;
    std::function<void()> redo;
    std::function<void()> cut;
    std::function<void()> copy;
    std::function<void()> paste;
};

}  // namespace ntrak::app
