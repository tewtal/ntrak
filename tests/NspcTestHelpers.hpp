#pragma once

#include "ntrak/nspc/NspcProject.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace ntrak::nspc::test_helpers {

inline void writeWord(std::array<std::uint8_t, 0x10000>& aram, std::uint16_t address, std::uint16_t value) {
    aram[address] = static_cast<std::uint8_t>(value & 0xFFu);
    aram[static_cast<std::size_t>(address) + 1u] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

inline void writeBrrBlock(std::array<std::uint8_t, 0x10000>& aram, std::uint16_t start, std::uint8_t header) {
    aram[start] = header;
    for (std::uint16_t i = 1; i < 9; ++i) {
        aram[static_cast<std::size_t>(start) + i] = 0;
    }
}

inline NspcProject buildProjectWithTwoSongsTwoAssets(NspcEngineConfig config) {
    std::array<std::uint8_t, 0x10000> aram{};

    // Two valid BRR samples in sample directory.
    writeWord(aram, 0x0200, 0x0500);
    writeWord(aram, 0x0202, 0x0500);
    writeWord(aram, 0x0204, 0x0509);
    writeWord(aram, 0x0206, 0x0509);
    writeBrrBlock(aram, 0x0500, 0x01);
    writeBrrBlock(aram, 0x0509, 0x01);

    // Two instruments + zero terminator.
    aram[0x0300] = 0x00;
    aram[0x0301] = 0x8F;
    aram[0x0302] = 0xE0;
    aram[0x0303] = 0x7F;
    aram[0x0304] = 0x01;
    aram[0x0305] = 0x00;

    aram[0x0306] = 0x01;
    aram[0x0307] = 0x8F;
    aram[0x0308] = 0xE0;
    aram[0x0309] = 0x7F;
    aram[0x030A] = 0x01;
    aram[0x030B] = 0x00;

    // Song table: two songs then terminator.
    writeWord(aram, 0x0400, 0x0600);
    writeWord(aram, 0x0402, 0x0610);
    writeWord(aram, 0x0404, 0x0000);

    // Song 0 and 1 sequences.
    writeWord(aram, 0x0600, 0x0700);
    writeWord(aram, 0x0602, 0x0000);
    writeWord(aram, 0x0610, 0x0710);
    writeWord(aram, 0x0612, 0x0000);

    return NspcProject(std::move(config), std::move(aram));
}

}  // namespace ntrak::nspc::test_helpers
