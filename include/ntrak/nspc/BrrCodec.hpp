#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ntrak::nspc {

struct BrrEncodeOptions {
    bool enableLoop = false;
    std::optional<size_t> loopStartSample;
    bool enhanceTreble = false; // Compensates SNES Gaussian interpolation low-pass.
};

struct BrrEncodeResult {
    std::vector<uint8_t> bytes;
    uint32_t loopOffsetBytes = 0;
};

std::expected<BrrEncodeResult, std::string> encodePcm16ToBrr(std::span<const int16_t> monoPcm,
                                                             const BrrEncodeOptions& options = {});

std::expected<std::vector<int16_t>, std::string> decodeBrrToPcm(std::span<const uint8_t> brrData);

}  // namespace ntrak::nspc
