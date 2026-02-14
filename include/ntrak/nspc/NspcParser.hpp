#pragma once

#include "ntrak/nspc/NspcEngine.hpp"
#include "ntrak/nspc/NspcProject.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <variant>

namespace ntrak::nspc {

enum class NspcParseError {
    InvalidConfig,
    InvalidHeader,
    UnsupportedVersion,
    UnexpectedEndOfData,
    InvalidEventData,
};

class NspcParser {
public:
    static std::expected<NspcProject, NspcParseError> load(std::span<const std::uint8_t> data);
};

}  // namespace ntrak::nspc
