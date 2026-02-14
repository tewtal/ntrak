#pragma once

#include "ntrak/nspc/NspcProject.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ntrak::nspc {

struct NspcProjectIrData {
    std::string engineName;
    std::optional<std::filesystem::path> baseSpcPath;
    std::optional<std::vector<std::string>> enabledEngineExtensions;
    std::vector<NspcSong> songs;
    std::vector<NspcInstrument> instruments;
    std::vector<BrrSample> samples;
    std::vector<int> retainedEngineSongIds;
    std::vector<int> retainedEngineInstrumentIds;
    std::vector<int> retainedEngineSampleIds;
};

std::expected<void, std::string> saveProjectIrFile(const NspcProject& project, const std::filesystem::path& path,
                                                   std::optional<std::filesystem::path> baseSpcPath = std::nullopt);

std::expected<NspcProjectIrData, std::string> loadProjectIrFile(const std::filesystem::path& path);

std::expected<void, std::string> applyProjectIrOverlay(NspcProject& project, const NspcProjectIrData& overlay);

}  // namespace ntrak::nspc
