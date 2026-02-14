#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ntrak::nspc {

std::string encodeBase64(std::span<const uint8_t> bytes);

std::expected<std::vector<uint8_t>, std::string> decodeBase64(std::string_view encoded);

}  // namespace ntrak::nspc
