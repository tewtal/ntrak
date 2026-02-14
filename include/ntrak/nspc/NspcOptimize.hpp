// ntrak/nspc/NspcOptimize.hpp
#pragma once

#include "ntrak/nspc/NspcData.hpp"

namespace ntrak::nspc {

struct NspcOptimizerOptions {
    int maxOptimizeIterations = 128;
    int topCandidatesFromSam = 2048;
    uint32_t maxCandidateBytes = 2048;
    int singleIterationCallPenaltyBytes = 4;
    bool allowSingleIterationCalls = true;
};

// Greedy suffix-automaton-based subroutine extraction.
// Assumes/forces flattened tracks first, then creates a fresh set of subroutines.
void optimizeSongSubroutines(NspcSong& song, const NspcOptimizerOptions& options = {});

}  // namespace ntrak::nspc
