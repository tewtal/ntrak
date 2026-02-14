#pragma once

#include "ntrak/nspc/NspcProject.hpp"

#include <expected>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ntrak::nspc {

struct ItImportResult {
    int targetSongIndex = -1;
    int importedInstrumentCount = 0;
    int importedSampleCount = 0;
    int importedPatternCount = 0;
    int importedTrackCount = 0;
    std::vector<std::string> enabledExtensions;
    std::vector<std::string> warnings;
};

struct ItSampleResampleOption {
    int sampleIndex = -1;       // 0-based IT sample index
    double resampleRatio = 1.0; // output_sample_count / input_sample_count
};

struct ItImportOptions {
    double globalResampleRatio = 1.0;  // Applied to every sample before per-sample overrides
    bool highQualityResampling = true;
    bool enhanceTrebleOnEncode = true;
    std::vector<ItSampleResampleOption> sampleResampleOptions;
    std::vector<int> instrumentsToDelete;
    std::vector<int> samplesToDelete;
};

struct ItImportSamplePreview {
    int sampleIndex = -1;
    std::string name;
    bool looped = false;
    uint32_t sourcePcmSampleCount = 0;
    uint32_t estimatedPcmSampleCount = 0;
    uint32_t estimatedBrrBytes = 0;
    double effectiveResampleRatio = 1.0;
};

struct ItImportPreview {
    std::string moduleName;
    int orderCount = 0;
    int referencedPatternCount = 0;
    int importedPatternCount = 0;
    int importedTrackCount = 0;
    int importedInstrumentCount = 0;
    int importedSampleCount = 0;
    uint32_t currentFreeAramBytes = 0;
    uint32_t freeAramAfterDeletionBytes = 0;
    uint32_t estimatedRequiredSampleBytes = 0;
    std::vector<ItImportSamplePreview> samples;
    std::vector<std::string> warnings;
};

std::expected<ItImportPreview, std::string>
analyzeItFileForSongSlot(const NspcProject& baseProject, const std::filesystem::path& itPath, int targetSongIndex,
                         const ItImportOptions& options = {});

std::expected<std::pair<NspcProject, ItImportResult>, std::string>
importItFileIntoSongSlot(const NspcProject& baseProject, const std::filesystem::path& itPath, int targetSongIndex,
                         const ItImportOptions& options = {});

}  // namespace ntrak::nspc
