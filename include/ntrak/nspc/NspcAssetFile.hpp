#pragma once

#include "ntrak/nspc/NspcData.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace ntrak::nspc {

struct NtiFileData {
    NspcInstrument instrument;
    BrrSample sample;
    uint32_t loopOffsetBytes = 0;
    bool loopEnabled = false;
};

std::expected<void, std::string> validateBrrData(std::span<const uint8_t> brrData);

std::expected<std::vector<uint8_t>, std::string> loadBrrFile(const std::filesystem::path& path);

std::expected<void, std::string> saveBrrFile(const std::filesystem::path& path, std::span<const uint8_t> brrData);

std::expected<NtiFileData, std::string> loadNtiFile(const std::filesystem::path& path);

std::expected<void, std::string> saveNtiFile(const std::filesystem::path& path, const NspcInstrument& instrument,
                                             const BrrSample& sample);

}  // namespace ntrak::nspc
