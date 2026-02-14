#include "ntrak/nspc/Base64.hpp"

#include <array>

namespace ntrak::nspc {

std::string encodeBase64(std::span<const uint8_t> bytes) {
    static constexpr char kBase64Table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((bytes.size() + 2u) / 3u) * 4u);

    size_t i = 0;
    while (i + 2u < bytes.size()) {
        const uint32_t chunk = (static_cast<uint32_t>(bytes[i]) << 16u) |
                               (static_cast<uint32_t>(bytes[i + 1u]) << 8u) |
                               static_cast<uint32_t>(bytes[i + 2u]);
        out.push_back(kBase64Table[(chunk >> 18u) & 0x3Fu]);
        out.push_back(kBase64Table[(chunk >> 12u) & 0x3Fu]);
        out.push_back(kBase64Table[(chunk >> 6u) & 0x3Fu]);
        out.push_back(kBase64Table[chunk & 0x3Fu]);
        i += 3u;
    }

    const size_t remaining = bytes.size() - i;
    if (remaining == 1u) {
        const uint32_t chunk = static_cast<uint32_t>(bytes[i]) << 16u;
        out.push_back(kBase64Table[(chunk >> 18u) & 0x3Fu]);
        out.push_back(kBase64Table[(chunk >> 12u) & 0x3Fu]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2u) {
        const uint32_t chunk = (static_cast<uint32_t>(bytes[i]) << 16u) | (static_cast<uint32_t>(bytes[i + 1u]) << 8u);
        out.push_back(kBase64Table[(chunk >> 18u) & 0x3Fu]);
        out.push_back(kBase64Table[(chunk >> 12u) & 0x3Fu]);
        out.push_back(kBase64Table[(chunk >> 6u) & 0x3Fu]);
        out.push_back('=');
    }

    return out;
}

std::expected<std::vector<uint8_t>, std::string> decodeBase64(std::string_view encoded) {
    std::array<int8_t, 256> decodeTable;
    decodeTable.fill(-1);
    for (int i = 0; i < 26; ++i) {
        decodeTable[static_cast<size_t>('A' + i)] = static_cast<int8_t>(i);
        decodeTable[static_cast<size_t>('a' + i)] = static_cast<int8_t>(26 + i);
    }
    for (int i = 0; i < 10; ++i) {
        decodeTable[static_cast<size_t>('0' + i)] = static_cast<int8_t>(52 + i);
    }
    decodeTable[static_cast<size_t>('+')] = 62;
    decodeTable[static_cast<size_t>('/')] = 63;

    std::string clean;
    clean.reserve(encoded.size());
    for (char c : encoded) {
        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
            continue;
        }
        clean.push_back(c);
    }

    if (clean.size() % 4u != 0u) {
        return std::unexpected("Base64 payload length is not divisible by 4");
    }

    std::vector<uint8_t> out;
    out.reserve((clean.size() / 4u) * 3u);

    for (size_t i = 0; i < clean.size(); i += 4u) {
        const char c0 = clean[i + 0u];
        const char c1 = clean[i + 1u];
        const char c2 = clean[i + 2u];
        const char c3 = clean[i + 3u];

        if (c0 == '=' || c1 == '=') {
            return std::unexpected("Base64 payload has invalid padding position");
        }
        if (c2 == '=' && c3 != '=') {
            return std::unexpected("Base64 payload has invalid padding pattern");
        }

        const int8_t v0 = decodeTable[static_cast<uint8_t>(c0)];
        const int8_t v1 = decodeTable[static_cast<uint8_t>(c1)];
        if (v0 < 0 || v1 < 0) {
            return std::unexpected("Base64 payload has invalid character");
        }

        const bool hasV2 = (c2 != '=');
        const bool hasV3 = (c3 != '=');
        const int8_t v2 = hasV2 ? decodeTable[static_cast<uint8_t>(c2)] : 0;
        const int8_t v3 = hasV3 ? decodeTable[static_cast<uint8_t>(c3)] : 0;
        if ((hasV2 && v2 < 0) || (hasV3 && v3 < 0)) {
            return std::unexpected("Base64 payload has invalid character");
        }

        out.push_back(static_cast<uint8_t>((static_cast<uint8_t>(v0) << 2u) | (static_cast<uint8_t>(v1) >> 4u)));
        if (hasV2) {
            out.push_back(static_cast<uint8_t>((static_cast<uint8_t>(v1 & 0x0F) << 4u) |
                                               (static_cast<uint8_t>(v2) >> 2u)));
        }
        if (hasV3) {
            out.push_back(static_cast<uint8_t>((static_cast<uint8_t>(v2 & 0x03) << 6u) |
                                               static_cast<uint8_t>(v3)));
        }

        if ((c2 == '=' || c3 == '=') && (i + 4u) < clean.size()) {
            return std::unexpected("Base64 payload has trailing data after padding");
        }
    }

    return out;
}

}  // namespace ntrak::nspc
