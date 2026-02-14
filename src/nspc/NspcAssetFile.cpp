#include "ntrak/nspc/NspcAssetFile.hpp"

#include "ntrak/nspc/Base64.hpp"
#include "ntrak/nspc/BrrCodec.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using json = nlohmann::json;

namespace ntrak::nspc {
namespace {

constexpr std::string_view kNtiFormatTag = "ntrak_instrument";
constexpr int kNtiFormatVersion = 1;

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

std::expected<json, std::string> readJsonFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(std::format("Failed to open '{}'", path.string()));
    }

    try {
        json root;
        file >> root;
        return root;
    } catch (const std::exception& ex) {
        return std::unexpected(std::format("Failed to parse '{}': {}", path.string(), ex.what()));
    }
}

std::expected<void, std::string> writeJsonFile(const std::filesystem::path& path, const json& root) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return std::unexpected(std::format("Failed to open '{}' for writing", path.string()));
    }

    file << root.dump(2);
    if (!file.good()) {
        return std::unexpected(std::format("Failed while writing '{}'", path.string()));
    }
    return {};
}

std::optional<uint8_t> parseU8(const json& value) {
    if (value.is_number_unsigned()) {
        const auto raw = value.get<uint64_t>();
        if (raw > 0xFFu) {
            return std::nullopt;
        }
        return static_cast<uint8_t>(raw);
    }
    if (value.is_number_integer()) {
        const auto raw = value.get<int64_t>();
        if (raw < 0 || raw > 0xFF) {
            return std::nullopt;
        }
        return static_cast<uint8_t>(raw);
    }
    return std::nullopt;
}

std::optional<uint32_t> parseU32(const json& value) {
    if (value.is_number_unsigned()) {
        const auto raw = value.get<uint64_t>();
        if (raw > std::numeric_limits<uint32_t>::max()) {
            return std::nullopt;
        }
        return static_cast<uint32_t>(raw);
    }
    if (value.is_number_integer()) {
        const auto raw = value.get<int64_t>();
        if (raw < 0 || raw > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
            return std::nullopt;
        }
        return static_cast<uint32_t>(raw);
    }
    return std::nullopt;
}

std::optional<int> parseInt(const json& value) {
    if (value.is_number_integer()) {
        return value.get<int>();
    }
    if (value.is_number_unsigned()) {
        const auto raw = value.get<uint64_t>();
        if (raw > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return std::nullopt;
        }
        return static_cast<int>(raw);
    }
    return std::nullopt;
}

bool fileSampleLoopEnabled(const BrrSample& sample) {
    if (sample.data.size() < 9u) {
        return false;
    }
    const uint8_t endHeader = sample.data[sample.data.size() - 9u];
    return (endHeader & 0x02u) != 0u;
}

}  // namespace

std::expected<void, std::string> validateBrrData(std::span<const uint8_t> brrData) {
    if (brrData.empty()) {
        return std::unexpected("BRR data is empty");
    }
    if ((brrData.size() % 9u) != 0u) {
        return std::unexpected("BRR data size must be a multiple of 9 bytes");
    }

    for (size_t blockOffset = 0; blockOffset < brrData.size(); blockOffset += 9u) {
        const uint8_t header = brrData[blockOffset];
        const uint8_t range = static_cast<uint8_t>((header >> 4u) & 0x0Fu);
        if (range > 0x0Cu) {
            return std::unexpected(std::format("Invalid BRR range nibble {:X} in block {}", range, blockOffset / 9u));
        }

        const bool isLastBlock = (blockOffset + 9u) == brrData.size();
        const bool hasEndFlag = (header & 0x01u) != 0u;
        if (!isLastBlock && hasEndFlag) {
            return std::unexpected("BRR END flag appears before final block");
        }
        if (isLastBlock && !hasEndFlag) {
            return std::unexpected("Final BRR block is missing END flag");
        }
    }

    auto decoded = decodeBrrToPcm(brrData);
    if (!decoded.has_value()) {
        return std::unexpected(decoded.error());
    }

    return {};
}

std::expected<std::vector<uint8_t>, std::string> loadBrrFile(const std::filesystem::path& path) {
    auto bytes = readBinaryFile(path);
    if (!bytes.has_value()) {
        return std::unexpected(bytes.error());
    }

    auto validation = validateBrrData(*bytes);
    if (!validation.has_value()) {
        return std::unexpected(std::format("Invalid BRR file '{}': {}", path.string(), validation.error()));
    }

    return *bytes;
}

std::expected<void, std::string> saveBrrFile(const std::filesystem::path& path, std::span<const uint8_t> brrData) {
    auto validation = validateBrrData(brrData);
    if (!validation.has_value()) {
        return std::unexpected(validation.error());
    }
    return writeBinaryFile(path, brrData);
}

