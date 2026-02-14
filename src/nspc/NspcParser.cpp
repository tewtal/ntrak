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

    return NspcProject(std::move(*matchingConfig), std::move(aram));
}
}  // namespace ntrak::nspc
