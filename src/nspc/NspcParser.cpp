#include "ntrak/nspc/NspcParser.hpp"

#include "ntrak/nspc/NspcEngine.hpp"

#include <algorithm>
#include <cstring>
#include <expected>
#include <optional>

namespace ntrak::nspc {

/// @brief Loads an SPC file from the given data buffer. The data should be the entire contents of an SPC file (usually
/// 64KB).
/// @param  data The data buffer containing the SPC file contents.
/// @return An expected containing the loaded NspcProject on success, or an NspcParseError on failure.
std::expected<NspcProject, NspcParseError> NspcParser::load(std::span<const std::uint8_t> data) {
    auto engineConfigs = ntrak::nspc::loadEngineConfigs();

    if (!engineConfigs) {
        return std::unexpected(NspcParseError::InvalidConfig);
    }

    if (data.size() < 0x100) {
        return std::unexpected(NspcParseError::UnexpectedEndOfData);
    }

    if (std::memcmp(data.data(), "SNES-SPC700 Sound File Data", 27) != 0) {
        return std::unexpected(NspcParseError::InvalidHeader);
    }

    if (data.size() < 0x100 + 0x10000) {
        return std::unexpected(NspcParseError::UnexpectedEndOfData);
    }

    // Parse SPC file ARAM into a local array that we'll later move to the project.
    std::array<std::uint8_t, 0x10000> aram{};
    std::copy_n(data.begin() + 0x100, aram.size(), aram.begin());

    // Copy the source SPC data to a local variable so we can modify it before sending it to the project.
    std::vector<std::uint8_t> spcData(data.begin(), data.end());

    std::optional<NspcEngineConfig> matchingConfig;

    // Compare the ARAM data against engine configuration to find a matching engine.
    for (const auto& config : *engineConfigs) {
        const std::size_t offset = config.entryPoint;
        const std::size_t length = config.engineBytes.size();

        if (offset + length > aram.size()) {
            continue;
        }

        if (std::equal(config.engineBytes.begin(), config.engineBytes.end(),
                       std::next(aram.begin(), static_cast<std::ptrdiff_t>(offset)))) {
            matchingConfig = resolveEngineConfigPointers(config,
                                                         std::span<const std::uint8_t>(aram.data(), aram.size()));
            break;
        }
    }

    if (!matchingConfig) {
        return std::unexpected(NspcParseError::UnsupportedVersion);
    }

    // Addmusick tweak: override SCR/ESA from SPC header DSP registers if they differ.
    constexpr size_t kSpcDspRegOffset = 0x10100;
    constexpr uint8_t kDirReg = 0x5D;
    constexpr uint8_t kEsaReg = 0x6D;

    if (matchingConfig->engineVariant == "addmusick" && data.size() >= kSpcDspRegOffset + 128) {
        const uint8_t dspDir = data[kSpcDspRegOffset + kDirReg];
        const uint8_t dspEsa = data[kSpcDspRegOffset + kEsaReg];
        const uint16_t spcSampleHeaders = static_cast<uint16_t>(dspDir) << 8U;
        const uint16_t spcEchoBuffer = static_cast<uint16_t>(dspEsa) << 8U;

        if (spcSampleHeaders != 0 && spcSampleHeaders != matchingConfig->sampleHeaders) {
            matchingConfig->sampleHeaders = spcSampleHeaders;
        }
        if (spcEchoBuffer != matchingConfig->echoBuffer) {
            matchingConfig->echoBuffer = spcEchoBuffer;
        }

        if (matchingConfig->defaultDspTablePtr.has_value()) {
            uint16_t defaultDspTable = aram[matchingConfig->defaultDspTablePtr.value()];
            defaultDspTable |= aram[matchingConfig->defaultDspTablePtr.value() + 1] << 8U;

            aram[defaultDspTable + 9] = dspDir;
            aram[defaultDspTable + 10] = dspEsa;
            spcData[defaultDspTable + 0x100 + 9] = dspDir;
            spcData[defaultDspTable + 0x100 + 10] = dspEsa;
        }
    }

    NspcProject project(std::move(*matchingConfig), aram);
    project.setSourceSpcData(std::vector<std::uint8_t>(spcData.begin(), spcData.end()));
    return project;
}
}  // namespace ntrak::nspc