std::expected<NtiFileData, std::string> loadNtiFile(const std::filesystem::path& path) {
    auto rootResult = readJsonFile(path);
    if (!rootResult.has_value()) {
        return std::unexpected(rootResult.error());
    }
    const json& root = *rootResult;

    if (!root.is_object()) {
        return std::unexpected("NTI file root must be an object");
    }
    if (root.value("format", "") != kNtiFormatTag) {
        return std::unexpected("NTI file has an unsupported format tag");
    }
    if (root.value("formatVersion", 0) != kNtiFormatVersion) {
        return std::unexpected(std::format("Unsupported NTI format version {}", root.value("formatVersion", 0)));
    }
    if (!root.contains("instrument") || !root["instrument"].is_object()) {
        return std::unexpected("NTI file is missing instrument payload");
    }
    if (!root.contains("sample") || !root["sample"].is_object()) {
        return std::unexpected("NTI file is missing sample payload");
    }

    const json& instrumentNode = root["instrument"];
    const json& sampleNode = root["sample"];

    const auto adsr1 = parseU8(instrumentNode.value("adsr1", -1));
    const auto adsr2 = parseU8(instrumentNode.value("adsr2", -1));
    const auto gain = parseU8(instrumentNode.value("gain", -1));
    const auto basePitchMult = parseU8(instrumentNode.value("basePitchMult", -1));
    const auto fracPitchMult = parseU8(instrumentNode.value("fracPitchMult", -1));
    if (!adsr1.has_value() || !adsr2.has_value() || !gain.has_value() || !basePitchMult.has_value() ||
        !fracPitchMult.has_value()) {
        return std::unexpected("NTI instrument payload is missing one or more required byte fields");
    }

    const auto sampleIndex = parseU8(instrumentNode.value("sampleIndex", 0));
    if (!sampleIndex.has_value()) {
        return std::unexpected("NTI instrument payload has invalid sampleIndex");
    }

    const std::string dataEncoding = sampleNode.value("dataEncoding", "");
    if (dataEncoding != "base64") {
        return std::unexpected(std::format("NTI sample data encoding '{}' is not supported", dataEncoding));
    }
    if (!sampleNode.contains("data") || !sampleNode["data"].is_string()) {
        return std::unexpected("NTI sample payload is missing base64 data");
    }

    auto sampleBytes = decodeBase64(sampleNode["data"].get<std::string>());
    if (!sampleBytes.has_value()) {
        return std::unexpected(std::format("NTI sample base64 decode failed: {}", sampleBytes.error()));
    }

    auto brrValidation = validateBrrData(*sampleBytes);
    if (!brrValidation.has_value()) {
        return std::unexpected(std::format("NTI sample BRR payload is invalid: {}", brrValidation.error()));
    }

    const bool loopEnabled = sampleNode.value("loopEnabled", false);
    const auto loopOffsetBytes = parseU32(sampleNode.value("loopOffsetBytes", 0));
    if (!loopOffsetBytes.has_value()) {
        return std::unexpected("NTI sample payload has invalid loopOffsetBytes");
    }

    uint32_t loopOffset = loopEnabled ? *loopOffsetBytes : 0u;
    if (loopEnabled) {
        if (loopOffset >= sampleBytes->size()) {
            return std::unexpected("NTI sample loop offset is outside the BRR payload");
        }
        if ((loopOffset % 9u) != 0u) {
            return std::unexpected("NTI sample loop offset must be aligned to BRR blocks");
        }
    }

    NtiFileData result{};
    result.instrument.id = parseInt(instrumentNode.value("id", -1)).value_or(-1);
    result.instrument.name = instrumentNode.value("name", "");
    result.instrument.sampleIndex = *sampleIndex;
    result.instrument.adsr1 = *adsr1;
    result.instrument.adsr2 = *adsr2;
    result.instrument.gain = *gain;
    result.instrument.basePitchMult = *basePitchMult;
    result.instrument.fracPitchMult = *fracPitchMult;
    result.instrument.originalAddr = 0;
    result.instrument.contentOrigin = NspcContentOrigin::UserProvided;

    result.sample.id = parseInt(sampleNode.value("id", -1)).value_or(-1);
    result.sample.name = sampleNode.value("name", "");
    result.sample.data = std::move(*sampleBytes);
    result.sample.originalAddr = 0;
    result.sample.originalLoopAddr = 0;
    result.sample.contentOrigin = NspcContentOrigin::UserProvided;

    result.loopEnabled = loopEnabled;
    result.loopOffsetBytes = loopOffset;
    return result;
}

std::expected<void, std::string> saveNtiFile(const std::filesystem::path& path, const NspcInstrument& instrument,
                                             const BrrSample& sample) {
    auto brrValidation = validateBrrData(sample.data);
    if (!brrValidation.has_value()) {
        return std::unexpected(std::format("Cannot write NTI with invalid sample BRR payload: {}", brrValidation.error()));
    }

    const bool loopEnabled = fileSampleLoopEnabled(sample);
    uint32_t loopOffsetBytes = 0;
    if (loopEnabled) {
        if (sample.originalLoopAddr < sample.originalAddr) {
            return std::unexpected("Cannot write NTI: looping sample has loop address before sample start");
        }
        loopOffsetBytes = static_cast<uint32_t>(sample.originalLoopAddr - sample.originalAddr);
        if (loopOffsetBytes >= sample.data.size() || (loopOffsetBytes % 9u) != 0u) {
            return std::unexpected("Cannot write NTI: looping sample has invalid loop offset");
        }
    }

    json root{
        {"format", kNtiFormatTag},
        {"formatVersion", kNtiFormatVersion},
        {"instrument",
         {
             {"id", instrument.id},
             {"name", instrument.name},
             {"sampleIndex", instrument.sampleIndex},
             {"adsr1", instrument.adsr1},
             {"adsr2", instrument.adsr2},
             {"gain", instrument.gain},
             {"basePitchMult", instrument.basePitchMult},
             {"fracPitchMult", instrument.fracPitchMult},
         }},
        {"sample",
         {
             {"id", sample.id},
             {"name", sample.name},
             {"loopEnabled", loopEnabled},
             {"loopOffsetBytes", loopOffsetBytes},
             {"dataEncoding", "base64"},
             {"data", encodeBase64(sample.data)},
         }},
    };

    return writeJsonFile(path, root);
}

}  // namespace ntrak::nspc
