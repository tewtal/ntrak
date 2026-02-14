#pragma once

#include "ntrak/nspc/NspcProject.hpp"
#include "ntrak/nspc/NspcOptimize.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace ntrak::nspc {

struct NspcUploadChunk {
    uint16_t address = 0;
    std::vector<uint8_t> bytes;
    std::string label;
};

struct NspcUploadList {
    std::vector<NspcUploadChunk> chunks;
};

struct NspcCompileOutput {
    NspcUploadList upload;
    std::vector<std::string> warnings;
};

struct NspcBuildOptions {
    bool optimizeSubroutines = true;
    NspcOptimizerOptions optimizerOptions{};
    bool applyOptimizedSongToProject = false;
    bool includeEngineExtensions = true;
    bool compactAramLayout = true;
};

struct NspcRoundTripReport {
    bool equivalent = false;
    size_t objectsCompared = 0;
    size_t bytesCompared = 0;
    size_t differingBytes = 0;
    size_t pointerDifferencesIgnored = 0;
    std::vector<std::string> messages;
};

std::expected<NspcCompileOutput, std::string> buildSongScopedUpload(
    NspcProject& project, int songIndex, NspcBuildOptions options = {});

std::expected<NspcUploadList, std::string> buildUserContentUpload(NspcProject& project, NspcBuildOptions options = {});

std::expected<std::vector<uint8_t>, std::string> buildUserContentNspcExport(
    NspcProject& project, NspcBuildOptions options = {});

std::expected<NspcRoundTripReport, std::string> verifySongRoundTrip(const NspcProject& project, int songIndex);

/// Debug/helper API: encode an event stream with the same rules used by song compilation.
/// The supplied subroutine address map is used for VcmdSubroutineCall patching.
std::expected<std::vector<uint8_t>, std::string> encodeEventStreamForEngine(
    const std::vector<NspcEventEntry>& events, const std::unordered_map<int, uint16_t>& subroutineAddrById,
    std::vector<std::string>& warnings, const NspcEngineConfig& engine);

std::expected<std::vector<uint8_t>, std::string> applyUploadToSpcImage(const NspcUploadList& upload,
                                                                       std::span<const uint8_t> baseSpcFile);

}  // namespace ntrak::nspc
